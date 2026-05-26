#pragma once

#include "EmbeddingClient.h"

#include <cstddef>

namespace rag
{

class HashEmbeddingClient : public EmbeddingClient
{
public:
    explicit HashEmbeddingClient(size_t dimensions = 256);

    std::vector<float> embed(const std::string& text) const override;
    size_t dimensions() const { return dimensions_; }

private:
    size_t dimensions_;
};

} // namespace rag
