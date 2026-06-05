#include "../include/handlers/ChatLoginHandler.h"
#include "../include/handlers/ChatRegisterHandler.h"
#include "../include/handlers/ChatLogoutHandler.h"
#include"../include/handlers/ChatHandler.h"
#include"../include/handlers/ChatEntryHandler.h"
#include"../include/handlers/ChatSendHandler.h"
#include"../include/handlers/ChatStreamHandler.h"
#include"../include/handlers/AIMenuHandler.h"
#include"../include/handlers/AIUploadSendHandler.h"
#include"../include/handlers/AIUploadHandler.h"
#include"../include/handlers/ChatHistoryHandler.h"


#include"../include/handlers/ChatCreateAndSendHandler.h"
#include"../include/handlers/ChatSessionsHandler.h"
#include"../include/handlers/ChatSpeechHandler.h"
#include"../include/handlers/FitnessProfileHandler.h"
#include"../include/handlers/FitnessCalendarHandler.h"
#include"../include/handlers/FitnessRagHandler.h"
#include"../include/handlers/FitnessToolHandler.h"
#include"../include/handlers/ExerciseLibraryHandler.h"
#include"../include/handlers/UserStatusHandler.h"

#include "../include/ChatServer.h"
#include "../../../HttpServer/include/http/HttpRequest.h"
#include "../../../HttpServer/include/http/HttpResponse.h"
#include "../../../HttpServer/include/http/HttpServer.h"
#include "../../../HttpServer/include/middleware/ratelimit/RateLimitMiddleware.h"
#include "../../../HttpServer/include/session/RedisSessionStorage.h"

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>



using namespace http;

namespace
{

std::string envOrDefault(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty())
    {
        return fallback;
    }
    return value;
}

bool isRedisSessionEnabled()
{
    std::string mode = envOrDefault("SESSION_STORAGE", "");
    if (mode == "redis" || mode == "Redis" || mode == "REDIS")
    {
        return true;
    }
    std::string enabled = envOrDefault("REDIS_SESSION_ENABLED", "");
    return enabled == "1" || enabled == "true" || enabled == "TRUE";
}

} // namespace


ChatServer::ChatServer(int port,
    const std::string& name,
    muduo::net::TcpServer::Option option)
    : httpServer_(port, name, option)
{
    initialize();
}

void ChatServer::initialize() {
    std::cout << "ChatServer initialize start  ! " << std::endl;
    const std::string mysqlHost = envOrDefault("MYSQL_HOST", "127.0.0.1");
    const std::string mysqlPort = envOrDefault("MYSQL_PORT", "3306");
    const std::string mysqlUser = envOrDefault("MYSQL_USER", "root");
    const std::string mysqlPassword = envOrDefault("MYSQL_PASSWORD", "123456");
    const std::string mysqlDatabase = envOrDefault("MYSQL_DATABASE", "ChatHttpServer");
	http::MysqlUtil::init("tcp://" + mysqlHost + ":" + mysqlPort, mysqlUser, mysqlPassword, mysqlDatabase, 5);

    initializeSession();

    initializeMiddleware();

    initializeRouter();
}

void ChatServer::initChatMessage() {

    std::cout << "initChatMessage start ! " << std::endl;
    readDataFromMySQL();
    std::cout << "initChatMessage success ! " << std::endl;
}

void ChatServer::readDataFromMySQL() {


    std::string sql = "SELECT id, username,session_id, is_user, content, ts FROM chat_message ORDER BY ts ASC, id ASC";

    sql::ResultSet* res;
    try {
        res = mysqlUtil_.executeQuery(sql);
    }
    catch (const std::exception& e) {
        std::cerr << "MySQL query failed: " << e.what() << std::endl;
        return;
    }

    while (res->next()) {
        long long user_id = 0;
        std::string session_id ;  
        std::string username, content;
        long long ts = 0;
        int is_user = 1;

        try {
            user_id    = res->getInt64("id");       
            session_id = res->getString("session_id");  
            username   = res->getString("username");
            content    = res->getString("content");
            ts         = res->getInt64("ts");
            is_user    = res->getInt("is_user");
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to read row: " << e.what() << std::endl;
            continue; 
        }

        auto& userSessions = chatInformation[user_id];

        std::shared_ptr<AIHelper> helper;
        auto itSession = userSessions.find(session_id);
        if (itSession == userSessions.end()) {
            helper = std::make_shared<AIHelper>();
            userSessions[session_id] = helper;
			sessionsIdsMap[user_id].push_back(session_id);
        } else {
            helper = itSession->second;
        }

        helper->restoreMessage(content, ts);
    }

    std::cout << "readDataFromMySQL finished" << std::endl;
}



void ChatServer::setThreadNum(int numThreads) {
    httpServer_.setThreadNum(numThreads);
}


void ChatServer::start() {
    httpServer_.start();
}


