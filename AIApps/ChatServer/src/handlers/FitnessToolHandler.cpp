#include "../../include/handlers/FitnessToolHandler.h"

#include "AIApps/ChatServer/include/auth/AIQuotaService.h"
#include "../../include/AIUtil/AIHelper.h"
#include "../../include/AIUtil/AIFactory.h"
#include "../../include/tools/FitnessToolService.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{

std::string trim(const std::string& value)
{
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if (begin >= end)
    {
        return "";
    }
    return std::string(begin, end);
}

bool parseJsonBody(const http::HttpRequest& req, json& body, std::string& message, bool allowEmpty = false)
{
    if (req.getBody().empty())
    {
        if (allowEmpty)
        {
            body = json::object();
            return true;
        }
        message = "请求体不能为空";
        return false;
    }
    try
    {
        body = json::parse(req.getBody());
        if (!body.is_object())
        {
            message = "请求 JSON 必须是对象";
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        message = std::string("请求 JSON 格式错误: ") + e.what();
        return false;
    }
}

std::string jsonString(const json& body, const std::string& key)
{
    if (!body.contains(key) || body[key].is_null())
    {
        return "";
    }
    if (body[key].is_string())
    {
        return trim(body[key].get<std::string>());
    }
    return body[key].dump();
}

int jsonInt(const json& body, const std::string& key, int fallback)
{
    if (!body.contains(key) || body[key].is_null())
    {
        return fallback;
    }
    try
    {
        if (body[key].is_number_integer())
        {
            return body[key].get<int>();
        }
        if (body[key].is_string())
        {
            return std::stoi(body[key].get<std::string>());
        }
    }
    catch (const std::exception&)
    {
    }
    return fallback;
}

std::string nullString(sql::ResultSet* res, const std::string& field)
{
    return res->isNull(field) ? "" : res->getString(field);
}

bool parseDate(const std::string& value, std::tm& out)
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
    if (check.tm_year != tm.tm_year || check.tm_mon != tm.tm_mon || check.tm_mday != tm.tm_mday)
    {
        return false;
    }
    out = tm;
    return true;
}

std::string formatDate(const std::tm& tm)
{
    char buffer[16] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return std::string(buffer);
}

std::string todayDate()
{
    std::time_t now = std::time(nullptr);
    std::tm local = *std::localtime(&now);
    return formatDate(local);
}

std::string addDays(const std::string& date, int days)
{
    std::tm tm {};
    if (!parseDate(date, tm))
    {
        throw std::runtime_error("日期格式应为 YYYY-MM-DD");
    }
    std::time_t t = std::mktime(&tm);
    t += static_cast<std::time_t>(days) * 24 * 60 * 60;
    std::tm local = *std::localtime(&t);
    return formatDate(local);
}

bool validDate(const std::string& date)
{
    std::tm tm {};
    return parseDate(date, tm);
}

json calendarJson(sql::ResultSet* res)
{
    json item;
    item["id"] = res->getInt64("id");
    item["calendar_date"] = nullString(res, "calendar_date");
    item["item_type"] = nullString(res, "item_type");
    item["title"] = nullString(res, "title");
    item["plan_content"] = nullString(res, "plan_content");
    item["status"] = nullString(res, "status");
    item["model_type"] = nullString(res, "model_type");
    return item;
}

json recordJson(sql::ResultSet* res)
{
    json item;
    item["id"] = res->getInt64("id");
    item["calendar_id"] = res->isNull("calendar_id") ? json(nullptr) : json(res->getInt64("calendar_id"));
    item["record_date"] = nullString(res, "record_date");
    item["exercise_name"] = nullString(res, "exercise_name");
    item["weight_kg"] = res->isNull("weight_kg") ? json(nullptr) : json(res->getDouble("weight_kg"));
    item["reps"] = res->isNull("reps") ? json(nullptr) : json(res->getInt("reps"));
    item["sets"] = res->isNull("sets") ? json(nullptr) : json(res->getInt("sets"));
    item["rpe"] = res->isNull("rpe") ? json(nullptr) : json(res->getDouble("rpe"));
    item["rir"] = res->isNull("rir") ? json(nullptr) : json(res->getDouble("rir"));
    item["rest_seconds"] = res->isNull("rest_seconds") ? json(nullptr) : json(res->getInt("rest_seconds"));
    item["duration_minutes"] = res->isNull("duration_minutes") ? json(nullptr) : json(res->getInt("duration_minutes"));
    item["completed"] = !res->isNull("completed") && res->getInt("completed") != 0;
    item["feeling_note"] = nullString(res, "feeling_note");
    item["sort_order"] = res->isNull("sort_order") ? 0 : res->getInt("sort_order");
    return item;
}

