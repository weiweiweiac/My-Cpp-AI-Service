#pragma once

#include "EmbeddingClient.h"
#include "VectorStore.h"

#include <mutex>
#include <string>
#include <vector>

namespace rag
{

class JsonVectorStore : public VectorStore
{
public:
    JsonVectorStore(std::string path, const EmbeddingClient& embeddingClient);

    bool addChunks(const std::vector<DocumentChunk>& chunks) override;
    std::vector<SearchResult> search(const std::string& query, int topK) override;
    bool load() override;
    bool save() override;
    bool clear() override;
    size_t size() const override;

    const std::string& path() const { return path_; }

private:
    static double cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

    std::string path_;
    const EmbeddingClient& embeddingClient_;
    std::vector<DocumentChunk> chunks_;
    mutable std::mutex mutex_;
};

} // namespace rag
