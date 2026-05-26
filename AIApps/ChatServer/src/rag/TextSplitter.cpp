#include "../../include/rag/TextSplitter.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{

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

std::vector<std::string> utf8Units(const std::string& text)
{
    std::vector<std::string> units;
    for (size_t i = 0; i < text.size();)
    {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        size_t len = 1;
        if ((ch & 0x80) == 0)
        {
            len = 1;
        }
        else if ((ch & 0xE0) == 0xC0 && i + 1 < text.size())
        {
            len = 2;
        }
        else if ((ch & 0xF0) == 0xE0 && i + 2 < text.size())
        {
            len = 3;
        }
        else if ((ch & 0xF8) == 0xF0 && i + 3 < text.size())
        {
            len = 4;
        }
        units.push_back(text.substr(i, len));
        i += len;
    }
    return units;
}

size_t utf8Length(const std::string& text)
{
    return utf8Units(text).size();
}

std::string joinUnits(const std::vector<std::string>& units, size_t begin, size_t end)
{
    std::string value;
    for (size_t i = begin; i < end && i < units.size(); ++i)
    {
        value += units[i];
    }
    return value;
}

std::vector<std::string> splitLongText(const std::string& text, size_t chunkSize, size_t overlap)
{
    std::vector<std::string> chunks;
    auto units = utf8Units(text);
    if (units.empty())
    {
        return chunks;
    }

    chunkSize = std::max<size_t>(chunkSize, 1);
    overlap = std::min(overlap, chunkSize > 1 ? chunkSize - 1 : 0);

    for (size_t start = 0; start < units.size();)
    {
        size_t end = std::min(start + chunkSize, units.size());
        std::string chunk = trimAscii(joinUnits(units, start, end));
        if (!chunk.empty())
        {
            chunks.push_back(chunk);
        }
        if (end == units.size())
        {
            break;
        }
        start = end > overlap ? end - overlap : end;
    }

    return chunks;
}

std::vector<std::string> paragraphsFromText(const std::string& text)
{
    std::vector<std::string> paragraphs;
    std::istringstream input(text);
    std::string line;
    std::string current;

    while (std::getline(input, line))
    {
        std::string trimmed = trimAscii(line);
        if (trimmed.empty())
        {
            if (!current.empty())
            {
                paragraphs.push_back(current);
                current.clear();
            }
            continue;
        }

        if (!current.empty())
        {
            current += "\n";
        }
        current += trimmed;
    }

    if (!current.empty())
    {
        paragraphs.push_back(current);
    }

    return paragraphs;
}

} // namespace

namespace rag
{

std::vector<std::string> TextSplitter::split(const std::string& text,
    size_t chunkSize,
    size_t overlap) const
{
    std::vector<std::string> chunks;
    std::string current;

    for (const auto& paragraph : paragraphsFromText(text))
    {
        if (utf8Length(paragraph) > chunkSize)
        {
            if (!trimAscii(current).empty())
            {
                chunks.push_back(trimAscii(current));
                current.clear();
            }
            auto parts = splitLongText(paragraph, chunkSize, overlap);
            chunks.insert(chunks.end(), parts.begin(), parts.end());
            continue;
        }

        if (current.empty())
        {
            current = paragraph;
            continue;
        }

        std::string candidate = current + "\n\n" + paragraph;
        if (utf8Length(candidate) <= chunkSize)
        {
            current = candidate;
        }
        else
        {
            chunks.push_back(trimAscii(current));
            current = paragraph;
        }
    }

    if (!trimAscii(current).empty())
    {
        chunks.push_back(trimAscii(current));
    }

    return chunks;
}

} // namespace rag
