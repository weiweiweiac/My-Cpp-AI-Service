#include"../include/AIUtil/AIHelper.h"
#include"../include/AIUtil/MQManager.h"
#include <stdexcept>
#include<chrono>

// 构造函数
struct AIHelper::StreamContext
{
    AIStrategy* strategy;
    StreamCallback onChunk;
    std::string pending;
    std::string fullText;
};

AIHelper::AIHelper() {
    //默认使用阿里云大模型
    strategy = StrategyFactory::instance().create("1");
}

void AIHelper::setStrategy(std::shared_ptr<AIStrategy> strat) {
    strategy = strat;
}


// 设置默认模型
//void AIHelper::setModel(const std::string& modelName) {
  //  model_ = modelName;
//}

// 添加一条用户消息
void AIHelper::addMessage(int userId,const std::string& userName, bool is_user,const std::string& userInput, std::string sessionId) {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    messages.push_back({ userInput,ms });
    //消息队列异步入库
    pushMessageToMysql(userId, userName, is_user, userInput, ms, sessionId);
}

void AIHelper::restoreMessage(const std::string& userInput,long long ms) {
    messages.push_back({ userInput,ms });
}


// 发送聊天消息
std::string AIHelper::chat(int userId,std::string userName, std::string sessionId, std::string userQuestion, std::string modelType) {

    //设置策略
    setStrategy(StrategyFactory::instance().create(modelType));

    
    if (false == strategy->isMCPModel) {

        addMessage(userId, userName, true, userQuestion, sessionId);
        json payload = strategy->buildRequest(this->messages);

        //执行请求
        json response = executeCurl(payload);
        std::string answer = strategy->parseResponse(response);
        addMessage(userId, userName, false, answer, sessionId);
        return answer.empty() ? "[Error] 无法解析响应" : answer;
    }
    //说明支持MCP
    AIConfig config;
    config.loadFromFile("../AIApps/ChatServer/resource/config.json");
    std::string tempUserQuestion =config.buildPrompt(userQuestion);
    std::cout << "tempUserQuestion is " << tempUserQuestion << std::endl;
    messages.push_back({ tempUserQuestion, 0 });

    json firstReq = strategy->buildRequest(this->messages);
    json firstResp = executeCurl(firstReq);
    std::string aiResult = strategy->parseResponse(firstResp);
    // 用完立即移除提示词
    messages.pop_back();

    std::cout << "aiResult is " << aiResult << std::endl;
    // 解析AI响应（是否工具调用）
    AIToolCall call = config.parseAIResponse(aiResult);

    // 情况1：AI 不调用工具
    if (!call.isToolCall) {
        addMessage(userId, userName, true, userQuestion, sessionId);
        addMessage(userId, userName, false, aiResult, sessionId);

        std::cout << "No tools required" << std::endl;
        return aiResult;
    }

    // 情况 2：AI 要调用工具
    json toolResult;
    AIToolRegistry registry;

    try {
        toolResult = registry.invoke(call.toolName, call.args);
        std::cout << "Tool call success" << std::endl;
    }
    catch (const std::exception& e) {
        //大多数情况都不会走这里
        std::string err = "[工具调用失败] " + std::string(e.what());
        addMessage(userId, userName, true, userQuestion, sessionId);
        addMessage(userId, userName, false, err, sessionId);

        std::cout << "Tool call failed" << std::endl << std::string(e.what());
        return err;
    }

    // 第二次调用AI
    // 用同样的 prompt_template，但说明工具执行过
    std::string secondPrompt = config.buildToolResultPrompt(userQuestion, call.toolName, call.args, toolResult);
    
    std::cout << "secondPrompt is " << secondPrompt << std::endl;
    messages.push_back({ secondPrompt, 0 });

    json secondReq = strategy->buildRequest(messages);
    json secondResp = executeCurl(secondReq);
    std::string finalAnswer = strategy->parseResponse(secondResp);
    //删除包含提示词的信息
    messages.pop_back();

    std::cout << "finalAnswer is " << finalAnswer << std::endl;

    addMessage(userId, userName, true, userQuestion, sessionId);
    addMessage(userId, userName, false, finalAnswer, sessionId);
    return finalAnswer;

}

