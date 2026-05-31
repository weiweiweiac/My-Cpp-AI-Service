#include "../../include/tools/AgentToolCalling.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

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

json schema(const json& properties, const std::vector<std::string>& required)
{
    return json{{"type", "object"}, {"properties", properties}, {"required", required}};
}

std::string nowText(const char* format)
{
    std::time_t now = std::time(nullptr);
    std::tm local = *std::localtime(&now);
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), format, &local);
    return std::string(buffer);
}

std::string makeTraceId()
{
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1000, 9999);
    return "agent-" + nowText("%Y%m%d-%H%M%S") + "-" + std::to_string(dist(rng));
}

std::string truncateText(const std::string& value, std::size_t maxBytes)
{
    if (value.size() <= maxBytes)
    {
        return value;
    }
    return value.substr(0, maxBytes);
}

bool matchDouble(const std::string& text, const std::vector<std::string>& patterns, double& value)
{
    for (const auto& pattern : patterns)
    {
        std::smatch match;
        if (std::regex_search(text, match, std::regex(pattern)) && match.size() >= 2)
        {
            try
            {
                value = std::stod(match[1].str());
                return true;
            }
            catch (const std::exception&)
            {
            }
        }
    }
    return false;
}

bool matchInt(const std::string& text, const std::vector<std::string>& patterns, int& value)
{
    double parsed = 0.0;
    if (!matchDouble(text, patterns, parsed))
    {
        return false;
    }
    value = static_cast<int>(std::round(parsed));
    return true;
}

bool numberValue(const json& body, const std::string& key, double& value)
{
    if (!body.contains(key) || body[key].is_null())
    {
        return false;
    }
    try
    {
        if (body[key].is_number())
        {
            value = body[key].get<double>();
            return true;
        }
        if (body[key].is_string())
        {
            std::string text = body[key].get<std::string>();
            size_t pos = 0;
            value = std::stod(text, &pos);
            return pos == text.size();
        }
    }
    catch (const std::exception&)
    {
    }
    return false;
}

bool intValue(const json& body, const std::string& key, int& value)
{
    double parsed = 0.0;
    if (!numberValue(body, key, parsed))
    {
        return false;
    }
    value = static_cast<int>(std::round(parsed));
    return true;
}

std::string stringValue(const json& body, const std::string& key)
{
    if (!body.contains(key) || body[key].is_null())
    {
        return "";
    }
    if (body[key].is_string())
    {
        return body[key].get<std::string>();
    }
    return body[key].dump();
}

bool validDate(const std::string& value)
{
    if (!std::regex_match(value, std::regex(R"(\d{4}-\d{2}-\d{2})")))
    {
        return false;
    }
    std::tm tm {};
    std::istringstream iss(value);
    iss >> std::get_time(&tm, "%Y-%m-%d");
    if (iss.fail())
    {
        return false;
    }
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    std::tm check = tm;
    std::mktime(&check);
    return check.tm_year == tm.tm_year && check.tm_mon == tm.tm_mon && check.tm_mday == tm.tm_mday;
}

std::string formatDate(const std::tm& tm)
{
    char buffer[16] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return std::string(buffer);
}

std::string addDays(const std::string& date, int days)
{
    std::tm tm {};
    std::istringstream iss(date);
    iss >> std::get_time(&tm, "%Y-%m-%d");
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    t += static_cast<std::time_t>(days) * 24 * 60 * 60;
    std::tm local = *std::localtime(&t);
    return formatDate(local);
}

std::string todayDate()
{
    std::time_t now = std::time(nullptr);
    std::tm local = *std::localtime(&now);
    return formatDate(local);
}

std::vector<std::string> findDates(const std::string& text)
{
    std::vector<std::string> dates;
    std::regex pattern(R"(\d{4}-\d{2}-\d{2})");
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it)
    {
        dates.push_back(it->str());
    }
    return dates;
}

std::string activityLevelFromWeeklyTimes(int times)
{
    if (times <= 1) return "sedentary";
    if (times <= 3) return "light";
    if (times <= 5) return "moderate";
    if (times <= 6) return "active";
    return "very_active";
}