std::string normalizeModelType(const std::string& modelType)
{
    return trim(modelType) == "2" ? "2" : "1";
}

std::string callAiSummary(const std::string& prompt, const std::string& modelType)
{
    auto strategy = StrategyFactory::instance().create(normalizeModelType(modelType));
    AIHelper helper;
    helper.setStrategy(strategy);
    std::vector<std::pair<std::string, long long>> messages = {{ prompt, 0 }};
    json response = helper.request(strategy->buildRequest(messages));
    return strategy->parseResponse(response);
}

std::string streamAiPrompt(const std::string& prompt,
    const std::string& modelType,
    AIHelper::StreamCallback onChunk)
{
    auto strategy = StrategyFactory::instance().create(normalizeModelType(modelType));
    AIHelper helper;
    helper.setStrategy(strategy);
    std::vector<std::pair<std::string, long long>> messages = {{ prompt, 0 }};
    return helper.requestStream(strategy->buildStreamRequest(messages), std::move(onChunk));
}

std::string streamPromptWithFallback(http::HttpStreamWriter& writer,
    const std::string& prompt,
    const std::string& modelType)
{
    std::string answer;
    try
    {
        answer = streamAiPrompt(prompt, modelType, [&writer](const std::string& chunk) {
            if (!chunk.empty())
            {
                writer.sendMessage(chunk);
            }
        });
    }
    catch (const std::exception& e)
    {
        writer.sendError(std::string("AI 流式调用失败，正在切换同步回答: ") + e.what());
    }

    if (answer.empty())
    {
        answer = callAiSummary(prompt, modelType);
        if (!answer.empty())
        {
            writer.sendMessage(answer);
        }
    }
    return answer;
}

std::string buildCoachFallbackPrompt(const std::string& question)
{
    std::ostringstream prompt;
    prompt
        << "你是 AI 私人健身教练。\n"
        << "用户问题：\n" << question << "\n\n"
        << "请用中文给出实用、谨慎的健身建议。不要做医疗诊断；如果涉及明显疼痛、伤病、胸闷、头晕或严重不适，建议咨询医生或专业人士。";
    return prompt.str();
}

std::string toolSelectionReason(const std::string& toolName)
{
    if (toolName == "calculate_bmi") return "检测到 BMI 相关问题";
    if (toolName == "calculate_bmr") return "检测到 BMR 或基础代谢相关问题";
    if (toolName == "calculate_tdee") return "检测到 TDEE 或每日消耗相关问题";
    if (toolName == "calculate_calorie_deficit") return "检测到热量缺口或减脂热量相关问题";
    if (toolName == "calculate_training_volume") return "检测到训练容量计算需求";
    if (toolName == "get_today_training_plan") return "用户询问今天该练什么";
    if (toolName == "get_week_training_calendar") return "用户询问本周训练安排";
    if (toolName == "get_recent_training_records") return "用户询问最近训练记录";
    if (toolName == "summarize_recent_training") return "用户询问最近训练总结";
    if (toolName == "save_training_record") return "用户希望记录训练";
    return "根据规则匹配到健身工具";
}

class MysqlFitnessDataProvider : public tools::FitnessDataProvider
{
public:
    explicit MysqlFitnessDataProvider(http::MysqlUtil& mysqlUtil)
        : mysqlUtil_(mysqlUtil)
    {}

