#include "AIApps/ChatServer/include/auth/AIQuotaService.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

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
    auto user = auth::makeQuotaStatus(1, "demo", "user", 5, 2);
    require(!user.isAdmin, "normal user should not be admin");
    require(user.quotaRemaining == 3, "normal user should have remaining quota");
    require(auth::checkQuotaStatus(user).allowed, "normal user with quota should be allowed");

    auto exhausted = auth::makeQuotaStatus(2, "limited", "user", 5, 5);
    auto exhaustedCheck = auth::checkQuotaStatus(exhausted);
    require(!exhaustedCheck.allowed, "normal user with exhausted quota should be denied");
    require(exhaustedCheck.message == auth::kAIQuotaExhaustedMessage,
        "exhausted quota should return the product message");

    auto admin = auth::makeQuotaStatus(3, "root", "admin", 1, 999);
    auto adminCheck = auth::checkQuotaStatus(admin);
    require(admin.isAdmin, "admin status should be recognized");
    require(adminCheck.allowed, "admin should ignore quota limits");

    auth::AIQuotaStatus consumed = user;
    require(auth::applySuccessfulQuotaConsumption(consumed), "successful normal user call should consume quota");
    require(consumed.quotaUsed == 3, "successful normal user call should increment used quota");
    require(consumed.quotaRemaining == 2, "successful normal user call should update remaining quota");

    auth::AIQuotaStatus failed = user;
    auth::recordFailedQuotaConsumption(failed);
    require(failed.quotaUsed == 2, "failed AI call should not consume quota");
    require(failed.quotaRemaining == 3, "failed AI call should leave remaining quota unchanged");

    auth::AIQuotaStatus adminConsumed = admin;
    require(!auth::applySuccessfulQuotaConsumption(adminConsumed),
        "successful admin call should not consume quota");
    require(adminConsumed.quotaUsed == 999, "admin quota usage should not change");

    auth::AIUsageRecord record;
    record.endpoint = "/chat/rag-send";
    record.modelType = "1";
    record.quotaConsumed = true;
    record.success = true;
    record.errorMessage = "";
    record.createdAt = "2026-05-28 12:00:00";

    json row = auth::usageRecordToJson(record);
    require(row["endpoint"] == "/chat/rag-send", "usage record should include endpoint");
    require(row["modelType"] == "1", "usage record should include model type");
    require(row["quotaConsumed"] == true, "usage record should include quota flag");
    require(row["success"] == true, "usage record should include success flag");
    require(row["errorMessage"] == "", "usage record should include error message");
    require(row["createdAt"] == "2026-05-28 12:00:00", "usage record should include created time");

    return 0;
}
