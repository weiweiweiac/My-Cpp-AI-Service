#include "../../include/tools/FitnessToolService.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace
{

std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool containsAny(const std::string& text, const std::vector<std::string>& needles)
{
    for (const auto& needle : needles)
    {
        if (text.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

json schema(const json& properties, const std::vector<std::string>& required = {})
{
    json body;
    body["type"] = "object";
    body["properties"] = properties;
    body["required"] = required;
    return body;
}

double round2(double value)
{
    return std::round(value * 100.0) / 100.0;
}

int roundInt(double value)
{
    return static_cast<int>(std::round(value));
}

std::string stringValue(const json& body, const std::vector<std::string>& keys)
{
    for (const auto& key : keys)
    {
        if (!body.contains(key) || body[key].is_null())
        {
            continue;
        }
        if (body[key].is_string())
        {
            return body[key].get<std::string>();
        }
        if (body[key].is_number_integer())
        {
            return std::to_string(body[key].get<int>());
        }
        if (body[key].is_number_float())
        {
            std::ostringstream oss;
            oss << body[key].get<double>();
            return oss.str();
        }
    }
    return "";
}

bool numberValue(const json& body, const std::vector<std::string>& keys, double& value)
{
    std::string text;
    for (const auto& key : keys)
    {
        if (!body.contains(key) || body[key].is_null())
        {
            continue;
        }
        if (body[key].is_number())
        {
            value = body[key].get<double>();
            return true;
        }
        if (body[key].is_string())
        {
            text = body[key].get<std::string>();
            break;
        }
    }

    if (text.empty())
    {
        return false;
    }

    try
    {
        size_t pos = 0;
        value = std::stod(text, &pos);
        return pos == text.size();
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool intValue(const json& body, const std::vector<std::string>& keys, int& value)
{
    double parsed = 0.0;
    if (!numberValue(body, keys, parsed))
    {
        return false;
    }
    value = static_cast<int>(std::round(parsed));
    return true;
}

json errorResult(const std::string& message)
{
    return json{{"success", false}, {"message", message}};
}

double activityFactor(const std::string& level)
{
    if (level == "sedentary") return 1.2;
    if (level == "light") return 1.375;
    if (level == "moderate") return 1.55;
    if (level == "active") return 1.725;
    if (level == "very_active") return 1.9;
    return 0.0;
}

std::string bmiCategory(double bmi)
{
    if (bmi < 18.5) return "偏瘦";
    if (bmi < 24.0) return "正常";
    if (bmi < 28.0) return "超重";
    return "肥胖";
}

json mergeProfileFallback(json args, const json& profile)
{
    if (!profile.is_object())
    {
        return args;
    }
    if (!args.contains("heightCm") && profile.contains("height_cm"))
    {
        args["heightCm"] = profile["height_cm"];
    }
    if (!args.contains("weightKg") && profile.contains("weight_kg"))
    {
        args["weightKg"] = profile["weight_kg"];
    }
    if (!args.contains("age") && profile.contains("age"))
    {
        args["age"] = profile["age"];
    }
    if (!args.contains("gender") && profile.contains("gender"))
    {
        args["gender"] = profile["gender"];
    }
    return args;
}

} // namespace

namespace tools
{

FitnessToolService::FitnessToolService(FitnessDataProvider* dataProvider)
    : dataProvider_(dataProvider)
{
    tools_ = {
        {
            "calculate_bmi",
            "根据身高体重计算 BMI",
            schema({
                {"heightCm", {{"type", "number"}, {"description", "身高，单位 cm，范围 80-250"}}},
                {"weightKg", {{"type", "number"}, {"description", "体重，单位 kg，范围 20-300"}}}
            }),
            false
        },
        {
            "calculate_bmr",
            "使用 Mifflin-St Jeor 公式计算基础代谢 BMR",
            schema({
                {"gender", {{"type", "string"}, {"enum", {"male", "female"}}}},
                {"age", {{"type", "integer"}, {"description", "年龄，范围 10-100"}}},
                {"heightCm", {{"type", "number"}}},
                {"weightKg", {{"type", "number"}}}
            }),
            false
        },
        {
            "calculate_tdee",
            "根据 BMR 和活动水平估算 TDEE",
            schema({
                {"bmr", {{"type", "number"}}},
                {"activityLevel", {{"type", "string"}, {"enum", {"sedentary", "light", "moderate", "active", "very_active"}}}}
            }),
            false
        },
        {
            "calculate_calorie_deficit",
            "根据 TDEE 和缺口比例计算减脂目标热量",
            schema({
                {"tdee", {{"type", "number"}}},
                {"goal", {{"type", "string"}}},
                {"deficitPercent", {{"type", "number"}, {"description", "建议 5-25"}}}
            }),
            false
        },
        {
            "calculate_training_volume",
            "根据动作记录计算训练容量",
            schema({
                {"records", {{"type", "array"}, {"description", "包含 exerciseName、weightKg、sets、reps 的数组"}}}
            }),
            false
        },
        {
            "get_today_training_plan",
            "查询当前用户今日训练计划和记录",
            schema({{"date", {{"type", "string"}, {"description", "可选，YYYY-MM-DD"}}}}),
            true
        },
        {
            "get_week_training_calendar",
            "查询当前用户本周训练日历",
            schema({
                {"startDate", {{"type", "string"}}},
                {"endDate", {{"type", "string"}}}
            }),
            true
        },
        {
            "get_recent_training_records",
            "查询当前用户最近训练记录",
            schema({{"days", {{"type", "integer"}, {"description", "默认 30，最大 90"}}}}),
            true
        },
        {
            "save_training_record",
            "保存当前用户结构化训练记录",
            schema({
                {"date", {{"type", "string"}}},
                {"durationMinutes", {{"type", "integer"}}},
                {"feelingNote", {{"type", "string"}}},
                {"records", {{"type", "array"}}}
            }, {"date", "records"}),
            true
        },
        {
            "summarize_recent_training",
            "读取最近训练记录并返回统计摘要",
            schema({{"days", {{"type", "integer"}, {"description", "默认 30，最大 90"}}}}),
            true
        }
    };
}

std::vector<FitnessToolDefinition> FitnessToolService::listTools() const
{
    return tools_;
}

bool FitnessToolService::hasTool(const std::string& name) const
{
    return std::any_of(tools_.begin(), tools_.end(), [&name](const FitnessToolDefinition& tool) {
        return tool.name == name;
    });
}

json FitnessToolService::callTool(const std::string& name, const json& args, int userId)
{
    if (!hasTool(name))
    {
        return errorResult("未知工具: " + name);
    }
    if (name == "calculate_bmi") return calculateBmi(args, userId);
    if (name == "calculate_bmr") return calculateBmr(args, userId);
    if (name == "calculate_tdee") return calculateTdee(args, userId);
    if (name == "calculate_calorie_deficit") return calculateCalorieDeficit(args);
    if (name == "calculate_training_volume") return calculateTrainingVolume(args, userId);
    if (name == "summarize_recent_training") return summarizeRecentTraining(args, userId);

    if (dataProvider_ == nullptr)
    {
        return errorResult("该工具需要登录用户数据，当前没有可用的数据提供者");
    }
    if (name == "get_today_training_plan") return dataProvider_->getTodayTrainingPlan(userId, args);
    if (name == "get_week_training_calendar") return dataProvider_->getWeekTrainingCalendar(userId, args);
    if (name == "get_recent_training_records") return dataProvider_->getRecentTrainingRecords(userId, args);
    if (name == "save_training_record") return dataProvider_->saveTrainingRecord(userId, args);

    return errorResult("工具未实现: " + name);
}

std::string FitnessToolService::matchToolName(const std::string& question) const
{
    std::string lower = toLowerAscii(question);
    if (containsAny(lower, {"bmi"})) return "calculate_bmi";
    if (containsAny(lower, {"bmr", "基础代谢"})) return "calculate_bmr";
    if (containsAny(lower, {"tdee", "总消耗"})) return "calculate_tdee";
    if (containsAny(lower, {"热量缺口", "减脂热量"})) return "calculate_calorie_deficit";
    if (containsAny(lower, {"训练容量"})) return "calculate_training_volume";
    if (containsAny(lower, {"今天练什么", "今天该练什么", "今日训练"})) return "get_today_training_plan";
    if (containsAny(lower, {"本周训练", "一周训练"})) return "get_week_training_calendar";
    if (containsAny(lower, {"最近训练总结", "训练记录总结", "总结最近训练"})) return "summarize_recent_training";
    if (containsAny(lower, {"最近训练", "训练记录"})) return "get_recent_training_records";
    if (containsAny(lower, {"帮我记录", "记录一下"})) return "save_training_record";
    return "";
}

std::string FitnessToolService::buildToolSummaryPrompt(const std::string& question,
    const std::string& toolName,
    const json& toolResult) const
{
    std::ostringstream prompt;
    prompt
        << "你是 AI 私人健身教练。\n"
        << "用户原始问题：\n" << question << "\n\n"
        << "已调用工具名称：\n" << toolName << "\n\n"
        << "工具返回 JSON：\n" << toolResult.dump(2) << "\n\n"
        << "请基于工具结果给出中文解释。\n"
        << "如果是 BMI/BMR/TDEE/热量缺口，说明仅供健身参考，不用于医疗建议。\n"
        << "不做医疗诊断；如果数据不足，明确说明需要用户补充档案或记录。\n"
        << "如果是训练日历，输出今日/本周训练重点和注意事项。\n"
        << "如果是训练记录总结，输出训练频率、训练容量、恢复建议。\n";
    return prompt.str();
}

json FitnessToolService::profileForUser(int userId)
{
    if (dataProvider_ == nullptr)
    {
        return json::object();
    }
    json profileResult = dataProvider_->loadProfile(userId);
    if (!profileResult.value("success", false) || !profileResult.contains("profile") || profileResult["profile"].is_null())
    {
        return json::object();
    }
    return profileResult["profile"];
}

json FitnessToolService::calculateBmi(const json& args, int userId)
{
    json merged = mergeProfileFallback(args, profileForUser(userId));
    double heightCm = 0.0;
    double weightKg = 0.0;
    if (!numberValue(merged, {"heightCm", "height_cm"}, heightCm) ||
        !numberValue(merged, {"weightKg", "weight_kg"}, weightKg))
    {
        return errorResult("缺少身高或体重，请传入 heightCm/weightKg 或先完善健身档案");
    }
    if (heightCm < 80.0 || heightCm > 250.0)
    {
        return errorResult("heightCm 范围应为 80-250");
    }
    if (weightKg < 20.0 || weightKg > 300.0)
    {
        return errorResult("weightKg 范围应为 20-300");
    }

    double heightM = heightCm / 100.0;
    double bmi = round2(weightKg / (heightM * heightM));
    json result;
    result["success"] = true;
    result["bmi"] = bmi;
    result["category"] = bmiCategory(bmi);
    result["message"] = "BMI 仅供健身参考，不用于医疗诊断";
    return result;
}

json FitnessToolService::calculateBmr(const json& args, int userId)
{
    json merged = mergeProfileFallback(args, profileForUser(userId));
    double heightCm = 0.0;
    double weightKg = 0.0;
    int age = 0;
    std::string gender = stringValue(merged, {"gender"});

    if (!numberValue(merged, {"heightCm", "height_cm"}, heightCm) ||
        !numberValue(merged, {"weightKg", "weight_kg"}, weightKg) ||
        !intValue(merged, {"age"}, age))
    {
        return errorResult("缺少 gender/age/heightCm/weightKg，请补充参数或健身档案");
    }
    if (age < 10 || age > 100) return errorResult("age 范围应为 10-100");
    if (heightCm < 80.0 || heightCm > 250.0) return errorResult("heightCm 范围应为 80-250");
    if (weightKg < 20.0 || weightKg > 300.0) return errorResult("weightKg 范围应为 20-300");

    json result;
    result["success"] = true;
    result["formula"] = "Mifflin-St Jeor";
    if (gender == "male")
    {
        result["bmr"] = round2(10.0 * weightKg + 6.25 * heightCm - 5.0 * age + 5.0);
    }
    else if (gender == "female")
    {
        result["bmr"] = round2(10.0 * weightKg + 6.25 * heightCm - 5.0 * age - 161.0);
    }
    else
    {
        double male = round2(10.0 * weightKg + 6.25 * heightCm - 5.0 * age + 5.0);
        double female = round2(10.0 * weightKg + 6.25 * heightCm - 5.0 * age - 161.0);
        result["bmrRange"] = json{{"female", female}, {"male", male}};
        result["message"] = "性别未知，返回男性/女性公式估算范围";
    }
    return result;
}

json FitnessToolService::calculateTdee(const json& args, int userId)
{
    double bmr = 0.0;
    json merged = args;
    if (!numberValue(merged, {"bmr"}, bmr))
    {
        json bmrResult = calculateBmr(args, userId);
        if (!bmrResult.value("success", false) || !bmrResult.contains("bmr"))
        {
            return errorResult("缺少 bmr，且无法从健身档案计算 BMR");
        }
        bmr = bmrResult["bmr"].get<double>();
    }

    std::string level = stringValue(args, {"activityLevel", "activity_level"});
    if (level.empty())
    {
        level = "moderate";
    }
    double factor = activityFactor(level);
    if (factor <= 0.0)
    {
        return errorResult("activityLevel 只支持 sedentary/light/moderate/active/very_active");
    }

    json result;
    result["success"] = true;
    result["tdee"] = roundInt(bmr * factor);
    result["activityFactor"] = factor;
    result["activityLevel"] = level;
    return result;
}

json FitnessToolService::calculateCalorieDeficit(const json& args)
{
    double tdee = 0.0;
    double deficitPercent = 15.0;
    if (!numberValue(args, {"tdee"}, tdee))
    {
        return errorResult("缺少 tdee");
    }
    numberValue(args, {"deficitPercent", "deficit_percent"}, deficitPercent);
    if (deficitPercent < 5.0 || deficitPercent > 25.0)
    {
        return errorResult("deficitPercent 建议范围为 5-25，避免极端热量缺口");
    }

    int deficitCalories = roundInt(tdee * deficitPercent / 100.0);
    json result;
    result["success"] = true;
    result["targetCalories"] = roundInt(tdee - deficitCalories);
    result["deficitCalories"] = deficitCalories;
    result["deficitPercent"] = deficitPercent;
    result["message"] = "仅供健身参考，不用于医疗建议；避免长期极端热量缺口";
    return result;
}

json FitnessToolService::calculateTrainingVolume(const json& args, int userId)
{
    json records = json::array();
    if (args.contains("records") && args["records"].is_array())
    {
        records = args["records"];
    }
    else if (dataProvider_ != nullptr)
    {
        json recent = dataProvider_->getRecentTrainingRecords(userId, args);
        if (recent.value("success", false) && recent.contains("records"))
        {
            records = recent["records"];
        }
    }

    if (!records.is_array() || records.empty())
    {
        return errorResult("records 必须是非空数组，或当前用户需要已有训练记录");
    }

    double total = 0.0;
    std::map<std::string, double> byExercise;
    for (const auto& record : records)
    {
        std::string exerciseName = stringValue(record, {"exerciseName", "exercise_name"});
        double weight = 0.0;
        double sets = 0.0;
        double reps = 0.0;
        if (exerciseName.empty() ||
            !numberValue(record, {"weightKg", "weight_kg"}, weight) ||
            !numberValue(record, {"sets"}, sets) ||
            !numberValue(record, {"reps"}, reps))
        {
            return errorResult("每条记录必须包含 exerciseName、weightKg、sets、reps");
        }
        double volume = weight * sets * reps;
        total += volume;
        byExercise[exerciseName] += volume;
    }

    json exercises = json::array();
    for (const auto& item : byExercise)
    {
        exercises.push_back({{"exerciseName", item.first}, {"volume", round2(item.second)}});
    }

    json result;
    result["success"] = true;
    result["totalVolume"] = round2(total);
    result["unit"] = "kg";
    result["byExercise"] = exercises;
    return result;
}

json FitnessToolService::summarizeRecentTraining(const json& args, int userId)
{
    if (dataProvider_ == nullptr)
    {
        return errorResult("summarize_recent_training 需要用户训练记录数据");
    }

    json recent = dataProvider_->getRecentTrainingRecords(userId, args);
    if (!recent.value("success", false))
    {
        return recent;
    }

    json records = recent.value("records", json::array());
    std::set<std::string> dates;
    std::map<std::string, int> exerciseCounts;
    std::map<std::string, int> durationByDate;
    double totalVolume = 0.0;

    for (const auto& record : records)
    {
        std::string date = stringValue(record, {"record_date", "date"});
        std::string exerciseName = stringValue(record, {"exerciseName", "exercise_name"});
        if (!date.empty())
        {
            dates.insert(date);
        }
        if (!exerciseName.empty())
        {
            exerciseCounts[exerciseName] += 1;
        }
        int duration = 0;
        if (!date.empty() && intValue(record, {"durationMinutes", "duration_minutes"}, duration))
        {
            durationByDate[date] = std::max(durationByDate[date], duration);
        }
        double weight = 0.0;
        double sets = 0.0;
        double reps = 0.0;
        if (numberValue(record, {"weightKg", "weight_kg"}, weight) &&
            numberValue(record, {"sets"}, sets) &&
            numberValue(record, {"reps"}, reps))
        {
            totalVolume += weight * sets * reps;
        }
    }

    int totalDuration = 0;
    for (const auto& item : durationByDate)
    {
        totalDuration += item.second;
    }

    json commonExercises = json::array();
    for (const auto& item : exerciseCounts)
    {
        commonExercises.push_back({{"exerciseName", item.first}, {"count", item.second}});
    }

    json stats;
    stats["trainingDays"] = dates.size();
    stats["totalRecords"] = records.size();
    stats["totalDurationMinutes"] = totalDuration;
    stats["totalVolume"] = round2(totalVolume);
    stats["commonExercises"] = commonExercises;

    json result;
    result["success"] = true;
    result["summary"] = "已生成最近训练结构化统计，可交给 AI 教练进一步总结";
    result["stats"] = stats;
    result["records"] = records;
    return result;
}

} // namespace tools
