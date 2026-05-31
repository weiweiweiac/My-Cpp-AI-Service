#pragma once

#include "../../../../HttpServer/include/utils/JsonUtil.h"

#include <string>
#include <vector>

namespace rag
{

struct DocumentChunk
{
    std::string chunkId;
    std::string docId;
    std::string title;
    std::string source;
    std::string content;
    std::vector<float> embedding;
    std::string createdAt;
};

struct SearchResult
{
    DocumentChunk chunk;
    double score { 0.0 };
};

inline json chunkToJson(const DocumentChunk& chunk)
{
    json body;
    body["chunkId"] = chunk.chunkId;
    body["docId"] = chunk.docId;
    body["title"] = chunk.title;
    body["source"] = chunk.source;
    body["content"] = chunk.content;
    body["text"] = chunk.content;
    body["embedding"] = chunk.embedding;
    body["createdAt"] = chunk.createdAt;
    return body;
}

inline std::string jsonStringValue(const json& body, const std::string& key)
{
    if (!body.contains(key) || body[key].is_null())
    {
        return "";
    }
    if (body[key].is_string())
    {
        return body[key].get<std::string>();
    }
    if (body[key].is_number() || body[key].is_boolean())
    {
        return body[key].dump();
    }
    return "";
}

inline DocumentChunk chunkFromJson(const json& body)
{
    DocumentChunk chunk;
    chunk.chunkId = jsonStringValue(body, "chunkId");
    chunk.docId = jsonStringValue(body, "docId");
    chunk.title = jsonStringValue(body, "title");
    chunk.source = jsonStringValue(body, "source");
    chunk.content = jsonStringValue(body, "content");
    if (chunk.content.empty())
    {
        chunk.content = jsonStringValue(body, "text");
    }
    chunk.createdAt = jsonStringValue(body, "createdAt");
    if (body.contains("embedding") && body["embedding"].is_array())
    {
        for (const auto& value : body["embedding"])
        {
            if (value.is_number())
            {
                chunk.embedding.push_back(value.get<float>());
            }
        }
    }
    return chunk;
}

} // namespace rag
