#include "../../include/handlers/FitnessRagHandler.h"

#include "../../include/AIUtil/AIHelper.h"
#include "../../include/AIUtil/AIFactory.h"
#include "../../include/rag/FitnessRagService.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
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

std::string callAiWithPrompt(const std::string& prompt, const std::string& modelType)
{
    auto strategy = StrategyFactory::instance().create(normalizeModelType(modelType));
    AIHelper helper;
    helper.setStrategy(strategy);
    std::vector<std::pair<std::string, long long>> messages = {{ prompt, 0 }};
    json response = helper.request(strategy->buildRequest(messages));
    return strategy->parseResponse(response);
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
    (void)userId;

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

    try
    {
        std::string prompt = service.buildRagPrompt(question, results);
        std::string answer = callAiWithPrompt(prompt, jsonString(requestBody, "modelType"));
        if (answer.empty())
        {
            answer = "AI 未返回可用回答";
        }
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"answer", answer}, {"retrievedChunks", searchResultsToJson(results)}}, false);
    }
    catch (const std::exception& e)
    {
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false}, {"message", std::string("RAG 问答失败: ") + e.what()},
                 {"retrievedChunks", searchResultsToJson(results)}},
            true);
    }
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
