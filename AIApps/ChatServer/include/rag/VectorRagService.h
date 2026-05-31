#pragma once

#include "EmbeddingClient.h"
#include "JsonVectorStore.h"
#include "TextSplitter.h"

#include <string>
#include <vector>

namespace rag
{

struct VectorIndexResult
{
    bool success { false };
    std::string message;
    size_t chunkCount { 0 };
};

class VectorRagService
{
public:
    explicit VectorRagService(std::string storePath = "data/rag/vector_store.json");
    VectorRagService(std::string storePath, EmbeddingClient::Config embeddingConfig);

    VectorIndexResult indexText(const std::string& source, const std::string& text);
    std::vector<SearchResult> search(const std::string& query, int topK);
    std::string buildRagPrompt(const std::string& question,
        const std::vector<SearchResult>& contexts) const;

    bool loadStore();
    bool saveStore();
    size_t size() const;
    const std::string& storePath() const;
    bool usingMockEmbedding() const;

private:
    std::vector<DocumentChunk> buildChunks(const std::string& source,
        const std::vector<std::string>& texts) const;
    std::string makeChunkId(const std::string& source,
        size_t index,
        const std::string& text) const;

    EmbeddingClient embeddingClient_;
    TextSplitter splitter_;
    JsonVectorStore store_;
};

} // namespace rag