    json loadProfile(int userId) override
    {
        std::string sql =
            "SELECT gender, age, height_cm, weight_kg, goal, training_level, weekly_days, equipment, injury_note "
            "FROM fitness_profile WHERE user_id = ? LIMIT 1";
        auto res = mysqlUtil_.executeQuery(sql, userId);
        json body;
        body["success"] = true;
        if (!res->next())
        {
            body["hasProfile"] = false;
            body["profile"] = nullptr;
            return body;
        }

        json profile;
        profile["gender"] = nullString(res, "gender");
        profile["age"] = res->isNull("age") ? json(nullptr) : json(res->getInt("age"));
        profile["height_cm"] = res->isNull("height_cm") ? json(nullptr) : json(res->getDouble("height_cm"));
        profile["weight_kg"] = res->isNull("weight_kg") ? json(nullptr) : json(res->getDouble("weight_kg"));
        profile["goal"] = nullString(res, "goal");
        profile["training_level"] = nullString(res, "training_level");
        profile["weekly_days"] = res->isNull("weekly_days") ? json(nullptr) : json(res->getInt("weekly_days"));
        profile["equipment"] = nullString(res, "equipment");
        profile["injury_note"] = nullString(res, "injury_note");
        body["hasProfile"] = true;
        body["profile"] = profile;
        return body;
    }

    json getTodayTrainingPlan(int userId, const json& args) override
    {
        std::string date = jsonString(args, "date");
        if (date.empty())
        {
            date = todayDate();
        }
        if (!validDate(date))
        {
            return json{{"success", false}, {"message", "date 格式应为 YYYY-MM-DD"}};
        }

        json calendar = nullptr;
        std::string calendarSql =
            "SELECT id, DATE_FORMAT(calendar_date, '%Y-%m-%d') AS calendar_date, item_type, title, plan_content, "
            "status, model_type FROM fitness_calendar WHERE user_id = ? AND calendar_date = ? LIMIT 1";
        auto calRes = mysqlUtil_.executeQuery(calendarSql, userId, std::as_const(date));
        if (calRes->next())
        {
            calendar = calendarJson(calRes);
        }

        json records = readRecords(userId, date, date);
        return json{{"success", true}, {"date", date}, {"hasPlan", !calendar.is_null()},
            {"calendar", calendar}, {"records", records}};
    }

    json getWeekTrainingCalendar(int userId, const json& args) override
    {
        std::string startDate = jsonString(args, "startDate");
        std::string endDate = jsonString(args, "endDate");
        if (startDate.empty())
        {
            startDate = todayDate();
        }
        if (endDate.empty())
        {
            endDate = addDays(startDate, 6);
        }
        if (!validDate(startDate) || !validDate(endDate))
        {
            return json{{"success", false}, {"message", "startDate/endDate 格式应为 YYYY-MM-DD"}};
        }

        std::string sql =
            "SELECT id, DATE_FORMAT(calendar_date, '%Y-%m-%d') AS calendar_date, item_type, title, plan_content, "
            "status, model_type FROM fitness_calendar WHERE user_id = ? AND calendar_date BETWEEN ? AND ? "
            "ORDER BY calendar_date ASC";
        auto res = mysqlUtil_.executeQuery(sql, userId, std::as_const(startDate), std::as_const(endDate));
        json calendar = json::array();
        while (res->next())
        {
            calendar.push_back(calendarJson(res));
        }
        return json{{"success", true}, {"startDate", startDate}, {"endDate", endDate}, {"calendar", calendar}};
    }

    json getRecentTrainingRecords(int userId, const json& args) override
    {
        int days = jsonInt(args, "days", 30);
        days = std::max(1, std::min(days, 90));
        std::string endDate = todayDate();
        std::string startDate = addDays(endDate, -(days - 1));
        return json{{"success", true}, {"days", days}, {"records", readRecords(userId, startDate, endDate)}};
    }

