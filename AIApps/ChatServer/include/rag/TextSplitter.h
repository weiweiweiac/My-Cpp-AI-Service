#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{

class TextSplitter
{
public:
    std::vector<std::string> split(const std::string& text,
        size_t chunkSize = 600,
        size_t overlap = 80) const;
};

} // namespace rag
