#pragma once

#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../../HttpServer/include/utils/MysqlUtil.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "../ChatServer.h"

class FitnessProfileHandler : public http::router::RouterHandler
{
public:
    enum class Action
    {
        GetProfile,
        SaveProfile
    };

    FitnessProfileHandler(ChatServer* server, Action action)
        : server_(server), action_(action)
    {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    void handleGetProfile(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleSaveProfile(const http::HttpRequest& req, http::HttpResponse* resp);
    bool getLoggedInUserId(const http::HttpRequest& req, http::HttpResponse* resp, int& userId);
    void sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
        http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
        const json& body, bool close);

private:
    ChatServer* server_;
    Action action_;
    http::MysqlUtil mysqlUtil_;
};
