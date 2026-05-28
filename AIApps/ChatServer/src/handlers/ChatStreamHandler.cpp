#include "../include/handlers/ChatStreamHandler.h"

#include "AIApps/ChatServer/include/auth/AIQuotaService.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>

void ChatStreamHandler::handle(const http::HttpRequest& req, http::HttpStreamWriter& writer)
{
    try
    {
        http::HttpResponse sessionResp(false);
        auto session = server_->getSessionManager()->getSession(req, &sessionResp);
        if (session->getValue("isLoggedIn") != "true")
        {
            writer.sendSseHeader();
            writer.sendError("Unauthorized");
            writer.sendDone();
            writer.close();
            return;
        }

        std::string userQuestion;
        std::string modelType = "1";
        std::string sessionId;

        auto body = req.getBody();
        if (!body.empty())
        {
            auto j = json::parse(body);
            if (j.contains("question")) userQuestion = j["question"].get<std::string>();
            if (j.contains("sessionId")) sessionId = j["sessionId"].get<std::string>();
            if (j.contains("modelType")) modelType = j["modelType"].get<std::string>();
        }

        if (userQuestion.empty())
        {
            writer.sendSseHeader();
            writer.sendError("question is required");
            writer.sendDone();
            writer.close();
            return;
        }

        if (sessionId.empty())
        {
            writer.sendSseHeader();
            writer.sendError("sessionId is required for streaming chat");
            writer.sendDone();
            writer.close();
            return;
        }

        int userId = std::stoi(session->getValue("userId"));
        std::string username = session->getValue("username");

        auth::AIQuotaService quotaService;
        auto quotaCheck = quotaService.checkBeforeAI(userId);
        if (!quotaCheck.allowed)
        {
            writer.sendSseHeader();
            writer.sendError(quotaCheck.message);
            writer.sendDone();
            writer.close();
            return;
        }

        std::shared_ptr<AIHelper> helper;
        {
            std::lock_guard<std::mutex> lock(server_->mutexForChatInformation);
            auto& userSessions = server_->chatInformation[userId];
            if (userSessions.find(sessionId) == userSessions.end())
            {
                userSessions.emplace(sessionId, std::make_shared<AIHelper>());
            }
            helper = userSessions[sessionId];
        }

        writer.sendSseHeader();
        writer.sendStatus("accepted");

        std::thread([helper, writer, userId, username, sessionId, userQuestion, modelType]() mutable {
            auth::AIQuotaService quotaService;
            bool quotaFinalized = false;
            try
            {
                writer.sendStatus("calling_ai");
                std::string fullText = helper->chatStream(
                    userId,
                    username,
                    sessionId,
                    userQuestion,
                    modelType,
                    [&writer](const std::string& chunk) {
                        writer.sendMessage(chunk);
                    });

                if (fullText.empty())
                {
                    quotaService.logAIUsage(userId, "/chat/send-stream", modelType,
                        false, false, "AI returned empty response");
                    writer.sendError("AI returned empty response");
                }
                else
                {
                    quotaService.consumeQuotaOnSuccess(userId, "/chat/send-stream", modelType);
                    quotaFinalized = true;
                }
                writer.sendDone();
            }
            catch (const std::exception& e)
            {
                if (!quotaFinalized)
                {
                    quotaService.logAIUsage(userId, "/chat/send-stream", modelType,
                        false, false, e.what());
                }
                writer.sendError(std::string("stream failed: ") + e.what());
                writer.sendDone();
            }
            writer.close();
        }).detach();
    }
    catch (const std::exception& e)
    {
        writer.sendSseHeader();
        writer.sendError(e.what());
        writer.sendDone();
        writer.close();
    }
}
