#include "../../include/rag/JsonVectorStore.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <utility>

namespace
{

std::string trimAscii(const std::string& value)
{
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if (begin >= end)
    {
        return "";
    }
    return std::string(begin, end);
}

bool validChunk(const rag::DocumentChunk& chunk)
{
    return !trimAscii(chunk.chunkId).empty()
        && !trimAscii(chunk.source).empty()
        && !trimAscii(chunk.content).empty()
        && !chunk.embedding.empty();
}

int normalizeTopK(int topK)
{
    if (topK <= 0)
    {
        return 3;
    }
    return std::min(topK, 10);
}

} // namespace

namespace rag
{

JsonVectorStore::JsonVectorStore(std::string path, const EmbeddingClient& embeddingClient)
    : path_(std::move(path)), embeddingClient_(embeddingClient)
{
}

bool JsonVectorStore::addChunk(const DocumentChunk& chunk)
{
    return addChunks(std::vector<DocumentChunk>{ chunk });
}

bool JsonVectorStore::addChunks(const std::vector<DocumentChunk>& chunks)
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t added = 0;
    for (const auto& chunk : chunks)
    {
        if (!validChunk(chunk))
        {
            std::cerr << "[JsonVectorStore] skip invalid chunk id=" << chunk.chunkId
                      << " source=" << chunk.source << std::endl;
            continue;
        }
        chunks_.push_back(chunk);
        ++added;
    }
    std::cerr << "[JsonVectorStore] current chunk count=" << chunks_.size() << std::endl;
    return chunks.empty() || added > 0;
}

std::vector<SearchResult> JsonVectorStore::search(const std::string& query, int topK)
{
    auto queryEmbedding = embeddingClient_.embed(query);
    return searchByEmbedding(queryEmbedding, topK);
}

std::vector<SearchResult> JsonVectorStore::searchByEmbedding(
    const std::vector<float>& queryEmbedding, int topK)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SearchResult> results;
    topK = normalizeTopK(topK);
    std::cerr << "[JsonVectorStore] search chunks=" << chunks_.size()
              << " topK=" << topK << std::endl;

    if (queryEmbedding.empty() || chunks_.empty())
    {
        return results;
    }

    for (const auto& chunk : chunks_)
    {
        if (!validChunk(chunk))
        {
            continue;
        }
        double score = cosineSimilarity(queryEmbedding, chunk.embedding);
        if (score > 0.0)
        {
            results.push_back(SearchResult{ chunk, score });
        }
    }

    std::sort(results.begin(), results.end(), [](const SearchResult& lhs, const SearchResult& rhs) {
        return lhs.score > rhs.score;
    });

    if (static_cast<int>(results.size()) > topK)
    {
        results.resize(static_cast<size_t>(topK));
    }

    for (const auto& result : results)
    {
        std::cerr << "[JsonVectorStore] hit id=" << result.chunk.chunkId
                  << " source=" << result.chunk.source
                  << " score=" << result.score << std::endl;
    }
    return results;
}

bool JsonVectorStore::load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();

    std::filesystem::path filePath(path_);
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec))
    {
        return true;
    }

    std::ifstream input(filePath);
    if (!input.is_open())
    {
        return false;
    }

    json body = json::parse(input, nullptr, false);
    if (body.is_discarded())
    {
        return false;
    }

    const json* rows = nullptr;
    if (body.is_array())
    {
        rows = &body;
    }
    else if (body.contains("chunks") && body["chunks"].is_array())
    {
        rows = &body["chunks"];
    }

    if (rows == nullptr)
    {
        return false;
    }

    for (const auto& item : *rows)
    {
        try
        {
            auto chunk = chunkFromJson(item);
            if (validChunk(chunk))
            {
                chunks_.push_back(std::move(chunk));
            }
            else
            {
                std::cerr << "[JsonVectorStore] skip invalid persisted chunk" << std::endl;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "[JsonVectorStore] skip bad persisted chunk: " << e.what() << std::endl;
        }
    }
    std::cerr << "[JsonVectorStore] loaded chunk count=" << chunks_.size() << std::endl;
    return true;
}

bool JsonVectorStore::save()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::path filePath(path_);
    std::error_code ec;
    if (filePath.has_parent_path())
    {
        std::filesystem::create_directories(filePath.parent_path(), ec);
        if (ec)
        {
            std::cerr << "[JsonVectorStore] failed to create store directory: "
                      << ec.message() << std::endl;
            return false;
        }
    }

    json body;
    body["version"] = 1;
    body["chunks"] = json::array();
    for (const auto& chunk : chunks_)
    {
        body["chunks"].push_back(chunkToJson(chunk));
    }

    std::ofstream output(filePath, std::ios::trunc);
    if (!output.is_open())
    {
        return false;
    }
    output << body.dump(2);
    std::cerr << "[JsonVectorStore] saved chunk count=" << chunks_.size()
              << " path=" << path_ << std::endl;
    return true;
}

bool JsonVectorStore::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
    return true;
}

size_t JsonVectorStore::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.size();
}

double JsonVectorStore::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.empty() || b.empty() || a.size() != b.size())
    {
        return 0.0;
    }

    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        normA += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        normB += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }

    if (normA <= 0.0 || normB <= 0.0)
    {
        return 0.0;
    }
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

} // namespace rag