    json saveTrainingRecord(int userId, const json& args) override
    {
        std::string date = jsonString(args, "date");
        if (!validDate(date))
        {
            return json{{"success", false}, {"message", "date 格式应为 YYYY-MM-DD"}};
        }
        if (!args.contains("records") || !args["records"].is_array())
        {
            return json{{"success", false}, {"message", "records 必须是数组"}};
        }

        int durationMinutes = jsonInt(args, "durationMinutes", 0);
        if (durationMinutes < 0 || durationMinutes > 600)
        {
            return json{{"success", false}, {"message", "durationMinutes 范围应为 0-600"}};
        }
        std::string feelingNote = jsonString(args, "feelingNote");
        if (feelingNote.size() > 1000)
        {
            return json{{"success", false}, {"message", "feelingNote 长度不能超过 1000 字节"}};
        }

        long long calendarId = ensureCalendarForDate(userId, date);
        std::string deleteSql = "DELETE FROM training_record WHERE user_id = ? AND record_date = ?";
        mysqlUtil_.executeUpdate(deleteSql, userId, std::as_const(date));

        std::string insertSql =
            "INSERT INTO training_record "
            "(user_id, calendar_id, record_date, exercise_name, weight_kg, reps, sets, rpe, rir, "
            "rest_seconds, duration_minutes, completed, feeling_note, sort_order) "
            "VALUES (?, ?, ?, ?, NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), "
            "NULLIF(?, ''), NULLIF(?, ''), ?, ?, ?)";

        bool hasCompleted = false;
        int order = 0;
        for (const auto& item : args["records"])
        {
            std::string exerciseName = jsonString(item, "exerciseName");
            if (exerciseName.empty())
            {
                exerciseName = jsonString(item, "exercise_name");
            }
            if (exerciseName.empty())
            {
                return json{{"success", false}, {"message", "动作名称不能为空"}};
            }
            std::string weightKg = valueToString(item, {"weightKg", "weight_kg"});
            std::string reps = valueToString(item, {"reps"});
            std::string sets = valueToString(item, {"sets"});
            std::string rpe = valueToString(item, {"rpe"});
            std::string rir = valueToString(item, {"rir"});
            std::string restSeconds = valueToString(item, {"restSeconds", "rest_seconds"});
            bool completed = item.contains("completed") && item["completed"].is_boolean() && item["completed"].get<bool>();
            hasCompleted = hasCompleted || completed;
            int completedValue = completed ? 1 : 0;
            std::string durationText = durationMinutes == 0 ? "" : std::to_string(durationMinutes);
            mysqlUtil_.executeUpdate(insertSql,
                userId,
                calendarId,
                std::as_const(date),
                std::as_const(exerciseName),
                std::as_const(weightKg),
                std::as_const(reps),
                std::as_const(sets),
                std::as_const(rpe),
                std::as_const(rir),
                std::as_const(restSeconds),
                std::as_const(durationText),
                completedValue,
                std::as_const(feelingNote),
                order++);
        }

        std::string status = hasCompleted ? "completed" : "planned";
        std::string updateSql = "UPDATE fitness_calendar SET status = ? WHERE user_id = ? AND calendar_date = ?";
        mysqlUtil_.executeUpdate(updateSql, std::as_const(status), userId, std::as_const(date));

        return getTodayTrainingPlan(userId, json{{"date", date}});
    }

private:
    json readRecords(int userId, const std::string& startDate, const std::string& endDate)
    {
        std::string sql =
            "SELECT id, calendar_id, DATE_FORMAT(record_date, '%Y-%m-%d') AS record_date, exercise_name, "
            "weight_kg, reps, sets, rpe, rir, rest_seconds, duration_minutes, completed, feeling_note, sort_order "
            "FROM training_record WHERE user_id = ? AND record_date BETWEEN ? AND ? "
            "ORDER BY record_date DESC, sort_order ASC, id ASC";
        auto res = mysqlUtil_.executeQuery(sql, userId, std::as_const(startDate), std::as_const(endDate));
        json records = json::array();
        while (res->next())
        {
            records.push_back(recordJson(res));
        }
        return records;
    }

