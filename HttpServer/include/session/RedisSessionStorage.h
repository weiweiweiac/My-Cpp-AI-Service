#pragma once

#include "SessionStorage.h"
#include "../utils/redis/RedisClient.h"

#include <memory>
#include <string>

namespace http
{
namespace session
{

class RedisSessionStorage : public SessionStorage
{
public:
    explicit RedisSessionStorage(
        std::shared_ptr<redis::RedisCommands> redis,
        int ttlSeconds = 7 * 24 * 60 * 60);

    void save(std::shared_ptr<Session> session) override;
    std::shared_ptr<Session> load(const std::string& sessionId) override;
    void remove(const std::string& sessionId) override;

private:
    std::string redisKey(const std::string& sessionId) const;

    std::shared_ptr<redis::RedisCommands> redis_;
    int ttlSeconds_;
};

} // namespace session
} // namespace http
