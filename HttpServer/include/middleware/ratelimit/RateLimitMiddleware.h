#pragma once

#include "../Middleware.h"
#include "../../utils/redis/RedisClient.h"

#include <memory>
#include <string>

namespace http
{
namespace middleware
{

class RateLimitMiddleware : public Middleware
{
public:
    explicit RateLimitMiddleware(std::shared_ptr<redis::RedisCommands> redis);

    void before(HttpRequest& request) override;
    void after(HttpResponse& response) override;

private:
    struct Policy
    {
        int limit;
        int windowSeconds;
        std::string scope;
    };

    bool policyForPath(const std::string& path, Policy& policy) const;
    std::string keyForRequest(const HttpRequest& request, const Policy& policy) const;
    std::string clientIp(const HttpRequest& request) const;
    std::string sessionId(const HttpRequest& request) const;
    void reject(const HttpRequest& request) const;

    std::shared_ptr<redis::RedisCommands> redis_;
};

} // namespace middleware
} // namespace http