    long long ensureCalendarForDate(int userId, const std::string& date)
    {
        std::string selectSql = "SELECT id FROM fitness_calendar WHERE user_id = ? AND calendar_date = ? LIMIT 1";
        auto res = mysqlUtil_.executeQuery(selectSql, userId, std::as_const(date));
        if (res->next())
        {
            return res->getInt64("id");
        }

        std::string insertSql =
            "INSERT INTO fitness_calendar "
            "(user_id, calendar_date, item_type, title, plan_content, status, model_type, profile_snapshot) "
            "VALUES (?, ?, 'custom', '自定义训练记录', '通过工具调用创建的训练记录。', 'planned', '', '{}')";
        mysqlUtil_.executeUpdate(insertSql, userId, std::as_const(date));
        auto inserted = mysqlUtil_.executeQuery(selectSql, userId, std::as_const(date));
        if (inserted->next())
        {
            return inserted->getInt64("id");
        }
        throw std::runtime_error("创建训练日历记录失败");
    }

    std::string valueToString(const json& item, const std::vector<std::string>& keys)
    {
        for (const auto& key : keys)
        {
            if (!item.contains(key) || item[key].is_null())
            {
                continue;
            }
            if (item[key].is_string())
            {
                return item[key].get<std::string>();
            }
            if (item[key].is_number_integer())
            {
                return std::to_string(item[key].get<int>());
            }
            if (item[key].is_number_float())
            {
                std::ostringstream oss;
                oss << item[key].get<double>();
                return oss.str();
            }
        }
        return "";
    }

    http::MysqlUtil& mysqlUtil_;
};

json toolsToJson(const std::vector<tools::FitnessToolDefinition>& definitions)
{
    json rows = json::array();
    for (const auto& definition : definitions)
    {
        rows.push_back({
            {"name", definition.name},
            {"description", definition.description},
            {"inputSchema", definition.inputSchema},
            {"requiresUserData", definition.requiresUserData}
        });
    }
    return rows;
}

} // namespace

void FitnessToolHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    switch (action_)
    {
    case Action::ListTools:
        handleListTools(req, resp);
        break;
    case Action::CallTool:
        handleCallTool(req, resp);
        break;
    case Action::ChatToolSend:
        handleChatToolSend(req, resp);
        break;
    }
}

void FitnessToolHandler::handleListTools(const http::HttpRequest& req, http::HttpResponse* resp)
{
    tools::FitnessToolService service;
    sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
        json{{"success", true}, {"tools", toolsToJson(service.listTools())}}, false);
}

void FitnessToolHandler::handleCallTool(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!requireLogin(req, resp, userId))
    {
        return;
    }

    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    std::string toolName = jsonString(requestBody, "toolName");
    json arguments = requestBody.contains("arguments") && requestBody["arguments"].is_object()
        ? requestBody["arguments"]
        : json::object();

    MysqlFitnessDataProvider provider(mysqlUtil_);
    tools::FitnessToolService service(&provider);
    json result = service.callTool(toolName, arguments, userId);
    bool success = result.value("success", false);
    json body;
    body["success"] = success;
    body["toolName"] = toolName;
    body["result"] = result;
    if (!success && result.contains("message"))
    {
        body["message"] = result["message"];
    }
    sendJson(req, resp,
        success ? http::HttpResponse::k200Ok : http::HttpResponse::k400BadRequest,
        success ? "OK" : "Bad Request",
        body,
        !success);
}

