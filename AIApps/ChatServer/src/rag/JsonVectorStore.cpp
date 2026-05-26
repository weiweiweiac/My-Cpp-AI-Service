#include "../../include/rag/JsonVectorStore.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

namespace rag
{

JsonVectorStore::JsonVectorStore(std::string path, const EmbeddingClient& embeddingClient)
    : path_(std::move(path)), embeddingClient_(embeddingClient)
{
}

bool JsonVectorStore::addChunks(const std::vector<DocumentChunk>& chunks)
{
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.insert(chunks_.end(), chunks.begin(), chunks.end());
    return true;
}

std::vector<SearchResult> JsonVectorStore::search(const std::string& query, int topK)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SearchResult> results;
    if (topK <= 0 || chunks_.empty())
    {
        return results;
    }

    auto queryEmbedding = embeddingClient_.embed(query);
    for (const auto& chunk : chunks_)
    {
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
    return results;
}

bool JsonVectorStore::load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();

    std::filesystem::path filePath(path_);
    if (!std::filesystem::exists(filePath))
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
        chunks_.push_back(chunkFromJson(item));
    }
    return true;
}

bool JsonVectorStore::save()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::path filePath(path_);
    if (filePath.has_parent_path())
    {
        std::filesystem::create_directories(filePath.parent_path());
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
