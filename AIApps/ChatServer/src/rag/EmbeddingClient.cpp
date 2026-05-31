#include "../../include/rag/EmbeddingClient.h"

#include "../../../../HttpServer/include/utils/JsonUtil.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace
{

std::string envValue(const char* name)
{
    const char* value = std::getenv(name);
    return value == nullptr ? "" : std::string(value);
}

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

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

uint64_t stableHash(const std::string& value)
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value)
    {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
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

void addToken(std::vector<float>& vector, const std::string& token, float weight)
{
    if (vector.empty() || token.empty())
    {
        return;
    }

    uint64_t hash = stableHash(token);
    size_t index = static_cast<size_t>(hash % vector.size());
    float sign = ((hash >> 8) & 1ULL) ? -1.0f : 1.0f;
    vector[index] += sign * weight;

    size_t secondIndex = static_cast<size_t>(((hash >> 32) ^ hash) % vector.size());
    vector[secondIndex] += sign * weight * 0.5f;
}

void normalize(std::vector<float>& vector)
{
    double sumSquares = 0.0;
    for (float value : vector)
    {
        sumSquares += static_cast<double>(value) * static_cast<double>(value);
    }
    if (sumSquares <= 0.0)
    {
        return;
    }

    float invNorm = static_cast<float>(1.0 / std::sqrt(sumSquares));
    for (auto& value : vector)
    {
        value *= invNorm;
    }
}

bool parseEmbeddingArray(const json& body, std::vector<float>& embedding)
{
    if (!body.is_array())
    {
        return false;
    }

    std::vector<float> values;
    values.reserve(body.size());
    for (const auto& item : body)
    {
        if (!item.is_number())
        {
            return false;
        }
        values.push_back(item.get<float>());
    }

    embedding = std::move(values);
    return !embedding.empty();
}

std::vector<float> extractEmbedding(const json& body)
{
    std::vector<float> embedding;
    if (body.contains("data") && body["data"].is_array() && !body["data"].empty()
        && body["data"][0].contains("embedding")
        && parseEmbeddingArray(body["data"][0]["embedding"], embedding))
    {
        return embedding;
    }

    if (body.contains("output") && body["output"].contains("embeddings")
        && body["output"]["embeddings"].is_array() && !body["output"]["embeddings"].empty()
        && body["output"]["embeddings"][0].contains("embedding")
        && parseEmbeddingArray(body["output"]["embeddings"][0]["embedding"], embedding))
    {
        return embedding;
    }

    if (body.contains("embedding") && parseEmbeddingArray(body["embedding"], embedding))
    {
        return embedding;
    }

    return {};
}

} // namespace

namespace rag
{

EmbeddingClient::EmbeddingClient()
    : EmbeddingClient(Config{
        envValue("EMBEDDING_API_KEY"),
        envValue("EMBEDDING_BASE_URL"),
        envValue("EMBEDDING_MODEL"),
        256
    })
{
}

EmbeddingClient::EmbeddingClient(Config config)
    : config_(std::move(config))
{
    if (config_.mockDimensions == 0)
    {
        config_.mockDimensions = 256;
    }
}

std::vector<float> EmbeddingClient::embed(const std::string& text) const
{
    return embedText(text);
}

std::vector<float> EmbeddingClient::embedText(const std::string& text) const
{
    std::string cleanText = trimAscii(text);
    if (cleanText.empty())
    {
        return std::vector<float>(config_.mockDimensions, 0.0f);
    }

    if (!isMockMode())
    {
        auto embedding = requestRemoteEmbedding(cleanText);
        if (!embedding.empty())
        {
            return embedding;
        }
        std::cerr << "[EmbeddingClient] remote embedding failed, fallback to deterministic mock embedding"
                  << std::endl;
    }

    return buildMockEmbedding(cleanText);
}

std::vector<std::vector<float>> EmbeddingClient::embedBatch(const std::vector<std::string>& texts) const
{
    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(texts.size());
    for (const auto& text : texts)
    {
        embeddings.push_back(embedText(text));
    }
    return embeddings;
}

bool EmbeddingClient::isMockMode() const
{
    return config_.apiKey.empty() || config_.baseUrl.empty() || config_.model.empty();
}

std::vector<float> EmbeddingClient::requestRemoteEmbedding(const std::string& text) const
{
    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        std::cerr << "[EmbeddingClient] failed to initialize curl" << std::endl;
        return {};
    }

    json payload;
    payload["model"] = config_.model;
    payload["input"] = text;
    std::string payloadText = payload.dump();
    std::string responseText;

    struct curl_slist* headers = nullptr;
    std::string authHeader = "Authorization: Bearer " + config_.apiKey;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, config_.baseUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadText.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseText);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    CURLcode result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK)
    {
        std::cerr << "[EmbeddingClient] curl failed: " << curl_easy_strerror(result) << std::endl;
        return {};
    }
    if (statusCode < 200 || statusCode >= 300)
    {
        std::cerr << "[EmbeddingClient] embedding API HTTP " << statusCode
                  << ", body=" << responseText << std::endl;
        return {};
    }

    json body = json::parse(responseText, nullptr, false);
    if (body.is_discarded())
    {
        std::cerr << "[EmbeddingClient] failed to parse embedding response: " << responseText << std::endl;
        return {};
    }

    auto embedding = extractEmbedding(body);
    if (embedding.empty())
    {
        std::cerr << "[EmbeddingClient] embedding response did not contain a usable vector" << std::endl;
    }
    return embedding;
}

std::vector<float> EmbeddingClient::buildMockEmbedding(const std::string& text) const
{
    std::vector<float> vector(config_.mockDimensions, 0.0f);
    for (const auto& token : tokenize(text))
    {
        addToken(vector, token, 1.0f);
    }

    static const std::vector<std::string> keywords = {
        "卧推", "深蹲", "硬拉", "肩胛", "肩膀", "胸肌", "核心", "膝盖",
        "蛋白质", "睡眠", "恢复", "增肌", "减脂", "热身", "拉伸"
    };
    for (const auto& keyword : keywords)
    {
        if (text.find(keyword) != std::string::npos)
        {
            addToken(vector, keyword, 3.0f);
        }
    }

    normalize(vector);
    return vector;
}

} // namespace rag
