#pragma once

#include "../../../../HttpServer/include/http/HttpStreamWriter.h"
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
        ChatRag,
        VectorIndex,
        VectorSearch,
        VectorChatRag
    };

    FitnessRagHandler(ChatServer* server, Action action)
        : server_(server), action_(action)
    {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;
    void handleChatRagStream(const http::HttpRequest& req, http::HttpStreamWriter& writer);
    void handleVectorChatRagStream(const http::HttpRequest& req, http::HttpStreamWriter& writer);

private:
    void handleIndex(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleSearch(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleChatRag(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleVectorIndex(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleVectorSearch(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleVectorChatRag(const http::HttpRequest& req, http::HttpResponse* resp);
    bool requireLogin(const http::HttpRequest& req, http::HttpResponse* resp, int& userId);
    void sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
        http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
        const json& body, bool close);

    ChatServer* server_;
    Action action_;
};
