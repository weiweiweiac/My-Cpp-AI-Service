#pragma once

#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "../../../../HttpServer/include/utils/MysqlUtil.h"
#include "../ChatServer.h"

class FitnessToolHandler : public http::router::RouterHandler
{
public:
    enum class Action
    {
        ListTools,
        CallTool,
        ChatToolSend
    };

    FitnessToolHandler(ChatServer* server, Action action)
        : server_(server), action_(action)
    {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    void handleListTools(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleCallTool(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleChatToolSend(const http::HttpRequest& req, http::HttpResponse* resp);
    bool requireLogin(const http::HttpRequest& req, http::HttpResponse* resp, int& userId);
    void sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
        http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
        const json& body, bool close);

    ChatServer* server_;
    Action action_;
    http::MysqlUtil mysqlUtil_;
};
