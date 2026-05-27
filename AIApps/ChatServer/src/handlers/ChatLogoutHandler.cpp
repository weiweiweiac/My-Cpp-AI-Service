#include "../include/handlers/ChatLogoutHandler.h"
#include "../include/auth/LoginSessionPolicy.h"

void ChatLogoutHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try
    {
        auto session = server_->getSessionManager()->getSession(req, resp);
        const std::string sessionId = session->getId();
        const std::string userIdValue = session->getValue("userId");

        if (userIdValue.empty())
        {
            session->clear();
            server_->getSessionManager()->destroySession(sessionId);

            json response;
            response["success"] = true;
            response["message"] = "already logged out";
            std::string responseBody = response.dump(4);
            resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
            resp->setCloseConnection(true);
            resp->setContentType("application/json");
            resp->setContentLength(responseBody.size());
            resp->setBody(responseBody);
            return;
        }

        int userId = std::stoi(userIdValue);

        session->clear();
        server_->getSessionManager()->destroySession(sessionId);

        bool clearedActiveLogin = false;
        {
            std::lock_guard<std::mutex> lock(server_->mutexForOnlineUsers_);
            clearedActiveLogin = auth::recordLogout(
                server_->onlineUsers_,
                server_->activeLoginSessionIds_,
                userId,
                sessionId);
        }

        (void)clearedActiveLogin;


        json response;
        response["success"] = true;
        response["message"] = "logout successful";
        std::string responseBody = response.dump(4);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(responseBody.size());
        resp->setBody(responseBody);
    }
    catch (const std::exception& e)
    {

        json failureResp;
        failureResp["success"] = true;
        failureResp["message"] = std::string("already logged out: ") + e.what();
        std::string failureBody = failureResp.dump(4);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
    }
}
