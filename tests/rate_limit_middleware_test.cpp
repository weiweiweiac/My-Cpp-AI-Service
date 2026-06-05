#include "HttpServer/include/middleware/ratelimit/RateLimitMiddleware.h"

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

http::HttpRequest requestFor(const std::string& path)
{
    http::HttpRequest request;
    request.setPath(path.data(), path.data() + path.size());
    request.setVersion("HTTP/1.1");
    return request;
}

void addHeader(http::HttpRequest& request, const std::string& line)
{
    const char* start = line.data();
    const char* colon = start + line.find(':');
    const char* end = start + line.size();
    request.addHeader(start, colon, end);
}

} // namespace

int main()
{
    auto redis = std::make_shared<FakeRedisClient>();
    http::middleware::RateLimitMiddleware limiter(redis);

    auto login = requestFor("/login");
    addHeader(login, "X-Forwarded-For: 10.0.0.7");
    for (int i = 0; i < 10; ++i)
    {
        limiter.before(login);
    }

    bool rejected = false;
    try
    {
        limiter.before(login);
    }
    catch (const http::HttpResponse& response)
    {
        rejected = true;
        require(response.getStatusCode() == http::HttpResponse::k429TooManyRequests,
            "over limit login should return 429");
    }
    require(rejected, "over limit login should throw a 429 response");

    auto ping = requestFor("/ping");
    for (int i = 0; i < 100; ++i)
    {
        limiter.before(ping);
    }
    require(redis->values.find("ratelimit:/ping") == redis->values.end(),
        "/ping should not be rate limited");

    return 0;
}