// 发送自定义请求体
std::string AIHelper::chatStream(int userId, std::string userName, std::string sessionId,
    std::string userQuestion, std::string modelType, StreamCallback onChunk)
{
    setStrategy(StrategyFactory::instance().create(modelType));

    if (strategy->isMCPModel || modelType == "3")
    {
        std::string answer = chat(userId, userName, sessionId, userQuestion, modelType);
        if (onChunk && !answer.empty())
        {
            onChunk(answer);
        }
        return answer;
    }

    addMessage(userId, userName, true, userQuestion, sessionId);
    std::string answer;
    try
    {
        json payload = strategy->buildStreamRequest(this->messages);
        answer = executeCurlStream(payload, onChunk);
    }
    catch (const std::exception&)
    {
        json response = executeCurl(strategy->buildRequest(this->messages));
        answer = strategy->parseResponse(response);
        if (onChunk && !answer.empty())
        {
            onChunk(answer);
        }
    }
    if (!answer.empty())
    {
        addMessage(userId, userName, false, answer, sessionId);
    }
    return answer;
}

json AIHelper::request(const json& payload) {
    return executeCurl(payload);
}

std::string AIHelper::requestStream(const json& payload, StreamCallback onChunk) {
    return executeCurlStream(payload, std::move(onChunk));
}

std::vector<std::pair<std::string, long long>> AIHelper::GetMessages() {
    return this->messages;
}


// 内部方法：执行 curl 请求
json AIHelper::executeCurl(const json& payload) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string readBuffer;
    struct curl_slist* headers = nullptr;
    std::string authHeader = "Authorization: Bearer " + strategy->getApiKey();

    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string payloadStr = payload.dump();


    curl_easy_setopt(curl, CURLOPT_URL, strategy->getApiUrl().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
    }

    try {
        return json::parse(readBuffer);
    }
    catch (...) {
        throw std::runtime_error("Failed to parse JSON response: " + readBuffer);
    }
}

// curl 回调函数，把返回的数据写到 string buffer
std::string AIHelper::executeCurlStream(const json& payload, StreamCallback onChunk)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    StreamContext context;
    context.strategy = strategy.get();
    context.onChunk = std::move(onChunk);

    struct curl_slist* headers = nullptr;
    std::string authHeader = "Authorization: Bearer " + strategy->getApiKey();

    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    std::string payloadStr = payload.dump();

    curl_easy_setopt(curl, CURLOPT_URL, strategy->getApiUrl().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
    }

    if (!context.pending.empty())
    {
        try
        {
            std::string token = context.strategy->parseStreamChunk(context.pending);
            if (!token.empty())
            {
                context.fullText += token;
                if (context.onChunk)
                {
                    context.onChunk(token);
                }
            }
        }
        catch (...)
        {
        }
    }

    return context.fullText;
}

size_t AIHelper::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

size_t AIHelper::StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    StreamContext* context = static_cast<StreamContext*>(userp);
    context->pending.append(static_cast<char*>(contents), totalSize);

    size_t lineEnd = std::string::npos;
    while ((lineEnd = context->pending.find('\n')) != std::string::npos)
    {
        std::string line = context->pending.substr(0, lineEnd);
        context->pending.erase(0, lineEnd + 1);

        if (line.rfind("data:", 0) != 0)
        {
            continue;
        }

        try
        {
            std::string token = context->strategy->parseStreamChunk(line);
            if (!token.empty())
            {
                context->fullText += token;
                if (context->onChunk)
                {
                    context->onChunk(token);
                }
            }
        }
        catch (const std::exception&)
        {
        }
    }

    return totalSize;
}

void AIHelper::pushMessageToMysql(int userId, const std::string& userName, bool is_user, const std::string& userInput,long long ms, std::string sessionId) {
    json message;
    message["type"] = "insert_chat_message";
    message["userId"] = userId;
    message["username"] = userName;
    message["sessionId"] = sessionId;
    message["isUser"] = is_user;
    message["content"] = userInput;
    message["ts"] = ms;

    MQManager::instance().publish("sql_queue", message.dump());
}

