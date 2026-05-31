#include "../../include/handlers/FitnessRagHandler.h"

#include "../../include/AIUtil/AIHelper.h"
#include "../../include/AIUtil/AIFactory.h"
#include "AIApps/ChatServer/include/auth/AIQuotaService.h"
#include "../../include/rag/FitnessRagService.h"
#include "../../include/rag/VectorRagService.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace
{

std::string trim(const std::string& value)
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

bool parseJsonBody(const http::HttpRequest& req, json& body, std::string& message)
{
    if (req.getBody().empty())
    {
        message = "请求体不能为空";
        return false;
    }
    try
    {
        body = json::parse(req.getBody());
        if (!body.is_object())
        {
            message = "请求 JSON 必须是对象";
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        message = std::string("请求 JSON 格式错误: ") + e.what();
        return false;
    }
}

std::string jsonString(const json& body, const std::string& key)
{
    if (!body.contains(key) || body[key].is_null())
    {
        return "";
    }
    if (body[key].is_string())
    {
        return trim(body[key].get<std::string>());
    }
    return body[key].dump();
}

std::string jsonStringAny(const json& body, const std::string& firstKey, const std::string& secondKey)
{
    std::string value = jsonString(body, firstKey);
    if (!value.empty())
    {
        return value;
    }
    return jsonString(body, secondKey);
}

int jsonInt(const json& body, const std::string& key, int fallback)
{
    if (!body.contains(key) || body[key].is_null())
    {
        return fallback;
    }
    try
    {
        if (body[key].is_number_integer())
        {
            return body[key].get<int>();
        }
        if (body[key].is_string())
        {
            return std::stoi(body[key].get<std::string>());
        }
    }
    catch (const std::exception&)
    {
    }
    return fallback;
}

int normalizeTopK(int topK)
{
    if (topK <= 0)
    {
        return 5;
    }
    return std::min(topK, 10);
}

int normalizeVectorTopK(int topK)
{
    if (topK <= 0)
    {
        return 3;
    }
    return std::min(topK, 10);
}

int fromHex(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::string urlDecode(const std::string& value)
{
    std::string decoded;
    decoded.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '+')
        {
            decoded.push_back(' ');
        }
        else if (value[i] == '%' && i + 2 < value.size())
        {
            int high = fromHex(value[i + 1]);
            int low = fromHex(value[i + 2]);
            if (high >= 0 && low >= 0)
            {
                decoded.push_back(static_cast<char>(high * 16 + low));
                i += 2;
            }
            else
            {
                decoded.push_back(value[i]);
            }
        }
        else
        {
            decoded.push_back(value[i]);
        }
    }

    return decoded;
}

std::string queryParam(const http::HttpRequest& req, const std::string& key)
{
    return trim(urlDecode(req.getQueryParameters(key)));
}

std::string normalizeModelType(const std::string& modelType)
{
    return trim(modelType) == "2" ? "2" : "1";
}

json searchResultsToJson(const std::vector<rag::SearchResult>& results)
{
    json rows = json::array();
    for (const auto& result : results)
    {
        json row;
        row["chunkId"] = result.chunk.chunkId;
        row["title"] = result.chunk.title;
        row["source"] = result.chunk.source;
        row["score"] = result.score;
        row["content"] = result.chunk.content;
        rows.push_back(row);
    }
    return rows;
}

json vectorSearchResultsToJson(const std::vector<rag::SearchResult>& results)
{
    json rows = json::array();
    for (const auto& result : results)
    {
        json row;
        row["id"] = result.chunk.chunkId;
        row["source"] = result.chunk.source;
        row["text"] = result.chunk.content;
        row["score"] = result.score;
        rows.push_back(row);
    }
    return rows;
}

std::string callAiWithPrompt(const std::string& prompt, const std::string& modelType)
{
    auto strategy = StrategyFactory::instance().create(normalizeModelType(modelType));
    AIHelper helper;
    helper.setStrategy(strategy);
    std::vector<std::pair<std::string, long long>> messages = {{ prompt, 0 }};
    json response = helper.request(strategy->buildRequest(messages));
    return strategy->parseResponse(response);
}

std::string streamAiWithPrompt(const std::string& prompt,
    const std::string& modelType,
    AIHelper::StreamCallback onChunk)
{
    auto strategy = StrategyFactory::instance().create(normalizeModelType(modelType));
    AIHelper helper;
    helper.setStrategy(strategy);
    std::vector<std::pair<std::string, long long>> messages = {{ prompt, 0 }};
    return helper.requestStream(strategy->buildStreamRequest(messages), std::move(onChunk));
}

} // namespace

void FitnessRagHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    switch (action_)
    {
    case Action::Index:
        handleIndex(req, resp);
        break;
    case Action::Search:
        handleSearch(req, resp);
        break;
    case Action::ChatRag:
        handleChatRag(req, resp);
        break;
    case Action::VectorIndex:
        handleVectorIndex(req, resp);
        break;
    case Action::VectorSearch:
        handleVectorSearch(req, resp);
        break;
    case Action::VectorChatRag:
        handleVectorChatRag(req, resp);
        break;
    }
}

void FitnessRagHandler::handleIndex(const http::HttpRequest& req, http::HttpResponse* resp)
{
    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    rag::FitnessRagService service;
    auto result = service.indexText(
        jsonString(requestBody, "title"),
        jsonString(requestBody, "source"),
        jsonString(requestBody, "content"));

    json body;
    body["success"] = result.success;
    body["message"] = result.message;
    body["chunkCount"] = result.chunkCount;
    body["storePath"] = service.storePath();
    sendJson(req, resp,
        result.success ? http::HttpResponse::k200Ok : http::HttpResponse::k400BadRequest,
        result.success ? "OK" : "Bad Request",
        body,
        !result.success);
}

void FitnessRagHandler::handleSearch(const http::HttpRequest& req, http::HttpResponse* resp)
{
    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    std::string query = jsonString(requestBody, "query");
    if (query.empty())
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", "query 不能为空"}}, true);
        return;
    }

    rag::FitnessRagService service;
    service.loadStore();
    if (service.size() == 0)
    {
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"message", "知识库为空，请先导入知识"}, {"results", json::array()}}, false);
        return;
    }

    int topK = normalizeTopK(jsonInt(requestBody, "topK", 5));
    auto results = service.search(query, topK);
    sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
        json{{"success", true}, {"message", "检索完成"}, {"results", searchResultsToJson(results)}}, false);
}

void FitnessRagHandler::handleChatRag(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!requireLogin(req, resp, userId))
    {
        return;
    }
    std::string modelType;
    bool aiCallStarted = false;
    bool quotaFinalized = false;

    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    std::string question = jsonString(requestBody, "question");
    if (question.empty())
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", "question 不能为空"}}, true);
        return;
    }

    rag::FitnessRagService service;
    service.loadStore();
    if (service.size() == 0)
    {
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"answer", "请先导入知识，再使用 RAG 增强问答"}, {"retrievedChunks", json::array()}}, false);
        return;
    }

    int topK = normalizeTopK(jsonInt(requestBody, "topK", 5));
    auto results = service.search(question, topK);
    if (results.empty())
    {
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"answer", "没有检索到相关知识片段，请先导入更相关的健身知识"}, {"retrievedChunks", json::array()}}, false);
        return;
    }

    modelType = jsonString(requestBody, "modelType");
    auth::AIQuotaService quotaService;
    auto quotaCheck = quotaService.checkBeforeAI(userId);
    if (!quotaCheck.allowed)
    {
        sendJson(req, resp,
            quotaCheck.systemError ? http::HttpResponse::k500InternalServerError : http::HttpResponse::k403Forbidden,
            quotaCheck.systemError ? "Internal Server Error" : "Forbidden",
            json{{"success", false}, {"message", quotaCheck.message}},
            true);
        return;
    }

    try
    {
        std::string prompt = service.buildRagPrompt(question, results);
        aiCallStarted = true;
        std::string answer = callAiWithPrompt(prompt, modelType);
        if (answer.empty())
        {
            quotaService.logAIUsage(userId, "/chat/rag-send", modelType,
                false, false, "AI returned empty RAG answer");
            quotaFinalized = true;
            answer = "AI 未返回可用回答";
        }
        else
        {
            quotaService.consumeQuotaOnSuccess(userId, "/chat/rag-send", modelType);
            quotaFinalized = true;
        }
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"answer", answer}, {"retrievedChunks", searchResultsToJson(results)}}, false);
    }
    catch (const std::exception& e)
    {
        if (aiCallStarted && !quotaFinalized)
        {
            quotaService.logAIUsage(userId, "/chat/rag-send", modelType,
                false, false, e.what());
        }
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false}, {"message", std::string("RAG 问答失败: ") + e.what()},
                 {"retrievedChunks", searchResultsToJson(results)}},
            true);
    }
}