void ChatServer::initializeRouter() {

    httpServer_.Get("/ping", [](const http::HttpRequest& req, http::HttpResponse* resp) {
        const std::string body = "pong";
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setContentType("text/plain");
        resp->setContentLength(body.size());
        resp->setBody(body);
    });

    httpServer_.Post("/echo", [](const http::HttpRequest& req, http::HttpResponse* resp) {
        const std::string body = req.getBody();
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setContentType("text/plain");
        resp->setContentLength(body.size());
        resp->setBody(body);
    });

    httpServer_.GetStream("/stream/mock", [](const http::HttpRequest& req, http::HttpStreamWriter& writer) {
        writer.sendSseHeader();
        writer.sendStatus("mock_started");
        std::thread([writer]() mutable {
            const std::vector<std::string> chunks = {
                "Hello, I am your AI fitness coach.",
                "Analyzing your training goal.",
                "Generating a practical suggestion."
            };
            for (const auto& chunk : chunks)
            {
                writer.sendMessage(chunk);
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
            }
            writer.sendDone();
            writer.close();
        }).detach();
    });

    httpServer_.Get("/fitness/profile",
        std::make_shared<FitnessProfileHandler>(this, FitnessProfileHandler::Action::GetProfile));
    httpServer_.Post("/fitness/profile/save",
        std::make_shared<FitnessProfileHandler>(this, FitnessProfileHandler::Action::SaveProfile));
    httpServer_.Post("/fitness/calendar/generate-plan",
        std::make_shared<FitnessCalendarHandler>(this, FitnessCalendarHandler::Action::GeneratePlan));
    auto fitnessCalendarStreamHandler =
        std::make_shared<FitnessCalendarHandler>(this, FitnessCalendarHandler::Action::GeneratePlan);
    httpServer_.PostStream("/fitness/calendar/generate-plan-stream",
        [fitnessCalendarStreamHandler](const http::HttpRequest& req, http::HttpStreamWriter& writer) {
            fitnessCalendarStreamHandler->handleGeneratePlanStream(req, writer);
        });
    httpServer_.Get("/fitness/calendar/list",
        std::make_shared<FitnessCalendarHandler>(this, FitnessCalendarHandler::Action::ListCalendar));
    httpServer_.Get("/fitness/calendar/day",
        std::make_shared<FitnessCalendarHandler>(this, FitnessCalendarHandler::Action::GetDay));
    httpServer_.Post("/fitness/calendar/record/save",
        std::make_shared<FitnessCalendarHandler>(this, FitnessCalendarHandler::Action::SaveRecord));
    httpServer_.Post("/fitness/calendar/status/update",
        std::make_shared<FitnessCalendarHandler>(this, FitnessCalendarHandler::Action::UpdateStatus));
    httpServer_.Get("/fitness/calendar/record/list",
        std::make_shared<FitnessCalendarHandler>(this, FitnessCalendarHandler::Action::ListRecords));

    httpServer_.Get("/fitness/exercises/list",
        std::make_shared<ExerciseLibraryHandler>(this, ExerciseLibraryHandler::Action::List));
    httpServer_.Post("/fitness/exercises/create",
        std::make_shared<ExerciseLibraryHandler>(this, ExerciseLibraryHandler::Action::Create));
    httpServer_.Post("/fitness/exercises/update",
        std::make_shared<ExerciseLibraryHandler>(this, ExerciseLibraryHandler::Action::Update));
    httpServer_.Post("/fitness/exercises/delete",
        std::make_shared<ExerciseLibraryHandler>(this, ExerciseLibraryHandler::Action::Delete));

    httpServer_.Post("/rag/index",
        std::make_shared<FitnessRagHandler>(this, FitnessRagHandler::Action::Index));
    httpServer_.Post("/rag/search",
        std::make_shared<FitnessRagHandler>(this, FitnessRagHandler::Action::Search));
    httpServer_.Post("/rag/vector/index",
        std::make_shared<FitnessRagHandler>(this, FitnessRagHandler::Action::VectorIndex));
    httpServer_.Get("/rag/vector/search",
        std::make_shared<FitnessRagHandler>(this, FitnessRagHandler::Action::VectorSearch));
    httpServer_.Post("/chat/rag-send",
        std::make_shared<FitnessRagHandler>(this, FitnessRagHandler::Action::ChatRag));
    auto fitnessRagStreamHandler =
        std::make_shared<FitnessRagHandler>(this, FitnessRagHandler::Action::ChatRag);
    httpServer_.PostStream("/chat/rag-send-stream",
        [fitnessRagStreamHandler](const http::HttpRequest& req, http::HttpStreamWriter& writer) {
            fitnessRagStreamHandler->handleChatRagStream(req, writer);
        });
    httpServer_.Post("/chat/vector-rag-send",
        std::make_shared<FitnessRagHandler>(this, FitnessRagHandler::Action::VectorChatRag));
    auto vectorRagStreamHandler =
        std::make_shared<FitnessRagHandler>(this, FitnessRagHandler::Action::VectorChatRag);
    httpServer_.PostStream("/chat/vector-rag-send-stream",
        [vectorRagStreamHandler](const http::HttpRequest& req, http::HttpStreamWriter& writer) {
            vectorRagStreamHandler->handleVectorChatRagStream(req, writer);
        });

    httpServer_.Get("/fitness/tools/list",
        std::make_shared<FitnessToolHandler>(this, FitnessToolHandler::Action::ListTools));
    httpServer_.Post("/fitness/tool/call",
        std::make_shared<FitnessToolHandler>(this, FitnessToolHandler::Action::CallTool));
    httpServer_.Post("/chat/fitness-tool-send",
        std::make_shared<FitnessToolHandler>(this, FitnessToolHandler::Action::ChatToolSend));
    auto fitnessToolStreamHandler =
        std::make_shared<FitnessToolHandler>(this, FitnessToolHandler::Action::ChatToolSend);
    httpServer_.PostStream("/chat/fitness-tool-send-stream",
        [fitnessToolStreamHandler](const http::HttpRequest& req, http::HttpStreamWriter& writer) {
            fitnessToolStreamHandler->handleChatToolSendStream(req, writer);
        });

    httpServer_.Get("/", std::make_shared<ChatEntryHandler>(this));
    httpServer_.Get("/entry", std::make_shared<ChatEntryHandler>(this));
    
    httpServer_.Post("/login", std::make_shared<ChatLoginHandler>(this));
    
    httpServer_.Post("/register", std::make_shared<ChatRegisterHandler>(this));
    
    httpServer_.Post("/user/logout", std::make_shared<ChatLogoutHandler>(this));

    httpServer_.Get("/user/me",
        std::make_shared<UserStatusHandler>(this, UserStatusHandler::Action::Me));
    httpServer_.Get("/user/ai-usage",
        std::make_shared<UserStatusHandler>(this, UserStatusHandler::Action::AIUsage));

    httpServer_.Get("/chat", std::make_shared<ChatHandler>(this));

    httpServer_.Post("/chat/send", std::make_shared<ChatSendHandler>(this));
    auto chatStreamHandler = std::make_shared<ChatStreamHandler>(this);
    httpServer_.PostStream("/chat/send-stream",
        [chatStreamHandler](const http::HttpRequest& req, http::HttpStreamWriter& writer) {
            chatStreamHandler->handle(req, writer);
        });
 
    httpServer_.Get("/menu", std::make_shared<AIMenuHandler>(this));
    
    httpServer_.Get("/upload", std::make_shared<AIUploadHandler>(this));
   
    httpServer_.Post("/upload/send", std::make_shared<AIUploadSendHandler>(this));
    
    httpServer_.Post("/chat/history", std::make_shared<ChatHistoryHandler>(this));

    
    httpServer_.Post("/chat/send-new-session", std::make_shared<ChatCreateAndSendHandler>(this));
    httpServer_.Get("/chat/sessions", std::make_shared<ChatSessionsHandler>(this));

    httpServer_.Post("/chat/tts", std::make_shared<ChatSpeechHandler>(this));
}