void FitnessToolHandler::handleChatToolSend(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!requireLogin(req, resp, userId))
    {
        return;
    }

    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    std::string question = jsonString(requestBody, "question");
    if (question.empty())
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", "question 不能为空"}}, true);
        return;
    }

    MysqlFitnessDataProvider provider(mysqlUtil_);
    tools::FitnessToolService service(&provider);
    std::string toolName = service.matchToolName(question);
    if (toolName.empty())
    {
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"answer", "这次问题没有匹配到健身工具，请使用 AI 教练对话继续咨询。"},
                 {"toolName", ""}, {"toolResult", json::object()}},
            false);
        return;
    }

    if (toolName == "save_training_record" && !requestBody.contains("arguments"))
    {
        json toolResult = {
            {"success", false},
            {"message", "自然语言记录解析本轮先不做复杂抽取，请在训练记录表单中保存，或使用 /fitness/tool/call 传结构化 records"}
        };
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"answer", toolResult["message"]}, {"toolName", toolName}, {"toolResult", toolResult}},
            false);
        return;
    }

    json arguments = requestBody.contains("arguments") && requestBody["arguments"].is_object()
        ? requestBody["arguments"]
        : json::object();
    json toolResult = service.callTool(toolName, arguments, userId);

    std::string modelType = jsonString(requestBody, "modelType");
    std::string answer;
    if (!toolResult.value("success", false))
    {
        answer = toolResult.value("message", "工具调用失败");
    }
    else
    {
        auth::AIQuotaService quotaService;
        auto quotaCheck = quotaService.checkBeforeAI(userId);
        if (!quotaCheck.allowed)
        {
            sendJson(req, resp,
                quotaCheck.systemError ? http::HttpResponse::k500InternalServerError : http::HttpResponse::k403Forbidden,
                quotaCheck.systemError ? "Internal Server Error" : "Forbidden",
                json{{"success", false}, {"message", quotaCheck.message}},
                true);
            return;
        }

        try
        {
            answer = callAiSummary(
                service.buildToolSummaryPrompt(question, toolName, toolResult),
                modelType);
            if (answer.empty())
            {
                quotaService.logAIUsage(userId, "/chat/fitness-tool-send", modelType,
                    false, false, "AI returned empty tool summary");
                answer = "AI 未返回可用回答";
            }
            else
            {
                quotaService.consumeQuotaOnSuccess(userId, "/chat/fitness-tool-send", modelType);
            }
        }
        catch (const std::exception& e)
        {
            quotaService.logAIUsage(userId, "/chat/fitness-tool-send", modelType,
                false, false, e.what());
            answer = std::string("工具调用已完成，但 AI 总结失败: ") + e.what() + "\n\n工具结果：\n" + toolResult.dump(2);
        }
    }

    sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
        json{{"success", true}, {"answer", answer}, {"toolName", toolName}, {"toolResult", toolResult}},
        false);
}

