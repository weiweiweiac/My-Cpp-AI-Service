#include "../include/handlers/ChatLoginHandler.h"
#include "../include/auth/LoginSessionPolicy.h"
#include "../include/auth/PasswordHasher.h"

void ChatLoginHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    
    auto contentType = req.getHeader("Content-Type");
    if (contentType.empty() || contentType != "application/json" || req.getBody().empty())
    {
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(0);
        resp->setBody("");
        return;
    }


    try
    {
        json parsed = json::parse(req.getBody());
        std::string username = parsed["username"];
        std::string password = parsed["password"];

        int userId = queryUserIdByPassword(username, password);
        if (userId != -1)
        {
            auto session = server_->getSessionManager()->getSession(req, resp);
            std::string oldSessionId;
            {
                std::lock_guard<std::mutex> lock(server_->mutexForOnlineUsers_);
                oldSessionId = auth::recordSuccessfulLogin(
                    server_->onlineUsers_,
                    server_->activeLoginSessionIds_,
                    userId,
                    session->getId());
            }

            if (!oldSessionId.empty())
            {
                server_->getSessionManager()->destroySession(oldSessionId);
            }

            session->setValue("userId", std::to_string(userId));
            session->setValue("username", username);
            session->setValue("isLoggedIn", "true");

            json successResp;
            successResp["success"] = true;
            successResp["userId"] = userId;
            std::string successBody = successResp.dump(4);

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(successBody.size());
            resp->setBody(successBody);
            return;
        }
        else 
        {
            json failureResp;
            failureResp["status"] = "error";
            failureResp["message"] = "Invalid username or password";
            std::string failureBody = failureResp.dump(4);

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k401Unauthorized, "Unauthorized");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(failureBody.size());
            resp->setBody(failureBody);
            return;
        }
    }
    catch (const std::exception& e)
    {
        json failureResp;
        failureResp["status"] = "error";
        failureResp["message"] = e.what();
        std::string failureBody = failureResp.dump(4);

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
        return;
    }

}

int ChatLoginHandler::queryUserIdByPassword(const std::string& username, const std::string& password)
{

    std::string sql = "SELECT id, password_hash, password_salt FROM users WHERE username = ? LIMIT 1";
    auto res = mysqlUtil_.executeQuery(sql, username);
    if (res->next())
    {
        const std::string passwordHash =
            res->isNull("password_hash") ? "" : res->getString("password_hash");
        const std::string passwordSalt =
            res->isNull("password_salt") ? "" : res->getString("password_salt");
        if (auth::PasswordHasher::verifyPassword(password, passwordSalt, passwordHash))
        {
            return res->getInt("id");
        }
    }

    return -1;
}

