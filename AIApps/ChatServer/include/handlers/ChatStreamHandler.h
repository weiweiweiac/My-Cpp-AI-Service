#pragma once

#include "../../../../HttpServer/include/http/HttpRequest.h"
#include "../../../../HttpServer/include/http/HttpResponse.h"
#include "../../../../HttpServer/include/http/HttpStreamWriter.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "../ChatServer.h"

class ChatStreamHandler
{
public:
    explicit ChatStreamHandler(ChatServer* server) : server_(server) {}

    void handle(const http::HttpRequest& req, http::HttpStreamWriter& writer);

private:
    ChatServer* server_;
};