void FitnessToolHandler::handleChatToolSendStream(const http::HttpRequest& req, http::HttpStreamWriter& writer)
{
    writer.sendSseHeader();

    http::HttpResponse sessionResp(false);
    auto session = server_->getSessionManager()->getSession(req, &sessionResp);
    if (session->getValue("isLoggedIn") != "true")
    {
        writer.sendError("请先登录");
        writer.sendDone();
        writer.close();
        return;
    }

    int userId = 0;
    try
    {
        userId = std::stoi(session->getValue("userId"));
    }
    catch (const std::exception&)
    {
        writer.sendError("登录状态异常，请重新登录");
        writer.sendDone();
        writer.close();
        return;
    }

    json requestBody;
    std::string errorMessage;
    if (!parseJsonBody(req, requestBody, errorMessage))
    {
        writer.sendError(errorMessage);
        writer.sendDone();
        writer.close();
        return;
    }

    std::string question = jsonString(requestBody, "question");
    if (question.empty())
    {
        writer.sendError("question 不能为空");
        writer.sendDone();
        writer.close();
        return;
    }

    std::string modelType = jsonString(requestBody, "modelType");
    writer.sendStatus("正在分析你的问题");

    std::thread([writer, requestBody, question, modelType, userId]() mutable {
        auth::AIQuotaService quotaService;
        bool aiCallStarted = false;
        bool quotaFinalized = false;
        try
        {
            http::MysqlUtil mysqlUtil;
            MysqlFitnessDataProvider provider(mysqlUtil);
            tools::FitnessToolService service(&provider);

            std::string toolName = service.matchToolName(question);
            if (toolName.empty())
            {
                writer.sendStatus("未匹配到工具，切换为普通 AI 教练回答");
                auto quotaCheck = quotaService.checkBeforeAI(userId);
                if (!quotaCheck.allowed)
                {
                    writer.sendError(quotaCheck.message);
                    writer.sendDone();
                    writer.close();
                    return;
                }

                aiCallStarted = true;
                std::string answer = streamPromptWithFallback(writer, buildCoachFallbackPrompt(question), modelType);
                if (answer.empty())
                {
                    quotaService.logAIUsage(userId, "/chat/fitness-tool-send-stream", modelType,
                        false, false, "AI returned empty tool fallback answer");
                    quotaFinalized = true;
                    writer.sendError("AI 未返回可用回答");
                }
                else
                {
                    quotaService.consumeQuotaOnSuccess(userId, "/chat/fitness-tool-send-stream", modelType);
                    quotaFinalized = true;
                }
                writer.sendDone();
                writer.close();
                return;
            }

            writer.sendEvent("tool_selected",
                json{{"toolName", toolName}, {"reason", toolSelectionReason(toolName)}}.dump());

            if (toolName == "save_training_record" && !requestBody.contains("arguments"))
            {
                writer.sendError("自然语言记录解析本轮先不做复杂抽取，请在训练记录表单中保存，或使用 /fitness/tool/call 传结构化 records");
                writer.sendDone();
                writer.close();
                return;
            }

            json arguments = requestBody.contains("arguments") && requestBody["arguments"].is_object()
                ? requestBody["arguments"]
                : json::object();
            json toolResult = service.callTool(toolName, arguments, userId);
            if (!toolResult.value("success", false))
            {
                writer.sendError(toolResult.value("message", "工具调用失败"));
                writer.sendDone();
                writer.close();
                return;
            }

            writer.sendEvent("tool_result", json{{"toolName", toolName}, {"result", toolResult}}.dump());
            writer.sendStatus("正在基于工具结果生成回答");

            auto quotaCheck = quotaService.checkBeforeAI(userId);
            if (!quotaCheck.allowed)
            {
                writer.sendError(quotaCheck.message);
                writer.sendDone();
                writer.close();
                return;
            }

            std::string prompt = service.buildToolSummaryPrompt(question, toolName, toolResult);
            aiCallStarted = true;
            std::string answer = streamPromptWithFallback(writer, prompt, modelType);
            if (answer.empty())
            {
                quotaService.logAIUsage(userId, "/chat/fitness-tool-send-stream", modelType,
                    false, false, "AI returned empty tool summary");
                quotaFinalized = true;
                writer.sendError("AI 未返回可用回答");
            }
            else
            {
                quotaService.consumeQuotaOnSuccess(userId, "/chat/fitness-tool-send-stream", modelType);
                quotaFinalized = true;
            }
            writer.sendDone();
        }
        catch (const std::exception& e)
        {
            if (aiCallStarted && !quotaFinalized)
            {
                quotaService.logAIUsage(userId, "/chat/fitness-tool-send-stream", modelType,
                    false, false, e.what());
            }
            writer.sendError(std::string("健身工具流式问答失败: ") + e.what());
            writer.sendDone();
        }
        writer.close();
    }).detach();
}

bool FitnessToolHandler::requireLogin(const http::HttpRequest& req, http::HttpResponse* resp, int& userId)
{
    auto session = server_->getSessionManager()->getSession(req, resp);
    if (session->getValue("isLoggedIn") != "true")
    {
        sendJson(req, resp, http::HttpResponse::k401Unauthorized, "Unauthorized",
            json{{"success", false}, {"message", "请先登录"}}, true);
        return false;
    }
    try
    {
        userId = std::stoi(session->getValue("userId"));
    }
    catch (const std::exception&)
    {
        sendJson(req, resp, http::HttpResponse::k401Unauthorized, "Unauthorized",
            json{{"success", false}, {"message", "登录状态异常，请重新登录"}}, true);
        return false;
    }
    return true;
}

void FitnessToolHandler::sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
    http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
    const json& body, bool close)
{
    std::string responseBody = body.dump(4);
    server_->packageResp(req.getVersion(), statusCode, statusMessage, close,
        "application/json", responseBody.size(), responseBody, resp);
}
