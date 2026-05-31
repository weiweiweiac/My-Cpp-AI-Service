#include "../../include/rag/VectorRagService.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace
{

constexpr size_t kChunkSize = 500;
constexpr size_t kChunkOverlap = 80;

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

uint64_t stableHash(const std::string& value)
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value)
    {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string toHex(uint64_t value)
{
    std::ostringstream oss;
    oss << std::hex << value;
    return oss.str();
}

std::string currentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return oss.str();
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

VectorRagService::VectorRagService(std::string storePath)
    : embeddingClient_(),
      store_(std::move(storePath), embeddingClient_)
{
}

VectorRagService::VectorRagService(std::string storePath, EmbeddingClient::Config embeddingConfig)
    : embeddingClient_(std::move(embeddingConfig)),
      store_(std::move(storePath), embeddingClient_)
{
}

VectorIndexResult VectorRagService::indexText(const std::string& source, const std::string& text)
{
    std::string cleanSource = trimAscii(source);
    std::string cleanText = trimAscii(text);
    if (cleanSource.empty())
    {
        return { false, "source 不能为空", 0 };
    }
    if (cleanText.empty())
    {
        return { false, "text 不能为空", 0 };
    }

    auto texts = splitter_.split(cleanText, kChunkSize, kChunkOverlap);
    if (texts.empty())
    {
        return { false, "未生成有效知识片段", 0 };
    }

    auto chunks = buildChunks(cleanSource, texts);
    if (chunks.empty())
    {
        return { false, "未生成有效 embedding", 0 };
    }

    if (!store_.load())
    {
        return { false, "读取 vector RAG 知识库失败", 0 };
    }
    if (!store_.addChunks(chunks) || !store_.save())
    {
        return { false, "保存 vector RAG 知识库失败", 0 };
    }

    return { true, "vector rag index success", chunks.size() };
}

std::vector<SearchResult> VectorRagService::search(const std::string& query, int topK)
{
    std::string cleanQuery = trimAscii(query);
    if (cleanQuery.empty())
    {
        return {};
    }

    store_.load();
    auto queryEmbedding = embeddingClient_.embedText(cleanQuery);
    return store_.searchByEmbedding(queryEmbedding, normalizeTopK(topK));
}

std::string VectorRagService::buildRagPrompt(const std::string& question,
    const std::vector<SearchResult>& contexts) const
{
    std::ostringstream prompt;
    prompt
        << "你是一个严谨的 AI 健身教练。请优先依据以下知识库片段回答用户问题。\n"
        << "如果知识库没有足够信息，请明确说明“当前知识库资料不足”，不要编造。\n\n"
        << "【知识库片段】\n";

    if (contexts.empty())
    {
        prompt << "（本次没有检索到可用知识库片段）\n\n";
    }
    else
    {
        for (size_t i = 0; i < contexts.size(); ++i)
        {
            const auto& result = contexts[i];
            prompt
                << "[" << (i + 1) << "] source=" << result.chunk.source
                << " score=" << std::fixed << std::setprecision(4) << result.score << "\n"
                << result.chunk.content << "\n\n";
        }
    }

    prompt
        << "【用户问题】\n"
        << question << "\n\n"
        << "【回答要求】\n"
        << "1. 回答要清晰、实用。\n"
        << "2. 涉及训练建议时提示个体差异。\n"
        << "3. 不要声称知识库没有提供的信息。\n"
        << "4. 如果资料不足，直接说明当前知识库资料不足。\n";
    return prompt.str();
}

bool VectorRagService::loadStore()
{
    return store_.load();
}

bool VectorRagService::saveStore()
{
    return store_.save();
}

size_t VectorRagService::size() const
{
    return store_.size();
}

const std::string& VectorRagService::storePath() const
{
    return store_.path();
}

bool VectorRagService::usingMockEmbedding() const
{
    return embeddingClient_.isMockMode();
}

std::vector<DocumentChunk> VectorRagService::buildChunks(const std::string& source,
    const std::vector<std::string>& texts) const
{
    std::vector<DocumentChunk> chunks;
    chunks.reserve(texts.size());
    std::string docId = "vector-doc-" + toHex(stableHash(source + currentTimestamp()));
    std::string createdAt = currentTimestamp();
    auto embeddings = embeddingClient_.embedBatch(texts);

    for (size_t i = 0; i < texts.size() && i < embeddings.size(); ++i)
    {
        std::string cleanText = trimAscii(texts[i]);
        if (cleanText.empty() || embeddings[i].empty())
        {
            continue;
        }

        DocumentChunk chunk;
        chunk.docId = docId;
        chunk.chunkId = makeChunkId(source, i + 1, cleanText);
        chunk.title = source;
        chunk.source = source;
        chunk.content = cleanText;
        chunk.embedding = std::move(embeddings[i]);
        chunk.createdAt = createdAt;
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

std::string VectorRagService::makeChunkId(const std::string& source,
    size_t index,
    const std::string& text) const
{
    std::ostringstream oss;
    oss << "vec-" << toHex(stableHash(source))
        << "-" << index
        << "-" << toHex(stableHash(text));
    return oss.str();
}

} // namespace rag
