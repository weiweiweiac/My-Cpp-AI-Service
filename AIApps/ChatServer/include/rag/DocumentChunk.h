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
    body["embedding"] = chunk.embedding;
    body["createdAt"] = chunk.createdAt;
    return body;
}

inline DocumentChunk chunkFromJson(const json& body)
{
    DocumentChunk chunk;
    chunk.chunkId = body.value("chunkId", "");
    chunk.docId = body.value("docId", "");
    chunk.title = body.value("title", "");
    chunk.source = body.value("source", "");
    chunk.content = body.value("content", "");
    chunk.createdAt = body.value("createdAt", "");
    if (body.contains("embedding") && body["embedding"].is_array())
    {
        chunk.embedding = body["embedding"].get<std::vector<float>>();
    }
    return chunk;
}

} // namespace rag
