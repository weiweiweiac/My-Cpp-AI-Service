#pragma once

#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "../../../../HttpServer/include/utils/MysqlUtil.h"
#include "../ChatServer.h"
#include "../fitness/ExerciseLibraryPolicy.h"

class ExerciseLibraryHandler : public http::router::RouterHandler
{
public:
    enum class Action
    {
        List,
        Create,
        Update,
        Delete
    };

    ExerciseLibraryHandler(ChatServer* server, Action action)
        : server_(server), action_(action)
    {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    void handleList(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleCreate(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleUpdate(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleDelete(const http::HttpRequest& req, http::HttpResponse* resp);

    bool requireLogin(const http::HttpRequest& req, http::HttpResponse* resp, int& userId);
    void sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
        http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
        const json& body, bool close);

private:
    ChatServer* server_;
    Action action_;
    http::MysqlUtil mysqlUtil_;
};
