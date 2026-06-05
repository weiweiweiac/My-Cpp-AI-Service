#include "../../include/session/RedisSessionStorage.h"

#include "../../include/utils/JsonUtil.h"

#include <muduo/base/Logging.h>

#include <unordered_map>
#include <utility>

namespace http
{
namespace session
{

RedisSessionStorage::RedisSessionStorage(
    std::shared_ptr<redis::RedisCommands> redis,
    int ttlSeconds)
    : redis_(std::move(redis))
    , ttlSeconds_(ttlSeconds)
{
}

std::string RedisSessionStorage::redisKey(const std::string& sessionId) const
{
    return "session:" + sessionId;
}

void RedisSessionStorage::save(std::shared_ptr<Session> session)
{
    if (!redis_ || !session)
    {
        return;
    }

    json body;
    body["id"] = session->getId();
    body["data"] = session->values();
    if (!redis_->setex(redisKey(session->getId()), ttlSeconds_, body.dump()))
    {
        LOG_WARN << "failed to save session to Redis";
    }
}

std::shared_ptr<Session> RedisSessionStorage::load(const std::string& sessionId)
{
    if (!redis_)
    {
        return nullptr;
    }

    auto raw = redis_->get(redisKey(sessionId));
    if (!raw)
    {
        return nullptr;
    }

    try
    {
        json body = json::parse(*raw);
        auto session = std::make_shared<Session>(sessionId, nullptr, ttlSeconds_);
        if (body.contains("data") && body["data"].is_object())
        {
            std::unordered_map<std::string, std::string> values;
            for (auto it = body["data"].begin(); it != body["data"].end(); ++it)
            {
                if (it.value().is_string())
                {
                    values[it.key()] = it.value().get<std::string>();
                }
            }
            session->replaceValues(values);
        }
        redis_->expire(redisKey(sessionId), ttlSeconds_);
        return session;
    }
    catch (const std::exception& e)
    {
        LOG_WARN << "failed to parse Redis session: " << e.what();
        redis_->del(redisKey(sessionId));
        return nullptr;
    }
}

void RedisSessionStorage::remove(const std::string& sessionId)
{
    if (redis_)
    {
        redis_->del(redisKey(sessionId));
    }
}

} // namespace session
} // namespace http
