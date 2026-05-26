#pragma once

#include "DocumentChunk.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{

class VectorStore
{
public:
    virtual ~VectorStore() = default;

    virtual bool addChunks(const std::vector<DocumentChunk>& chunks) = 0;
    virtual std::vector<SearchResult> search(const std::string& query, int topK) = 0;
    virtual bool load() = 0;
    virtual bool save() = 0;
    virtual bool clear() = 0;
    virtual size_t size() const = 0;
};

} // namespace rag
