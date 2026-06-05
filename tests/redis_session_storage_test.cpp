#include "HttpServer/include/session/RedisSessionStorage.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace
{

class FakeRedisClient : public http::redis::RedisCommands
{
public:
    std::optional<std::string> get(const std::string& key) override
    {
        auto it = values.find(key);
        if (it == values.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    bool set(const std::string& key, const std::string& value) override
    {
        values[key] = value;
        ttls.erase(key);
        return true;
    }

    bool setex(const std::string& key, int seconds, const std::string& value) override
    {
        values[key] = value;
        ttls[key] = seconds;
        return true;
    }

    bool del(const std::string& key) override
    {
        values.erase(key);
        ttls.erase(key);
        return true;
    }

    long long incr(const std::string& key) override
    {
        long long value = 1;
        auto current = get(key);
        if (current)
        {
            value = std::stoll(*current) + 1;
        }
        values[key] = std::to_string(value);
        return value;
    }

    bool expire(const std::string& key, int seconds) override
    {
        if (values.find(key) == values.end())
        {
            return false;
        }
        ttls[key] = seconds;
        return true;
    }

    std::unordered_map<std::string, std::string> values;
    std::unordered_map<std::string, int> ttls;
};

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

} // namespace

int main()
{
    auto redis = std::make_shared<FakeRedisClient>();
    http::session::RedisSessionStorage storage(redis, 604800);

    auto session = std::make_shared<http::session::Session>("abc123", nullptr);
    session->setValue("userId", "42");
    session->setValue("username", "alice");
    session->setValue("isLoggedIn", "true");

    storage.save(session);
    require(redis->values.find("session:abc123") != redis->values.end(),
        "session should be written under session:{id}");
    require(redis->ttls["session:abc123"] == 604800,
        "session should be written with configured TTL");

    auto loaded = storage.load("abc123");
    require(loaded != nullptr, "saved session should load from redis");
    require(loaded->getValue("userId") == "42", "loaded session should preserve userId");
    require(loaded->getValue("username") == "alice", "loaded session should preserve username");
    require(loaded->getValue("isLoggedIn") == "true", "loaded session should preserve login flag");

    storage.remove("abc123");
    require(redis->values.find("session:abc123") == redis->values.end(),
        "removed session should be deleted from redis");

    return 0;
}
