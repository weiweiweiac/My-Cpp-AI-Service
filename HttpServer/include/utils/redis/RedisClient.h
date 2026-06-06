#pragma once

#include <memory>
#include <optional>
#include <string>

namespace http
{
namespace redis
{

class RedisCommands
{
public:
    virtual ~RedisCommands() = default;

    virtual std::optional<std::string> get(const std::string& key) = 0;
    virtual bool set(const std::string& key, const std::string& value) = 0;
    virtual bool setex(const std::string& key, int seconds, const std::string& value) = 0;
    virtual bool del(const std::string& key) = 0;
    virtual long long incr(const std::string& key) = 0;
    virtual bool expire(const std::string& key, int seconds) = 0;
};

class RedisClient : public RedisCommands
{
public:
    RedisClient();
    RedisClient(const std::string& host, int port, const std::string& password = "");
    ~RedisClient() override;

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool isConnected() const;

    std::optional<std::string> get(const std::string& key) override;
    bool set(const std::string& key, const std::string& value) override;
    bool setex(const std::string& key, int seconds, const std::string& value) override;
    bool del(const std::string& key) override;
    long long incr(const std::string& key) override;
    bool expire(const std::string& key, int seconds) override;

private:
    void connect(const std::string& host, int port, const std::string& password);

    int socketFd_ { -1 };
};

std::shared_ptr<RedisClient> makeRedisClientFromEnv();

} // namespace redis
} // namespace http
