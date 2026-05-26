#pragma once

#include "../../../../HttpServer/include/utils/JsonUtil.h"

#include <string>
#include <vector>

namespace tools
{

struct FitnessToolDefinition
{
    std::string name;
    std::string description;
    json inputSchema;
    bool requiresUserData { false };
};

class FitnessDataProvider
{
public:
    virtual ~FitnessDataProvider() = default;

    virtual json loadProfile(int userId) = 0;
    virtual json getTodayTrainingPlan(int userId, const json& args) = 0;
    virtual json getWeekTrainingCalendar(int userId, const json& args) = 0;
    virtual json getRecentTrainingRecords(int userId, const json& args) = 0;
    virtual json saveTrainingRecord(int userId, const json& args) = 0;
};

class FitnessToolService
{
public:
    explicit FitnessToolService(FitnessDataProvider* dataProvider = nullptr);

    std::vector<FitnessToolDefinition> listTools() const;
    bool hasTool(const std::string& name) const;
    json callTool(const std::string& name, const json& args, int userId);
    std::string matchToolName(const std::string& question) const;
    std::string buildToolSummaryPrompt(const std::string& question,
        const std::string& toolName,
        const json& toolResult) const;

private:
    json calculateBmi(const json& args, int userId);
    json calculateBmr(const json& args, int userId);
    json calculateTdee(const json& args, int userId);
    json calculateCalorieDeficit(const json& args);
    json calculateTrainingVolume(const json& args, int userId);
    json summarizeRecentTraining(const json& args, int userId);

    json profileForUser(int userId);

    FitnessDataProvider* dataProvider_;
    std::vector<FitnessToolDefinition> tools_;
};

} // namespace tools
