#pragma once

#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "../ChatServer.h"

class FitnessRagHandler : public http::router::RouterHandler
{
public:
    enum class Action
    {
        Index,
        Search,
        ChatRag
    };

    FitnessRagHandler(ChatServer* server, Action action)
        : server_(server), action_(action)
    {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    void handleIndex(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleSearch(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleChatRag(const http::HttpRequest& req, http::HttpResponse* resp);
    bool requireLogin(const http::HttpRequest& req, http::HttpResponse* resp, int& userId);
    void sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
        http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
        const json& body, bool close);

    ChatServer* server_;
    Action action_;
};