void FitnessRagHandler::handleVectorIndex(const http::HttpRequest& req, http::HttpResponse* resp)
{
    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    std::string source = jsonString(requestBody, "source");
    std::string text = jsonStringAny(requestBody, "text", "content");
    if (source.empty())
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", "source 不能为空"}}, true);
        return;
    }
    if (text.empty())
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", "text 不能为空"}}, true);
        return;
    }

    rag::VectorRagService service;
    auto result = service.indexText(source, text);
    json body;
    body["success"] = result.success;
    body["message"] = result.message;
    body["chunks"] = result.chunkCount;
    body["storePath"] = service.storePath();
    body["embeddingMode"] = service.usingMockEmbedding() ? "mock" : "remote";

    sendJson(req, resp,
        result.success ? http::HttpResponse::k200Ok : http::HttpResponse::k400BadRequest,
        result.success ? "OK" : "Bad Request",
        body,
        !result.success);
}

void FitnessRagHandler::handleVectorSearch(const http::HttpRequest& req, http::HttpResponse* resp)
{
    std::string query = queryParam(req, "query");
    if (query.empty())
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", "query 不能为空"}}, true);
        return;
    }

    int topK = normalizeVectorTopK(3);
    std::string topKText = queryParam(req, "topK");
    if (!topKText.empty())
    {
        try
        {
            topK = normalizeVectorTopK(std::stoi(topKText));
        }
        catch (const std::exception&)
        {
            topK = 3;
        }
    }

    rag::VectorRagService service;
    service.loadStore();
    auto results = service.size() == 0 ? std::vector<rag::SearchResult>{} : service.search(query, topK);
    sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
        json{{"success", true},
             {"message", "vector rag search success"},
             {"results", vectorSearchResultsToJson(results)},
             {"storePath", service.storePath()},
             {"embeddingMode", service.usingMockEmbedding() ? "mock" : "remote"}},
        false);
}

void FitnessRagHandler::handleVectorChatRag(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!requireLogin(req, resp, userId))
    {
        return;
    }

    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    std::string question = jsonStringAny(requestBody, "message", "question");
    if (question.empty())
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", "message 不能为空"}}, true);
        return;
    }

    int topK = normalizeVectorTopK(jsonInt(requestBody, "topK", 3));
    std::string modelType = jsonString(requestBody, "modelType");

    rag::VectorRagService service;
    service.loadStore();
    if (service.size() == 0)
    {
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true},
                 {"answer", "请先导入 vector RAG 知识，再使用增强问答"},
                 {"contexts", json::array()}},
            false);
        return;
    }

    auto results = service.search(question, topK);
    if (results.empty())
    {
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true},
                 {"answer", "没有检索到相关知识片段，请先导入更相关的知识"},
                 {"contexts", json::array()}},
            false);
        return;
    }

    auth::AIQuotaService quotaService;
    auto quotaCheck = quotaService.checkBeforeAI(userId);
    if (!quotaCheck.allowed)
    {
        sendJson(req, resp,
            quotaCheck.systemError ? http::HttpResponse::k500InternalServerError : http::HttpResponse::k403Forbidden,
            quotaCheck.systemError ? "Internal Server Error" : "Forbidden",
            json{{"success", false}, {"message", quotaCheck.message}},
            true);
        return;
    }

    bool aiCallStarted = false;
    bool quotaFinalized = false;
    try
    {
        std::string prompt = service.buildRagPrompt(question, results);
        aiCallStarted = true;
        std::string answer = callAiWithPrompt(prompt, modelType);
        if (answer.empty())
        {
            quotaService.logAIUsage(userId, "/chat/vector-rag-send", modelType,
                false, false, "AI returned empty vector RAG answer");
            quotaFinalized = true;
            answer = "AI 未返回可用回答";
        }
        else
        {
            quotaService.consumeQuotaOnSuccess(userId, "/chat/vector-rag-send", modelType);
            quotaFinalized = true;
        }

        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true},
                 {"answer", answer},
                 {"contexts", vectorSearchResultsToJson(results)}},
            false);
    }
    catch (const std::exception& e)
    {
        if (aiCallStarted && !quotaFinalized)
        {
            quotaService.logAIUsage(userId, "/chat/vector-rag-send", modelType,
                false, false, e.what());
        }
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false},
                 {"message", std::string("Vector RAG 问答失败: ") + e.what()},
                 {"contexts", vectorSearchResultsToJson(results)}},
            true);
    }
}

