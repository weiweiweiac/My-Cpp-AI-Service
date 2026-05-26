#include "AIApps/ChatServer/include/tools/FitnessToolService.h"

#include <cmath>
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

bool nearly(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

int main()
{
    tools::FitnessToolService service;
    auto tools = service.listTools();

    require(tools.size() >= 10, "FitnessToolService should register calculation and calendar tools");
    require(service.hasTool("calculate_bmi"), "calculate_bmi should be registered");
    require(service.hasTool("get_today_training_plan"), "get_today_training_plan should be registered");

    auto bmi = service.callTool("calculate_bmi", json{{"heightCm", 175}, {"weightKg", 70}}, 7);
    require(bmi.value("success", false), "calculate_bmi should accept valid input");
    require(nearly(bmi["bmi"].get<double>(), 22.86, 0.01), "calculate_bmi should compute BMI");
    require(bmi["category"].get<std::string>() == "正常", "calculate_bmi should classify normal BMI");

    auto badBmi = service.callTool("calculate_bmi", json{{"heightCm", 30}, {"weightKg", 70}}, 7);
    require(!badBmi.value("success", true), "calculate_bmi should reject invalid height");

    auto bmr = service.callTool("calculate_bmr",
        json{{"gender", "male"}, {"age", 23}, {"heightCm", 175}, {"weightKg", 70}}, 7);
    require(bmr.value("success", false), "calculate_bmr should accept valid input");
    require(nearly(bmr["bmr"].get<double>(), 1668.75, 0.01), "calculate_bmr should use Mifflin-St Jeor");

    auto tdee = service.callTool("calculate_tdee", json{{"bmr", 1668.75}, {"activityLevel", "moderate"}}, 7);
    require(tdee.value("success", false), "calculate_tdee should accept valid input");
    require(nearly(tdee["activityFactor"].get<double>(), 1.55, 0.001), "calculate_tdee should use activity factor");

    auto deficit = service.callTool("calculate_calorie_deficit",
        json{{"tdee", 2585}, {"goal", "减脂"}, {"deficitPercent", 15}}, 7);
    require(deficit.value("success", false), "calculate_calorie_deficit should accept safe deficit");
    require(deficit["targetCalories"].get<int>() == 2197, "calculate_calorie_deficit should compute target calories");

    auto volume = service.callTool("calculate_training_volume",
        json{{"records", json::array({
            json{{"exerciseName", "杠铃卧推"}, {"weightKg", 60}, {"sets", 4}, {"reps", 8}},
            json{{"exerciseName", "杠铃划船"}, {"weightKg", 50}, {"sets", 3}, {"reps", 10}}
        })}},
        7);
    require(volume.value("success", false), "calculate_training_volume should accept structured records");
    require(nearly(volume["totalVolume"].get<double>(), 3420.0, 0.01),
        "calculate_training_volume should sum weight * sets * reps");

    require(service.matchToolName("我 BMI 多少") == "calculate_bmi", "Tool matching should route BMI questions");
    require(service.matchToolName("我今天该练什么？") == "get_today_training_plan",
        "Tool matching should route today training questions");
    require(service.matchToolName("最近训练记录总结一下") == "summarize_recent_training",
        "Tool matching should route recent training summaries");
    require(service.matchToolName("帮我记录一下今天训练") == "save_training_record",
        "Tool matching should route structured save intent");

    return 0;
}