void ChatServer::initializeSession() {

    std::unique_ptr<http::session::SessionStorage> sessionStorage;
    if (isRedisSessionEnabled())
    {
        redisClient_ = http::redis::makeRedisClientFromEnv();
        if (redisClient_ && redisClient_->isConnected())
        {
            sessionStorage = std::make_unique<http::session::RedisSessionStorage>(redisClient_);
        }
        else
        {
            LOG_WARN << "Redis session requested but Redis is unavailable; using memory session storage";
        }
    }

    if (!sessionStorage)
    {
        sessionStorage = std::make_unique<http::session::MemorySessionStorage>();
    }

    auto sessionManager = std::make_unique<http::session::SessionManager>(std::move(sessionStorage));

    setSessionManager(std::move(sessionManager));
}

void ChatServer::initializeMiddleware() {

    auto corsMiddleware = std::make_shared<http::middleware::CorsMiddleware>();

    httpServer_.addMiddleware(corsMiddleware);

    if (!redisClient_)
    {
        redisClient_ = http::redis::makeRedisClientFromEnv();
    }
    if (redisClient_ && redisClient_->isConnected())
    {
        auto rateLimitMiddleware =
            std::make_shared<http::middleware::RateLimitMiddleware>(redisClient_);
        httpServer_.addMiddleware(rateLimitMiddleware);
    }
    else
    {
        LOG_WARN << "Redis unavailable; rate limiting middleware disabled";
    }
}


void ChatServer::packageResp(const std::string& version,
    http::HttpResponse::HttpStatusCode statusCode,
    const std::string& statusMsg,
    bool close,
    const std::string& contentType,
    int contentLen,
    const std::string& body,
    http::HttpResponse* resp)
{
    if (resp == nullptr)
    {
        LOG_ERROR << "Response pointer is null";
        return;
    }

    try
    {
        resp->setVersion(version);
        resp->setStatusCode(statusCode);
        resp->setStatusMessage(statusMsg);
        resp->setCloseConnection(close);
        resp->setContentType(contentType);
        resp->setContentLength(contentLen);
        resp->setBody(body);

        LOG_INFO << "Response packaged successfully";
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "Error in packageResp: " << e.what();

        resp->setStatusCode(http::HttpResponse::k500InternalServerError);
        resp->setStatusMessage("Internal Server Error");
        resp->setCloseConnection(true);
    }
}
