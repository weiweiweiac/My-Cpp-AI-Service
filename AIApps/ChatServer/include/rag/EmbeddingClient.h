#pragma once

#include <string>
#include <vector>

namespace rag
{

class EmbeddingClient
{
public:
    virtual ~EmbeddingClient() = default;
    virtual std::vector<float> embed(const std::string& text) const = 0;
};

} // namespace rag
