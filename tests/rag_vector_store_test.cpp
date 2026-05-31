#include "AIApps/ChatServer/include/rag/EmbeddingClient.h"
#include "AIApps/ChatServer/include/rag/JsonVectorStore.h"
#include "AIApps/ChatServer/include/rag/VectorRagService.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

double norm(const std::vector<float>& values)
{
    double sum = 0.0;
    for (float value : values)
    {
        sum += static_cast<double>(value) * static_cast<double>(value);
    }
    return std::sqrt(sum);
}

rag::DocumentChunk makeChunk(const std::string& chunkId,
    const std::string& source,
    const std::string& text,
    std::vector<float> embedding)
{
    rag::DocumentChunk chunk;
    chunk.chunkId = chunkId;
    chunk.docId = "doc-" + chunkId;
    chunk.title = source;
    chunk.source = source;
    chunk.content = text;
    chunk.embedding = std::move(embedding);
    chunk.createdAt = "2026-05-31 00:00:00";
    return chunk;
}

std::string repeatText(const std::string& text, int times)
{
    std::string value;
    for (int i = 0; i < times; ++i)
    {
        value += text;
    }
    return value;
}

} // namespace

int main()
{
    rag::EmbeddingClient::Config mockConfig;
    mockConfig.mockDimensions = 256;
    rag::EmbeddingClient embeddingClient(mockConfig);
    auto first = embeddingClient.embedText("卧推时肩胛后缩下沉可以保护肩膀");
    auto second = embeddingClient.embedText("卧推时肩胛后缩下沉可以保护肩膀");
    auto unrelated = embeddingClient.embedText("蛋白质和睡眠会影响训练恢复");

    require(embeddingClient.isMockMode(), "EmbeddingClient should use mock mode without env configuration");
    require(first.size() == 256, "Mock embedding should use a stable 256-dimension vector");
    require(first == second, "Mock embedding should be deterministic for the same text");
    require(std::abs(norm(first) - 1.0) < 0.0001, "Mock embedding should be L2-normalized");
    require(first != unrelated, "Different texts should not produce identical mock embeddings");

    require(std::abs(rag::JsonVectorStore::cosineSimilarity({1.0f, 0.0f}, {1.0f, 0.0f}) - 1.0) < 0.0001,
        "Cosine similarity should be 1 for identical unit vectors");
    require(std::abs(rag::JsonVectorStore::cosineSimilarity({1.0f, 0.0f}, {0.0f, 1.0f})) < 0.0001,
        "Cosine similarity should be 0 for orthogonal vectors");
    require(rag::JsonVectorStore::cosineSimilarity({1.0f}, {1.0f, 0.0f}) == 0.0,
        "Cosine similarity should safely reject dimension mismatch");

    const std::filesystem::path storePath = std::filesystem::current_path() / "rag_vector_store_test.json";
    std::filesystem::remove(storePath);

    rag::JsonVectorStore store(storePath.string(), embeddingClient);
    require(store.searchByEmbedding({1.0f, 0.0f}, 3).empty(), "Empty vector store search should return no results");

    require(store.addChunk(makeChunk("bench", "bench_press.md",
        "卧推时保持肩胛后缩下沉，避免肩关节过度前移。", {1.0f, 0.0f})),
        "VectorStore should add a single chunk");
    require(store.addChunk(makeChunk("squat", "squat.md",
        "深蹲时保持核心收紧，膝盖方向与脚尖一致。", {0.2f, 0.8f})),
        "VectorStore should add another chunk");
    require(store.size() == 2, "VectorStore should track chunk count");

    auto topResults = store.searchByEmbedding({1.0f, 0.0f}, 2);
    require(topResults.size() == 2, "VectorStore should return requested TopK when available");
    require(topResults.front().chunk.chunkId == "bench", "TopK search should sort by cosine score descending");
    require(topResults.front().score >= topResults.back().score, "Search scores should be descending");
    require(store.save(), "VectorStore should persist chunks to JSON");

    rag::JsonVectorStore reloaded(storePath.string(), embeddingClient);
    require(reloaded.load(), "VectorStore should load persisted JSON");
    require(reloaded.size() == 2, "VectorStore should restore chunk count after load");

    rag::VectorRagService vectorRag(storePath.string(), mockConfig);
    auto emptyIndex = vectorRag.indexText("empty.md", "   ");
    require(!emptyIndex.success, "VectorRagService should reject empty text");

    std::filesystem::remove(storePath);
    rag::VectorRagService service(storePath.string(), mockConfig);
    std::string longBenchText = repeatText(
        "杠铃卧推是一种经典胸部训练动作，主要刺激胸大肌，同时需要肩胛稳定。"
        "动作过程中应保持肩胛后缩下沉，避免肩关节过度前移。", 12);
    auto indexResult = service.indexText("bench_press.md", longBenchText);
    require(indexResult.success, "VectorRagService should index valid text");
    require(indexResult.chunkCount >= 2, "VectorRagService should split long text into multiple chunks");
    require(std::filesystem::exists(storePath), "VectorRagService should persist vector store JSON");

    auto searchResults = service.search("卧推怎么保护肩膀", 3);
    require(!searchResults.empty(), "VectorRagService should retrieve indexed chunks");
    require(searchResults.front().chunk.source == "bench_press.md", "Search result should keep source metadata");
    require(searchResults.front().score > 0.0, "Search result should include positive score");

    std::string prompt = service.buildRagPrompt("卧推怎么保护肩膀？", searchResults);
    require(prompt.find("当前知识库资料不足") != std::string::npos,
        "Vector RAG prompt should include insufficient-knowledge instruction");
    require(prompt.find("source=bench_press.md") != std::string::npos,
        "Vector RAG prompt should include context source");
    require(prompt.find("卧推怎么保护肩膀") != std::string::npos,
        "Vector RAG prompt should include the user question");

    std::filesystem::remove(storePath);
    return 0;
}