void FitnessRagHandler::handleChatRagStream(const http::HttpRequest& req, http::HttpStreamWriter& writer)
{
    writer.sendSseHeader();

    http::HttpResponse sessionResp(false);
    auto session = server_->getSessionManager()->getSession(req, &sessionResp);
    if (session->getValue("isLoggedIn") != "true")
    {
        writer.sendError("请先登录");
        writer.sendDone();
        writer.close();
        return;
    }

    int userId = 0;
    try
    {
        userId = std::stoi(session->getValue("userId"));
    }
    catch (const std::exception&)
    {
        writer.sendError("登录状态异常，请重新登录");
        writer.sendDone();
        writer.close();
        return;
    }
    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        writer.sendError(errorMessage);
        writer.sendDone();
        writer.close();
        return;
    }

    std::string question = jsonString(requestBody, "question");
    if (question.empty())
    {
        writer.sendError("question 不能为空");
        writer.sendDone();
        writer.close();
        return;
    }

    int topK = normalizeTopK(jsonInt(requestBody, "topK", 5));
    std::string modelType = jsonString(requestBody, "modelType");

    writer.sendStatus("正在检索健身知识库");
    std::thread([writer, question, modelType, topK, userId]() mutable {
        auth::AIQuotaService quotaService;
        bool aiCallStarted = false;
        bool quotaFinalized = false;
        try
        {
            rag::FitnessRagService service;
            service.loadStore();
            if (service.size() == 0)
            {
                writer.sendError("知识库为空或没有检索到相关片段，请先导入知识");
                writer.sendDone();
                writer.close();
                return;
            }

            auto results = service.search(question, topK);
            if (results.empty())
            {
                writer.sendError("知识库为空或没有检索到相关片段，请先导入知识");
                writer.sendDone();
                writer.close();
                return;
            }

            auto quotaCheck = quotaService.checkBeforeAI(userId);
            if (!quotaCheck.allowed)
            {
                writer.sendError(quotaCheck.message);
                writer.sendDone();
                writer.close();
                return;
            }

            writer.sendEvent("retrieved", json{{"chunks", searchResultsToJson(results)}}.dump());
            writer.sendStatus("正在基于参考片段生成回答");

            std::string prompt = service.buildRagPrompt(question, results);
            std::string answer;
            try
            {
                aiCallStarted = true;
                answer = streamAiWithPrompt(prompt, modelType, [&writer](const std::string& chunk) {
                    if (!chunk.empty())
                    {
                        writer.sendMessage(chunk);
                    }
                });
            }
            catch (const std::exception& e)
            {
                writer.sendError(std::string("AI 流式调用失败，正在切换同步回答: ") + e.what());
            }

            if (answer.empty())
            {
                try
                {
                    aiCallStarted = true;
                    answer = callAiWithPrompt(prompt, modelType);
                    if (!answer.empty())
                    {
                        writer.sendMessage(answer);
                    }
                }
                catch (const std::exception& e)
                {
                    quotaService.logAIUsage(userId, "/chat/rag-send-stream", modelType,
                        false, false, e.what());
                    quotaFinalized = true;
                    writer.sendError(std::string("AI 回答失败: ") + e.what());
                    writer.sendDone();
                    writer.close();
                    return;
                }
            }

            if (answer.empty())
            {
                quotaService.logAIUsage(userId, "/chat/rag-send-stream", modelType,
                    false, false, "AI returned empty RAG answer");
                quotaFinalized = true;
                writer.sendError("AI 未返回可用回答");
            }
            else
            {
                quotaService.consumeQuotaOnSuccess(userId, "/chat/rag-send-stream", modelType);
                quotaFinalized = true;
            }
            writer.sendDone();
        }
        catch (const std::exception& e)
        {
            if (aiCallStarted && !quotaFinalized)
            {
                quotaService.logAIUsage(userId, "/chat/rag-send-stream", modelType,
                    false, false, e.what());
            }
            writer.sendError(std::string("RAG 流式问答失败: ") + e.what());
            writer.sendDone();
        }
        writer.close();
    }).detach();
}

