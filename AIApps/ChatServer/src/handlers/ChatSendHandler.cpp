#include "../include/handlers/ChatSendHandler.h"

#include "AIApps/ChatServer/include/auth/AIQuotaService.h"


void ChatSendHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    std::string modelType = "1";
    bool aiCallStarted = false;
    bool quotaFinalized = false;
    const std::string endpoint = "/chat/send";

    try
    {

        auto session = server_->getSessionManager()->getSession(req, resp);
        LOG_INFO << "session->getValue(\"isLoggedIn\") = " << session->getValue("isLoggedIn");
        if (session->getValue("isLoggedIn") != "true")
        {

            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "Unauthorized";
            std::string errorBody = errorResp.dump(4);

            server_->packageResp(req.getVersion(), http::HttpResponse::k401Unauthorized,
                "Unauthorized", true, "application/json", errorBody.size(),
                errorBody, resp);
            return;
        }


        userId = std::stoi(session->getValue("userId"));
        std::string username = session->getValue("username");

        std::string userQuestion;
        std::string sessionId;

        auto body = req.getBody();
        if (!body.empty()) {
            auto j = json::parse(body);
            if (j.contains("question")) userQuestion = j["question"];
            if (j.contains("sessionId")) sessionId = j["sessionId"];

            modelType = j.contains("modelType") ? j["modelType"].get<std::string>() : "1";
        }

        auth::AIQuotaService quotaService;
        auto quotaCheck = quotaService.checkBeforeAI(userId);
        if (!quotaCheck.allowed)
        {
            json errorResp;
            errorResp["success"] = false;
            errorResp["message"] = quotaCheck.message;
            std::string errorBody = errorResp.dump(4);
            server_->packageResp(req.getVersion(),
                quotaCheck.systemError ? http::HttpResponse::k500InternalServerError : http::HttpResponse::k403Forbidden,
                quotaCheck.systemError ? "Internal Server Error" : "Forbidden",
                true, "application/json", errorBody.size(), errorBody, resp);
            return;
        }


        std::shared_ptr<AIHelper> AIHelperPtr;
        {
            std::lock_guard<std::mutex> lock(server_->mutexForChatInformation);

            auto& userSessions = server_->chatInformation[userId];

            if (userSessions.find(sessionId) == userSessions.end()) {

                userSessions.emplace( 
                    sessionId,
                    std::make_shared<AIHelper>()
                );
            }
            AIHelperPtr= userSessions[sessionId];
        }
        

        aiCallStarted = true;
        std::string aiInformation=AIHelperPtr->chat(userId, username,sessionId, userQuestion, modelType);
        if (aiInformation.empty())
        {
            quotaService.logAIUsage(userId, endpoint, modelType,
                false, false, "AI returned empty response");
            quotaFinalized = true;
        }
        else
        {
            quotaService.consumeQuotaOnSuccess(userId, endpoint, modelType);
            quotaFinalized = true;
        }
        json successResp;
        successResp["success"] = true;
        successResp["Information"] = aiInformation;
        std::string successBody = successResp.dump(4);

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("application/json");
        resp->setContentLength(successBody.size());
        resp->setBody(successBody);
        return;
    }
    catch (const std::exception& e)
    {
        if (aiCallStarted && !quotaFinalized)
        {
            auth::AIQuotaService quotaService;
            quotaService.logAIUsage(userId, endpoint, modelType, false, false, e.what());
        }

        json failureResp;
        failureResp["status"] = "error";
        failureResp["message"] = e.what();
        std::string failureBody = failureResp.dump(4);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
    }
}









