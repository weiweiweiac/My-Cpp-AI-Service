#pragma once

#include "HashEmbeddingClient.h"
#include "JsonVectorStore.h"
#include "TextSplitter.h"

#include <string>
#include <vector>

namespace rag
{

struct IndexResult
{
    bool success { false };
    std::string message;
    size_t chunkCount { 0 };
};

class FitnessRagService
{
public:
    explicit FitnessRagService(std::string storePath = "data/fitness_rag_store.json");

    IndexResult indexText(const std::string& title,
        const std::string& source,
        const std::string& content);
    std::vector<SearchResult> search(const std::string& query, int topK);
    std::string buildRagPrompt(const std::string& question,
        const std::vector<SearchResult>& retrievedChunks) const;
    bool loadStore();
    bool saveStore();
    size_t size() const;
    const std::string& storePath() const;

private:
    HashEmbeddingClient embeddingClient_;
    TextSplitter splitter_;
    JsonVectorStore store_;
};

} // namespace rag
