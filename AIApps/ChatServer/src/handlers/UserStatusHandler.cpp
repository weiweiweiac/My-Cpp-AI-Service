#include "../../include/handlers/UserStatusHandler.h"

#include "../../include/auth/AIQuotaService.h"

#include <algorithm>

void UserStatusHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    switch (action_)
    {
    case Action::Me:
        handleMe(req, resp);
        break;
    case Action::AIUsage:
        handleAIUsage(req, resp);
        break;
    }
}

void UserStatusHandler::handleMe(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!requireLogin(req, resp, userId))
    {
        return;
    }

    try
    {
        auth::AIQuotaService quotaService;
        auto status = quotaService.getQuotaStatus(userId);
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"user", auth::quotaStatusToJson(status)}},
            false);
    }
    catch (const std::exception& e)
    {
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false}, {"message", std::string("查询用户状态失败: ") + e.what()}},
            true);
    }
}

void UserStatusHandler::handleAIUsage(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!requireLogin(req, resp, userId))
    {
        return;
    }

    int limit = 20;
    std::string limitText = req.getQueryParameters("limit");
    if (!limitText.empty())
    {
        try
        {
            limit = std::stoi(limitText);
        }
        catch (const std::exception&)
        {
            limit = 20;
        }
    }
    limit = std::max(1, std::min(limit, 50));

    try
    {
        auth::AIQuotaService quotaService;
        auto records = quotaService.getRecentUsage(userId, limit);
        json usage = json::array();
        for (const auto& record : records)
        {
            usage.push_back(auth::usageRecordToJson(record));
        }
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"usage", usage}},
            false);
    }
    catch (const std::exception& e)
    {
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false}, {"message", std::string("查询 AI 使用记录失败: ") + e.what()}},
            true);
    }
}

bool UserStatusHandler::requireLogin(const http::HttpRequest& req, http::HttpResponse* resp, int& userId)
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

void UserStatusHandler::sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
    http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
    const json& body, bool close)
{
    std::string responseBody = body.dump(4);
    server_->packageResp(req.getVersion(), statusCode, statusMessage, close,
        "application/json", responseBody.size(), responseBody, resp);
}
