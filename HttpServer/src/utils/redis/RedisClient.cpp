#include "../../../include/utils/redis/RedisClient.h"

#include <hiredis/hiredis.h>
#include <muduo/base/Logging.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace http
{
namespace redis
{

namespace
{

std::string envOrDefault(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return fallback;
    }
    return value;
}

struct ReplyDeleter
{
    void operator()(redisReply* reply) const
    {
        if (reply)
        {
            freeReplyObject(reply);
        }
    }
};

using ReplyPtr = std::unique_ptr<redisReply, ReplyDeleter>;

} // namespace

RedisClient::RedisClient()
{
    const std::string host = envOrDefault("REDIS_HOST", "127.0.0.1");
    const int port = std::stoi(envOrDefault("REDIS_PORT", "6379"));
    const std::string password = envOrDefault("REDIS_PASSWORD", "");
    connect(host, port, password);
}

RedisClient::RedisClient(const std::string& host, int port, const std::string& password)
{
    connect(host, port, password);
}

RedisClient::~RedisClient()
{
    if (context_)
    {
        redisFree(context_);
        context_ = nullptr;
    }
}

void RedisClient::connect(const std::string& host, int port, const std::string& password)
{
    context_ = redisConnect(host.c_str(), port);
    if (context_ == nullptr || context_->err)
    {
        LOG_ERROR << "Redis connection failed";
        if (context_)
        {
            redisFree(context_);
            context_ = nullptr;
        }
        return;
    }

    if (!password.empty())
    {
        ReplyPtr reply(static_cast<redisReply*>(
            redisCommand(context_, "AUTH %s", password.c_str())));
        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            LOG_ERROR << "Redis AUTH failed";
            redisFree(context_);
            context_ = nullptr;
            return;
        }
    }

    LOG_INFO << "Redis connection established";
}

bool RedisClient::isConnected() const
{
    return context_ != nullptr && context_->err == 0;
}

std::optional<std::string> RedisClient::get(const std::string& key)
{
    if (!isConnected())
    {
        return std::nullopt;
    }

    ReplyPtr reply(static_cast<redisReply*>(
        redisCommand(context_, "GET %s", key.c_str())));
    if (!reply || reply->type == REDIS_REPLY_NIL)
    {
        return std::nullopt;
    }
    if (reply->type != REDIS_REPLY_STRING)
    {
        return std::nullopt;
    }
    return std::string(reply->str, reply->len);
}

bool RedisClient::set(const std::string& key, const std::string& value)
{
    if (!isConnected())
    {
        return false;
    }
    ReplyPtr reply(static_cast<redisReply*>(
        redisCommand(context_, "SET %s %b", key.c_str(), value.data(), value.size())));
    return reply && reply->type != REDIS_REPLY_ERROR;
}

bool RedisClient::setex(const std::string& key, int seconds, const std::string& value)
{
    if (!isConnected())
    {
        return false;
    }
    ReplyPtr reply(static_cast<redisReply*>(
        redisCommand(context_, "SETEX %s %d %b", key.c_str(), seconds, value.data(), value.size())));
    return reply && reply->type != REDIS_REPLY_ERROR;
}

bool RedisClient::del(const std::string& key)
{
    if (!isConnected())
    {
        return false;
    }
    ReplyPtr reply(static_cast<redisReply*>(
        redisCommand(context_, "DEL %s", key.c_str())));
    return reply && reply->type != REDIS_REPLY_ERROR;
}

long long RedisClient::incr(const std::string& key)
{
    if (!isConnected())
    {
        return -1;
    }
    ReplyPtr reply(static_cast<redisReply*>(
        redisCommand(context_, "INCR %s", key.c_str())));
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        return -1;
    }
    return reply->integer;
}

bool RedisClient::expire(const std::string& key, int seconds)
{
    if (!isConnected())
    {
        return false;
    }
    ReplyPtr reply(static_cast<redisReply*>(
        redisCommand(context_, "EXPIRE %s %d", key.c_str(), seconds)));
    return reply && reply->type != REDIS_REPLY_ERROR;
}

std::shared_ptr<RedisClient> makeRedisClientFromEnv()
{
    return std::make_shared<RedisClient>();
}

} // namespace redis
} // namespace http
