#include "../../../include/middleware/ratelimit/RateLimitMiddleware.h"

#include <muduo/base/Logging.h>

#include <utility>

namespace http
{
namespace middleware
{

RateLimitMiddleware::RateLimitMiddleware(std::shared_ptr<redis::RedisCommands> redis)
    : redis_(std::move(redis))
{
}

void RateLimitMiddleware::before(HttpRequest& request)
{
    Policy policy {};
    if (!policyForPath(request.path(), policy) || !redis_)
    {
        return;
    }

    const std::string key = keyForRequest(request, policy);
    const long long count = redis_->incr(key);
    if (count < 0)
    {
        LOG_WARN << "rate limit skipped because Redis INCR failed";
        return;
    }
    if (count == 1)
    {
        redis_->expire(key, policy.windowSeconds);
    }
    if (count > policy.limit)
    {
        reject(request);
    }
}

void RateLimitMiddleware::after(HttpResponse&)
{
}

bool RateLimitMiddleware::policyForPath(const std::string& path, Policy& policy) const
{
    if (path == "/ping")
    {
        return false;
    }
    if (path == "/login")
    {
        policy = {10, 60, "ip"};
        return true;
    }
    if (path == "/register")
    {
        policy = {5, 60, "ip"};
        return true;
    }
    if (path == "/chat/send" ||
        path == "/chat/send-stream" ||
        path == "/chat/vector-rag-send" ||
        path == "/chat/vector-rag-send-stream" ||
        path == "/chat/fitness-tool-send" ||
        path == "/chat/fitness-tool-send-stream")
    {
        policy = {20, 60, "session"};
        return true;
    }
    return false;
}

std::string RateLimitMiddleware::keyForRequest(const HttpRequest& request, const Policy& policy) const
{
    std::string identity;
    if (policy.scope == "ip")
    {
        identity = clientIp(request);
    }
    else
    {
        identity = sessionId(request);
        if (identity.empty())
        {
            identity = clientIp(request);
        }
    }
    return "ratelimit:" + request.path() + ":" + identity;
}

std::string RateLimitMiddleware::clientIp(const HttpRequest& request) const
{
    std::string ip = request.getHeader("X-Forwarded-For");
    if (!ip.empty())
    {
        const size_t comma = ip.find(',');
        if (comma != std::string::npos)
        {
            ip = ip.substr(0, comma);
        }
        return ip;
    }

    ip = request.getHeader("X-Real-IP");
    if (!ip.empty())
    {
        return ip;
    }

    return "unknown";
}

std::string RateLimitMiddleware::sessionId(const HttpRequest& request) const
{
    const std::string cookie = request.getHeader("Cookie");
    const std::string marker = "sessionId=";
    size_t pos = cookie.find(marker);
    if (pos == std::string::npos)
    {
        return "";
    }
    pos += marker.size();
    size_t end = cookie.find(';', pos);
    if (end == std::string::npos)
    {
        end = cookie.size();
    }
    return cookie.substr(pos, end - pos);
}

void RateLimitMiddleware::reject(const HttpRequest& request) const
{
    const std::string body = R"({"success":false,"message":"Too Many Requests"})";
    HttpResponse response(true);
    response.setStatusLine(request.getVersion(), HttpResponse::k429TooManyRequests, "Too Many Requests");
    response.setContentType("application/json");
    response.setContentLength(body.size());
    response.setBody(body);
    throw response;
}

} // namespace middleware
} // namespace http
