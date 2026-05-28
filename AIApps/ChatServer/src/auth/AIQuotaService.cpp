#include "../../include/auth/AIQuotaService.h"

#include "HttpServer/include/utils/MysqlUtil.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace auth
{

namespace
{

std::string safeString(sql::ResultSet* res, const std::string& column)
{
    return res->isNull(column) ? "" : res->getString(column);
}

int safeInt(sql::ResultSet* res, const std::string& column, int fallback)
{
    return res->isNull(column) ? fallback : res->getInt(column);
}

int clampUsageLimit(int limit)
{
    if (limit <= 0)
    {
        return 20;
    }
    return std::min(limit, 50);
}

} // namespace

AIQuotaStatus AIQuotaService::getQuotaStatus(int userId)
{
    http::MysqlUtil mysqlUtil;
    std::string sql =
        "SELECT id, username, role, ai_quota_total, ai_quota_used "
        "FROM users WHERE id = ? LIMIT 1";
    auto res = mysqlUtil.executeQuery(sql, userId);
    if (!res->next())
    {
        throw std::runtime_error("user not found");
    }

    return makeQuotaStatus(
        res->getInt("id"),
        safeString(res, "username"),
        safeString(res, "role"),
        safeInt(res, "ai_quota_total", 5),
        safeInt(res, "ai_quota_used", 0));
}

AIQuotaCheckResult AIQuotaService::checkBeforeAI(int userId)
{
    try
    {
        return checkQuotaStatus(getQuotaStatus(userId));
    }
    catch (const std::exception& e)
    {
        AIQuotaCheckResult result;
        result.allowed = false;
        result.systemError = true;
        result.message = std::string("查询 AI 免费额度失败: ") + e.what();
        result.status.userId = userId;
        return result;
    }
}

bool AIQuotaService::consumeQuotaOnSuccess(
    int userId,
    const std::string& endpoint,
    const std::string& modelType)
{
    try
    {
        AIQuotaStatus status = getQuotaStatus(userId);
        if (status.isAdmin)
        {
            logAIUsage(userId, endpoint, modelType, false, true, "");
            return true;
        }

        http::MysqlUtil mysqlUtil;
        std::string sql =
            "UPDATE users SET ai_quota_used = ai_quota_used + 1 "
            "WHERE id = ? AND role <> 'admin' AND ai_quota_used < ai_quota_total";
        int updatedRows = mysqlUtil.executeUpdate(sql, userId);
        bool consumed = updatedRows > 0;
        logAIUsage(userId, endpoint, modelType, consumed, consumed,
            consumed ? "" : "quota update skipped");
        return consumed;
    }
    catch (const std::exception& e)
    {
        logAIUsage(userId, endpoint, modelType, false, false, e.what());
        std::cerr << "consume quota failed: " << e.what() << std::endl;
        return false;
    }
}

bool AIQuotaService::logAIUsage(
    int userId,
    const std::string& endpoint,
    const std::string& modelType,
    bool quotaConsumed,
    bool success,
    const std::string& errorMessage)
{
    try
    {
        http::MysqlUtil mysqlUtil;
        std::string sql =
            "INSERT INTO ai_usage_log "
            "(user_id, endpoint, model_type, quota_consumed, success, error_message) "
            "VALUES (?, ?, ?, ?, ?, ?)";
        mysqlUtil.executeUpdate(sql,
            userId,
            endpoint,
            modelType,
            quotaConsumed ? 1 : 0,
            success ? 1 : 0,
            errorMessage);
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "log AI usage failed: " << e.what() << std::endl;
        return false;
    }
}

std::vector<AIUsageRecord> AIQuotaService::getRecentUsage(int userId, int limit)
{
    int safeLimit = clampUsageLimit(limit);
    http::MysqlUtil mysqlUtil;
    std::string sql =
        "SELECT endpoint, model_type, quota_consumed, success, "
        "COALESCE(error_message, '') AS error_message, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
        "FROM ai_usage_log WHERE user_id = ? "
        "ORDER BY created_at DESC, id DESC LIMIT " + std::to_string(safeLimit);
    auto res = mysqlUtil.executeQuery(sql, userId);

    std::vector<AIUsageRecord> records;
    while (res->next())
    {
        AIUsageRecord record;
        record.endpoint = safeString(res, "endpoint");
        record.modelType = safeString(res, "model_type");
        record.quotaConsumed = safeInt(res, "quota_consumed", 0) != 0;
        record.success = safeInt(res, "success", 0) != 0;
        record.errorMessage = safeString(res, "error_message");
        record.createdAt = safeString(res, "created_at");
        records.push_back(record);
    }
    return records;
}

} // namespace auth
