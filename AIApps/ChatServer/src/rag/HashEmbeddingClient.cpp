#include "../../include/rag/HashEmbeddingClient.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

namespace
{

std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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

std::vector<std::string> tokenize(const std::string& text)
{
    std::vector<std::string> tokens;
    auto units = utf8Units(text);
    std::string ascii;

    for (const auto& unit : units)
    {
        unsigned char first = static_cast<unsigned char>(unit[0]);
        if (unit.size() == 1 && std::isalnum(first))
        {
            ascii.push_back(static_cast<char>(std::tolower(first)));
            continue;
        }

        if (!ascii.empty())
        {
            tokens.push_back(ascii);
            ascii.clear();
        }

        if (unit.size() > 1)
        {
            tokens.push_back(unit);
        }
    }

    if (!ascii.empty())
    {
        tokens.push_back(ascii);
    }

    for (size_t i = 0; i + 1 < units.size(); ++i)
    {
        if (units[i].size() > 1 && units[i + 1].size() > 1)
        {
            tokens.push_back(units[i] + units[i + 1]);
        }
    }

    return tokens;
}

const std::vector<std::string>& fitnessKeywords()
{
    static const std::vector<std::string> keywords = {
        "卧推", "深蹲", "硬拉", "肩胛", "背阔肌", "胸肌", "臀腿", "膝盖", "肩膀",
        "rpe", "rir", "增肌", "减脂", "热身", "拉伸", "蛋白质", "热量缺口",
        "训练容量", "渐进超负荷", "恢复", "睡眠"
    };
    return keywords;
}

void addToken(std::vector<float>& vector, const std::string& token, float weight)
{
    if (vector.empty() || token.empty())
    {
        return;
    }

    size_t hash = std::hash<std::string>{}(token);
    size_t index = hash % vector.size();
    float sign = ((hash >> 8) & 1U) ? -1.0f : 1.0f;
    vector[index] += sign * weight;
}

} // namespace

namespace rag
{

HashEmbeddingClient::HashEmbeddingClient(size_t dimensions)
    : dimensions_(dimensions == 0 ? 256 : dimensions)
{
}

std::vector<float> HashEmbeddingClient::embed(const std::string& text) const
{
    std::vector<float> vector(dimensions_, 0.0f);
    for (const auto& token : tokenize(text))
    {
        addToken(vector, token, 1.0f);
    }

    std::string lowered = toLowerAscii(text);
    for (const auto& keyword : fitnessKeywords())
    {
        if (lowered.find(keyword) != std::string::npos)
        {
            addToken(vector, keyword, 3.0f);
        }
    }

    double sumSquares = 0.0;
    for (float value : vector)
    {
        sumSquares += static_cast<double>(value) * static_cast<double>(value);
    }

    if (sumSquares <= 0.0)
    {
        return vector;
    }

    float invNorm = static_cast<float>(1.0 / std::sqrt(sumSquares));
    for (auto& value : vector)
    {
        value *= invNorm;
    }

    return vector;
}

} // namespace rag
