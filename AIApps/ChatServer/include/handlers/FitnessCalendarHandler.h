#pragma once

#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../../HttpServer/include/http/HttpStreamWriter.h"
#include "../../../../HttpServer/include/utils/MysqlUtil.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "../ChatServer.h"

class FitnessCalendarHandler : public http::router::RouterHandler
{
public:
    enum class Action
    {
        GeneratePlan,
        ListCalendar,
        GetDay,
        SaveRecord,
        UpdateStatus,
        ListRecords
    };

    FitnessCalendarHandler(ChatServer* server, Action action)
        : server_(server), action_(action)
    {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;
    void handleGeneratePlanStream(const http::HttpRequest& req, http::HttpStreamWriter& writer);

private:
    void handleGeneratePlan(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleListCalendar(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleGetDay(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleSaveRecord(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleUpdateStatus(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleListRecords(const http::HttpRequest& req, http::HttpResponse* resp);

    bool getLoggedInUserId(const http::HttpRequest& req, http::HttpResponse* resp, int& userId);
    void sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
        http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
        const json& body, bool close);

private:
    ChatServer* server_;
    Action action_;
    http::MysqlUtil mysqlUtil_;
};