std::string summarizeResult(const json& result)
{
    if (result.is_object())
    {
        if (result.contains("message") && result["message"].is_string())
        {
            return truncateText(result["message"].get<std::string>(), 300);
        }
        if (result.contains("bmi")) return "BMI=" + result["bmi"].dump();
        if (result.contains("bmr")) return "BMR=" + result["bmr"].dump();
        if (result.contains("tdee")) return "TDEE=" + result["tdee"].dump();
        if (result.contains("totalVolume")) return "totalVolume=" + result["totalVolume"].dump();
        if (result.contains("records")) return "records=" + std::to_string(result["records"].size());
    }
    return truncateText(result.dump(), 300);
}

json mergeArguments(json extracted, const json& explicitArguments)
{
    if (!explicitArguments.is_object())
    {
        return extracted;
    }
    for (auto it = explicitArguments.begin(); it != explicitArguments.end(); ++it)
    {
        extracted[it.key()] = it.value();
    }
    return extracted;
}

} // namespace

namespace agent
{

json AgentToolSchema::toJson() const
{
    return json{
        {"name", name},
        {"legacyToolName", legacyToolName},
        {"intent", intent},
        {"description", description},
        {"parameters", parameters},
        {"requiredFields", requiredFields},
        {"examples", examples}
    };
}

bool AgentToolCall::matched() const
{
    return !toolName.empty() && toolName != "unknown" && !legacyToolName.empty();
}

json AgentToolResult::toJson() const
{
    return json{
        {"success", success},
        {"toolName", toolName},
        {"resultJson", resultJson},
        {"errorMessage", errorMessage}
    };
}

AgentTrace AgentTrace::start(const std::string& type, const AgentToolCall& call)
{
    AgentTrace trace;
    trace.traceId = makeTraceId();
    trace.type = type;
    trace.userMessage = truncateText(call.rawUserMessage, 500);
    trace.intent = call.intent;
    trace.selectedTool = call.toolName;
    trace.arguments = call.arguments.is_object() ? call.arguments : json::object();
    trace.needSecondLLMCall = call.needSecondLLMCall;
    trace.finalAnswerStatus = call.needSecondLLMCall ? "pending" : "not_required";
    trace.createdAt = nowText("%Y-%m-%d %H:%M:%S");
    return trace;
}

void AgentTrace::markValidation(bool success, const std::string& error)
{
    validationStatus = success ? "success" : "failed";
    validationError = success ? "" : error;
    if (!success)
    {
        errorMessage = error;
        toolStatus = "not_started";
        finalAnswerStatus = "failed";
    }
}

void AgentTrace::markToolResult(bool success, const json& result)
{
    toolStatus = success ? "success" : "failed";
    toolResultSummary = summarizeResult(result);
    if (!success)
    {
        errorMessage = result.value("message", std::string("工具执行失败"));
        finalAnswerStatus = needSecondLLMCall ? "failed" : "not_required";
    }
}

void AgentTrace::markFinalAnswer(bool success, const std::string& error)
{
    finalAnswerStatus = success ? "success" : "failed";
    if (!success)
    {
        errorMessage = error;
    }
}

void AgentTrace::markError(const std::string& error)
{
    errorMessage = error;
    if (validationStatus == "pending")
    {
        validationStatus = "failed";
        validationError = error;
    }
    if (toolStatus == "not_started")
    {
        toolStatus = "failed";
    }
    finalAnswerStatus = "failed";
}

json AgentTrace::toJson() const
{
    return json{
        {"traceId", traceId},
        {"type", type},
        {"userMessage", userMessage},
        {"intent", intent},
        {"selectedTool", selectedTool},
        {"arguments", arguments},
        {"validationStatus", validationStatus},
        {"validationError", validationError},
        {"toolStatus", toolStatus},
        {"toolResultSummary", toolResultSummary},
        {"needSecondLLMCall", needSecondLLMCall},
        {"finalAnswerStatus", finalAnswerStatus},
        {"errorMessage", errorMessage},
        {"createdAt", createdAt}
    };
}

std::vector<AgentToolSchema> defaultAgentToolSchemas()
{
    return {
        {
            "bmi_calculator",
            "calculate_bmi",
            "calculate_bmi",
            "根据身高和体重计算 BMI。",
            schema({
                {"heightCm", {{"type", "number"}, {"description", "身高，单位 cm，范围 80-250"}}},
                {"weightKg", {{"type", "number"}, {"description", "体重，单位 kg，范围 20-300"}}}
            }, {"heightCm", "weightKg"}),
            {"heightCm", "weightKg"},
            {"我身高175cm体重70kg，BMI是多少？"}
        },
        {
            "bmr_calculator",
            "calculate_bmr",
            "calculate_bmr",
            "使用 Mifflin-St Jeor 公式估算基础代谢 BMR。",
            schema({
                {"gender", {{"type", "string"}, {"enum", {"male", "female"}}}},
                {"age", {{"type", "integer"}, {"description", "年龄，范围 10-100"}}},
                {"heightCm", {{"type", "number"}}},
                {"weightKg", {{"type", "number"}}}
            }, {"gender", "age", "heightCm", "weightKg"}),
            {"gender", "age", "heightCm", "weightKg"},
            {"男，22岁，175cm，70kg，帮我算BMR"}
        },
        {
            "tdee_calculator",
            "calculate_tdee",
            "calculate_tdee",
            "根据 BMR 和活动水平估算每日总消耗 TDEE。",
            schema({
                {"gender", {{"type", "string"}, {"enum", {"male", "female"}}}},
                {"age", {{"type", "integer"}}},
                {"heightCm", {{"type", "number"}}},
                {"weightKg", {{"type", "number"}}},
                {"activityLevel", {{"type", "string"}, {"enum", {"sedentary", "light", "moderate", "active", "very_active"}}}}
            }, {"gender", "age", "heightCm", "weightKg", "activityLevel"}),
            {"gender", "age", "heightCm", "weightKg", "activityLevel"},
            {"男，22岁，175cm，70kg，每周训练4次，帮我算TDEE"}
        },
        {
            "training_volume_calculator",
            "calculate_training_volume",
            "calculate_training_volume",
            "根据重量、次数、组数计算训练容量。",
            schema({
                {"weight", {{"type", "number"}, {"description", "训练重量，单位 kg"}}},
                {"reps", {{"type", "integer"}}},
                {"sets", {{"type", "integer"}}},
                {"exerciseName", {{"type", "string"}}}
            }, {"weight", "reps", "sets"}),
            {"weight", "reps", "sets"},
            {"卧推80kg做8次4组，训练容量是多少？"}
        },
        {
            "training_record_query",
            "get_recent_training_records",
            "query_training_records",
            "查询用户训练记录。",
            schema({
                {"startDate", {{"type", "string"}, {"description", "开始日期，YYYY-MM-DD"}}},
                {"endDate", {{"type", "string"}, {"description", "结束日期，YYYY-MM-DD"}}},
                {"exerciseName", {{"type", "string"}}},
                {"days", {{"type", "integer"}}}
            }, {"startDate", "endDate"}),
            {"startDate", "endDate"},
            {"查询最近训练记录"}
        }
    };
}

json agentToolSchemasToJson(const std::vector<AgentToolSchema>& schemas)
{
    json rows = json::array();
    for (const auto& item : schemas)
    {
        rows.push_back(item.toJson());
    }
    return rows;
}

std::string standardToolName(const std::string& name)
{
    if (name == "bmi_calculator" || name == "calculate_bmi") return "bmi_calculator";
    if (name == "bmr_calculator" || name == "calculate_bmr") return "bmr_calculator";
    if (name == "tdee_calculator" || name == "calculate_tdee") return "tdee_calculator";
    if (name == "training_volume_calculator" || name == "calculate_training_volume")
    {
        return "training_volume_calculator";
    }
    if (name == "training_record_query" || name == "get_recent_training_records" ||
        name == "summarize_recent_training")
    {
        return "training_record_query";
    }
    return name.empty() ? "unknown" : name;
}

std::string legacyToolName(const std::string& standardName)
{
    if (standardName == "bmi_calculator") return "calculate_bmi";
    if (standardName == "bmr_calculator") return "calculate_bmr";
    if (standardName == "tdee_calculator") return "calculate_tdee";
    if (standardName == "training_volume_calculator") return "calculate_training_volume";
    if (standardName == "training_record_query") return "get_recent_training_records";
    return "";
}

std::string intentForTool(const std::string& standardName)
{
    if (standardName == "bmi_calculator") return "calculate_bmi";
    if (standardName == "bmr_calculator") return "calculate_bmr";
    if (standardName == "tdee_calculator") return "calculate_tdee";
    if (standardName == "training_volume_calculator") return "calculate_training_volume";
    if (standardName == "training_record_query") return "query_training_records";
    return "unknown";
}

AgentToolRouter::AgentToolRouter(std::vector<AgentToolSchema> schemas)
    : schemas_(std::move(schemas))
{
}

AgentToolCall AgentToolRouter::route(const std::string& userMessage,
    const json& explicitArguments,
    const std::string& explicitToolName,
    bool needSecondLLMCall) const
{
    std::string selected = standardToolName(explicitToolName);
    double confidence = 1.0;
    if (explicitToolName.empty())
    {
        std::string lower = toLowerAscii(userMessage);
        confidence = 0.9;
        if (containsAny(lower, {"bmi", "体质指数"}))
        {
            selected = "bmi_calculator";
        }
        else if (containsAny(lower, {"bmr", "基础代谢"}))
        {
            selected = "bmr_calculator";
        }
        else if (containsAny(lower, {"tdee", "总消耗", "每日消耗"}))
        {
            selected = "tdee_calculator";
        }
        else if (containsAny(lower, {"训练容量", "容量", "总重量"}))
        {
            selected = "training_volume_calculator";
        }
        else if (containsAny(lower, {"训练记录", "最近训练", "记录"}))
        {
            selected = "training_record_query";
        }
        else
        {
            selected = "unknown";
            confidence = 0.0;
        }
    }

    std::string legacy = legacyToolName(selected);
    json arguments = extractArguments(userMessage, selected);
    arguments = mergeArguments(arguments, explicitArguments);

    AgentToolCall call;
    call.toolName = selected;
    call.legacyToolName = legacy;
    call.rawUserMessage = userMessage;
    call.intent = intentForTool(selected);
    call.arguments = arguments;
    call.confidence = confidence;
    call.needSecondLLMCall = needSecondLLMCall;
    return call;
}

const AgentToolSchema* AgentToolRouter::findSchema(const std::string& standardName) const
{
    auto it = std::find_if(schemas_.begin(), schemas_.end(), [&standardName](const AgentToolSchema& schema) {
        return schema.name == standardName;
    });
    return it == schemas_.end() ? nullptr : &(*it);
}

json AgentToolRouter::extractArguments(const std::string& userMessage, const std::string& standardName) const
{
    json args = json::object();
    if (userMessage.empty())
    {
        return args;
    }

    if (standardName == "bmi_calculator" || standardName == "bmr_calculator" ||
        standardName == "tdee_calculator")
    {
        double height = 0.0;
        if (matchDouble(userMessage,
            {R"(身高\s*(?:是|:|：)?\s*(\d+(?:\.\d+)?)\s*(?:cm|厘米)?)",
             R"((\d+(?:\.\d+)?)\s*(?:cm|厘米))"},
            height))
        {
            args["heightCm"] = height;
        }

        double weight = 0.0;
        if (matchDouble(userMessage,
            {R"(体重\s*(?:是|:|：)?\s*(\d+(?:\.\d+)?)\s*(?:kg|公斤)?)",
             R"((\d+(?:\.\d+)?)\s*(?:kg|公斤))"},
            weight))
        {
            args["weightKg"] = weight;
        }
    }

    if (standardName == "bmr_calculator" || standardName == "tdee_calculator")
    {
        std::string lower = toLowerAscii(userMessage);
        if (containsAny(lower, {"female", "女"}))
        {
            args["gender"] = "female";
        }
        else if (containsAny(lower, {"male", "男"}))
        {
            args["gender"] = "male";
        }

        int age = 0;
        if (matchInt(userMessage,
            {R"(年龄\s*(?:是|:|：)?\s*(\d{1,3}))", R"((\d{1,3})\s*岁)"},
            age))
        {
            args["age"] = age;
        }
    }

    if (standardName == "tdee_calculator")
    {
        int weeklyTimes = 0;
        if (matchInt(userMessage,
            {R"(每\s*周\s*(?:训练|运动|健身)?\s*(\d{1,2})\s*次)",
             R"(一\s*周\s*(?:训练|运动|健身)?\s*(\d{1,2})\s*次)"},
            weeklyTimes))
        {
            args["activityLevel"] = activityLevelFromWeeklyTimes(weeklyTimes);
        }
        else if (containsAny(userMessage, {"久坐"}))
        {
            args["activityLevel"] = "sedentary";
        }
        else if (containsAny(userMessage, {"轻度", "偶尔"}))
        {
            args["activityLevel"] = "light";
        }
        else if (containsAny(userMessage, {"中等", "规律"}))
        {
            args["activityLevel"] = "moderate";
        }
        else if (containsAny(userMessage, {"高强度", "频繁"}))
        {
            args["activityLevel"] = "active";
        }
    }

    if (standardName == "training_volume_calculator")
    {
        double weight = 0.0;
        if (matchDouble(userMessage, {R"((\d+(?:\.\d+)?)\s*(?:kg|公斤))"}, weight))
        {
            args["weight"] = weight;
        }
        int reps = 0;
        if (matchInt(userMessage, {R"(做\s*(\d+)\s*次)", R"((\d+)\s*次)"}, reps))
        {
            args["reps"] = reps;
        }
        int sets = 0;
        if (matchInt(userMessage, {R"((\d+)\s*组)"}, sets))
        {
            args["sets"] = sets;
        }
        if (containsAny(userMessage, {"卧推"}))
        {
            args["exerciseName"] = "卧推";
        }
    }

    if (standardName == "training_record_query")
    {
        std::vector<std::string> dates = findDates(userMessage);
        if (dates.size() >= 2)
        {
            args["startDate"] = dates[0];
            args["endDate"] = dates[1];
        }
        else if (dates.size() == 1)
        {
            args["startDate"] = dates[0];
            args["endDate"] = dates[0];
        }
        else
        {
            std::string end = todayDate();
            args["startDate"] = addDays(end, -29);
            args["endDate"] = end;
            args["days"] = 30;
        }
        if (containsAny(userMessage, {"卧推"}))
        {
            args["exerciseName"] = "卧推";
        }
    }

    return args;
}

AgentToolValidator::AgentToolValidator(std::vector<AgentToolSchema> schemas)
    : schemas_(std::move(schemas))
{
}

AgentToolValidationResult AgentToolValidator::validate(const AgentToolCall& call) const
{
    AgentToolValidationResult result;
    const AgentToolSchema* schema = findSchema(call.toolName);
    if (schema == nullptr)
    {
        result.success = false;
        result.message = "未匹配到可用工具";
        return result;
    }

    std::vector<std::string> requiredFields = schema->requiredFields;
    if (call.toolName == "training_volume_calculator" &&
        call.arguments.contains("records") && call.arguments["records"].is_array() &&
        !call.arguments["records"].empty())
    {
        requiredFields.clear();
    }
    if (call.toolName == "tdee_calculator" && call.arguments.contains("bmr"))
    {
        requiredFields.clear();
    }
    if (call.toolName == "training_record_query" && call.arguments.contains("days"))
    {
        requiredFields.clear();
    }

    for (const auto& field : requiredFields)
    {
        if (!call.arguments.contains(field) || call.arguments[field].is_null() ||
            (call.arguments[field].is_string() && call.arguments[field].get<std::string>().empty()))
        {
            result.missingFields.push_back(field);
        }
    }
    if (!result.missingFields.empty())
    {
        std::ostringstream oss;
        oss << "缺少参数: ";
        for (std::size_t i = 0; i < result.missingFields.size(); ++i)
        {
            if (i > 0) oss << ", ";
            oss << result.missingFields[i];
        }
        result.success = false;
        result.message = oss.str();
        return result;
    }

    double height = 0.0;
    if (call.arguments.contains("heightCm"))
    {
        if (!numberValue(call.arguments, "heightCm", height))
        {
            result.message = "heightCm 必须是数字";
            return result;
        }
        if (height < 80.0 || height > 250.0)
        {
            result.message = "heightCm 范围应为 80-250";
            return result;
        }
    }

    double weightKg = 0.0;
    if (call.arguments.contains("weightKg"))
    {
        if (!numberValue(call.arguments, "weightKg", weightKg))
        {
            result.message = "weightKg 必须是数字";
            return result;
        }
        if (weightKg < 20.0 || weightKg > 300.0)
        {
            result.message = "weightKg 范围应为 20-300";
            return result;
        }
    }

    int age = 0;
    if (call.arguments.contains("age"))
    {
        if (!intValue(call.arguments, "age", age))
        {
            result.message = "age 必须是整数";
            return result;
        }
        if (age < 10 || age > 100)
        {
            result.message = "age 范围应为 10-100";
            return result;
        }
    }

    std::string gender = stringValue(call.arguments, "gender");
    if (!gender.empty() && gender != "male" && gender != "female")
    {
        result.message = "gender 只支持 male/female";
        return result;
    }

    std::string activityLevel = stringValue(call.arguments, "activityLevel");
    if (!activityLevel.empty() && activityLevel != "sedentary" && activityLevel != "light" &&
        activityLevel != "moderate" && activityLevel != "active" && activityLevel != "very_active")
    {
        result.message = "activityLevel 只支持 sedentary/light/moderate/active/very_active";
        return result;
    }

    double trainingWeight = 0.0;
    if (call.arguments.contains("weight"))
    {
        if (!numberValue(call.arguments, "weight", trainingWeight))
        {
            result.message = "weight 必须是数字";
            return result;
        }
        if (trainingWeight <= 0.0 || trainingWeight > 500.0)
        {
            result.message = "weight 范围应为 0-500";
            return result;
        }
    }

    int reps = 0;
    if (call.arguments.contains("reps"))
    {
        if (!intValue(call.arguments, "reps", reps))
        {
            result.message = "reps 必须是整数";
            return result;
        }
        if (reps <= 0 || reps > 1000)
        {
            result.message = "reps 范围应为 1-1000";
            return result;
        }
    }

    int sets = 0;
    if (call.arguments.contains("sets"))
    {
        if (!intValue(call.arguments, "sets", sets))
        {
            result.message = "sets 必须是整数";
            return result;
        }
        if (sets <= 0 || sets > 100)
        {
            result.message = "sets 范围应为 1-100";
            return result;
        }
    }

    for (const auto& field : {"startDate", "endDate"})
    {
        std::string date = stringValue(call.arguments, field);
        if (!date.empty() && !validDate(date))
        {
            result.message = std::string(field) + " 格式应为 YYYY-MM-DD";
            return result;
        }
    }

    result.success = true;
    result.message = "success";
    return result;
}

const AgentToolSchema* AgentToolValidator::findSchema(const std::string& standardName) const
{
    auto it = std::find_if(schemas_.begin(), schemas_.end(), [&standardName](const AgentToolSchema& schema) {
        return schema.name == standardName;
    });
    return it == schemas_.end() ? nullptr : &(*it);
}

AgentToolExecutor::AgentToolExecutor(tools::FitnessToolService& service)
    : service_(service)
{
}

AgentToolResult AgentToolExecutor::execute(const AgentToolCall& call, int userId) const
{
    AgentToolResult result;
    result.toolName = call.toolName;
    std::string legacy = call.legacyToolName.empty() ? legacyToolName(call.toolName) : call.legacyToolName;
    if (legacy.empty())
    {
        result.success = false;
        result.errorMessage = "未知工具: " + call.toolName;
        result.resultJson = json{{"success", false}, {"message", result.errorMessage}};
        return result;
    }

    json arguments = call.arguments.is_object() ? call.arguments : json::object();
    if (call.toolName == "training_volume_calculator" && !arguments.contains("records"))
    {
        std::string exerciseName = arguments.value("exerciseName", std::string("training"));
        arguments = json{{"records", json::array({
            json{{"exerciseName", exerciseName},
                 {"weightKg", call.arguments["weight"]},
                 {"reps", call.arguments["reps"]},
                 {"sets", call.arguments["sets"]}}
        })}};
    }
    else if (call.toolName == "training_record_query")
    {
        if (!arguments.contains("days"))
        {
            arguments["days"] = 30;
        }
    }

    try
    {
        result.resultJson = service_.callTool(legacy, arguments, userId);
        result.success = result.resultJson.value("success", false);
        if (!result.success)
        {
            result.errorMessage = result.resultJson.value("message", std::string("工具执行失败"));
        }
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.errorMessage = e.what();
        result.resultJson = json{{"success", false}, {"message", result.errorMessage}};
    }
    return result;
}

} // namespace agent
