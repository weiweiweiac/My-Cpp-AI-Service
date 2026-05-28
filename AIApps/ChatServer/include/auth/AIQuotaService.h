#pragma once

#include "HttpServer/include/utils/JsonUtil.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace auth
{

inline const std::string kAIQuotaExhaustedMessage =
    "免费 AI 试用次数已用完，请联系管理员或后续配置自己的 API Key";

struct AIQuotaStatus
{
    int userId = 0;
    std::string username;
    std::string role = "user";
    int quotaTotal = 5;
    int quotaUsed = 0;
    int quotaRemaining = 5;
    bool isAdmin = false;
};

struct AIQuotaCheckResult
{
    bool allowed = false;
    bool isAdmin = false;
    bool systemError = false;
    std::string message;
    AIQuotaStatus status;
};

struct AIUsageRecord
{
    std::string endpoint;
    std::string modelType;
    bool quotaConsumed = false;
    bool success = true;
    std::string errorMessage;
    std::string createdAt;
};

inline std::string normalizeRole(std::string role)
{
    std::transform(role.begin(), role.end(), role.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return role == "admin" ? "admin" : "user";
}

inline AIQuotaStatus makeQuotaStatus(
    int userId,
    const std::string& username,
    const std::string& role,
    int quotaTotal,
    int quotaUsed)
{
    AIQuotaStatus status;
    status.userId = userId;
    status.username = username;
    status.role = normalizeRole(role);
    status.quotaTotal = std::max(0, quotaTotal);
    status.quotaUsed = std::max(0, quotaUsed);
    status.quotaRemaining = std::max(0, status.quotaTotal - status.quotaUsed);
    status.isAdmin = status.role == "admin";
    return status;
}

inline AIQuotaCheckResult checkQuotaStatus(const AIQuotaStatus& status)
{
    AIQuotaCheckResult result;
    result.status = status;
    result.isAdmin = status.isAdmin;

    if (status.isAdmin || status.quotaUsed < status.quotaTotal)
    {
        result.allowed = true;
        result.message = "allowed";
        return result;
    }

    result.allowed = false;
    result.message = kAIQuotaExhaustedMessage;
    return result;
}

inline bool applySuccessfulQuotaConsumption(AIQuotaStatus& status)
{
    if (status.isAdmin)
    {
        return false;
    }
    if (status.quotaUsed >= status.quotaTotal)
    {
        status.quotaRemaining = std::max(0, status.quotaTotal - status.quotaUsed);
        return false;
    }

    status.quotaUsed += 1;
    status.quotaRemaining = std::max(0, status.quotaTotal - status.quotaUsed);
    return true;
}

inline void recordFailedQuotaConsumption(AIQuotaStatus& status)
{
    status.quotaRemaining = std::max(0, status.quotaTotal - status.quotaUsed);
}

inline json quotaStatusToJson(const AIQuotaStatus& status)
{
    return json{
        {"userId", status.userId},
        {"username", status.username},
        {"role", status.role},
        {"aiQuotaTotal", status.quotaTotal},
        {"aiQuotaUsed", status.quotaUsed},
        {"aiQuotaRemaining", status.quotaRemaining},
        {"isAdmin", status.isAdmin}
    };
}

inline json usageRecordToJson(const AIUsageRecord& record)
{
    return json{
        {"endpoint", record.endpoint},
        {"modelType", record.modelType},
        {"quotaConsumed", record.quotaConsumed},
        {"success", record.success},
        {"errorMessage", record.errorMessage},
        {"createdAt", record.createdAt}
    };
}

class AIQuotaService
{
public:
    AIQuotaStatus getQuotaStatus(int userId);
    AIQuotaCheckResult checkBeforeAI(int userId);
    bool consumeQuotaOnSuccess(int userId, const std::string& endpoint, const std::string& modelType);
    bool logAIUsage(int userId, const std::string& endpoint, const std::string& modelType,
        bool quotaConsumed, bool success, const std::string& errorMessage);
    std::vector<AIUsageRecord> getRecentUsage(int userId, int limit = 20);
};

} // namespace auth