void FitnessRagHandler::handleVectorChatRagStream(const http::HttpRequest& req, http::HttpStreamWriter& writer)
{
    writer.sendSseHeader();

    http::HttpResponse sessionResp(false);
    auto session = server_->getSessionManager()->getSession(req, &sessionResp);
    if (session->getValue("isLoggedIn") != "true")
    {
        writer.sendError("请先登录");
        writer.sendDone();
        writer.close();
        return;
    }

    int userId = 0;
    try
    {
        userId = std::stoi(session->getValue("userId"));
    }
    catch (const std::exception&)
    {
        writer.sendError("登录状态异常，请重新登录");
        writer.sendDone();
        writer.close();
        return;
    }

    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        writer.sendError(errorMessage);
        writer.sendDone();
        writer.close();
        return;
    }

    std::string question = jsonStringAny(requestBody, "message", "question");
    if (question.empty())
    {
        writer.sendError("message 不能为空");
        writer.sendDone();
        writer.close();
        return;
    }

    int topK = normalizeVectorTopK(jsonInt(requestBody, "topK", 3));
    std::string modelType = jsonString(requestBody, "modelType");

    writer.sendStatus("正在检索 vector RAG 知识库");
    std::thread([writer, question, modelType, topK, userId]() mutable {
        auth::AIQuotaService quotaService;
        bool aiCallStarted = false;
        bool quotaFinalized = false;
        try
        {
            rag::VectorRagService service;
            service.loadStore();
            if (service.size() == 0)
            {
                writer.sendError("vector RAG 知识库为空，请先导入知识");
                writer.sendDone();
                writer.close();
                return;
            }

            auto results = service.search(question, topK);
            if (results.empty())
            {
                writer.sendError("vector RAG 没有检索到相关片段，请先导入更相关的知识");
                writer.sendDone();
                writer.close();
                return;
            }

            auto quotaCheck = quotaService.checkBeforeAI(userId);
            if (!quotaCheck.allowed)
            {
                writer.sendError(quotaCheck.message);
                writer.sendDone();
                writer.close();
                return;
            }

            writer.sendEvent("retrieved", json{{"contexts", vectorSearchResultsToJson(results)}}.dump());
            writer.sendStatus("正在基于 vector RAG 片段生成回答");

            std::string prompt = service.buildRagPrompt(question, results);
            std::string answer;
            try
            {
                aiCallStarted = true;
                answer = streamAiWithPrompt(prompt, modelType, [&writer](const std::string& chunk) {
                    if (!chunk.empty())
                    {
                        writer.sendMessage(chunk);
                    }
                });
            }
            catch (const std::exception& e)
            {
                writer.sendError(std::string("AI 流式调用失败，正在切换同步回答: ") + e.what());
            }

            if (answer.empty())
            {
                try
                {
                    aiCallStarted = true;
                    answer = callAiWithPrompt(prompt, modelType);
                    if (!answer.empty())
                    {
                        writer.sendMessage(answer);
                    }
                }
                catch (const std::exception& e)
                {
                    quotaService.logAIUsage(userId, "/chat/vector-rag-send-stream", modelType,
                        false, false, e.what());
                    quotaFinalized = true;
                    writer.sendError(std::string("AI 回答失败: ") + e.what());
                    writer.sendDone();
                    writer.close();
                    return;
                }
            }

            if (answer.empty())
            {
                quotaService.logAIUsage(userId, "/chat/vector-rag-send-stream", modelType,
                    false, false, "AI returned empty vector RAG answer");
                quotaFinalized = true;
                writer.sendError("AI 未返回可用回答");
            }
            else
            {
                quotaService.consumeQuotaOnSuccess(userId, "/chat/vector-rag-send-stream", modelType);
                quotaFinalized = true;
            }
            writer.sendDone();
        }
        catch (const std::exception& e)
        {
            if (aiCallStarted && !quotaFinalized)
            {
                quotaService.logAIUsage(userId, "/chat/vector-rag-send-stream", modelType,
                    false, false, e.what());
            }
            writer.sendError(std::string("Vector RAG 流式问答失败: ") + e.what());
            writer.sendDone();
        }
        writer.close();
    }).detach();
}

bool FitnessRagHandler::requireLogin(const http::HttpRequest& req, http::HttpResponse* resp, int& userId)
{
    auto session = server_->getSessionManager()->getSession(req, resp);
    if (session->getValue("isLoggedIn") != "true")
    {
        sendJson(req, resp, http::HttpResponse::k401Unauthorized, "Unauthorized",
            json{{"success", false}, {"message", "请先登录"}}, true);
        return false;
    }
    try
    {
        userId = std::stoi(session->getValue("userId"));
    }
    catch (const std::exception&)
    {
        sendJson(req, resp, http::HttpResponse::k401Unauthorized, "Unauthorized",
            json{{"success", false}, {"message", "登录状态异常，请重新登录"}}, true);
        return false;
    }
    return true;
}

void FitnessRagHandler::sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
    http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
    const json& body, bool close)
{
    std::string responseBody = body.dump(4);
    server_->packageResp(req.getVersion(), statusCode, statusMessage, close,
        "application/json", responseBody.size(), responseBody, resp);
}
