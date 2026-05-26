#include "../include/handlers/FitnessCalendarHandler.h"

#include "../include/AIUtil/AIHelper.h"
#include "../include/AIUtil/AIFactory.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{

struct ProfileData
{
    json snapshot;
    std::string gender;
    std::string age;
    std::string heightCm;
    std::string weightKg;
    std::string goal;
    std::string trainingLevel;
    int weeklyDays { 4 };
    std::string equipment;
    std::string injuryNote;
};

struct CalendarItem
{
    std::string date;
    std::string itemType;
    std::string title;
    std::string planContent;
    std::string status;
};

struct RecordInput
{
    std::string exerciseName;
    std::string weightKg;
    std::string reps;
    std::string sets;
    std::string rpe;
    std::string rir;
    std::string restSeconds;
    bool completed { false };
};

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

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string jsonValueToString(const json& value)
{
    if (value.is_null())
    {
        return "";
    }
    if (value.is_string())
    {
        return trim(value.get<std::string>());
    }
    if (value.is_boolean())
    {
        return value.get<bool>() ? "1" : "0";
    }
    if (value.is_number_integer())
    {
        return std::to_string(value.get<int>());
    }
    if (value.is_number_float())
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value.get<double>();
        return oss.str();
    }
    return "";
}

std::string getJsonString(const json& body, const std::string& key)
{
    if (!body.contains(key))
    {
        return "";
    }
    return jsonValueToString(body[key]);
}

std::string getJsonStringAny(const json& body, const std::vector<std::string>& keys)
{
    for (const auto& key : keys)
    {
        if (body.contains(key))
        {
            return jsonValueToString(body[key]);
        }
    }
    return "";
}

bool getJsonBool(const json& body, const std::string& key)
{
    if (!body.contains(key) || body[key].is_null())
    {
        return false;
    }
    const auto& value = body[key];
    if (value.is_boolean())
    {
        return value.get<bool>();
    }
    if (value.is_number())
    {
        return value.get<double>() != 0;
    }
    std::string text = toLower(trim(jsonValueToString(value)));
    return text == "1" || text == "true" || text == "yes" || text == "completed";
}

bool parseRequestJson(const http::HttpRequest& req, json& body, std::string& errorMessage,
    bool allowEmpty)
{
    const std::string rawBody = req.getBody();
    if (rawBody.empty())
    {
        if (allowEmpty)
        {
            body = json::object();
            return true;
        }
        errorMessage = "请求体不能为空";
        return false;
    }

    try
    {
        body = json::parse(rawBody);
        if (!body.is_object())
        {
            errorMessage = "请求 JSON 必须是对象";
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        errorMessage = std::string("请求 JSON 格式错误: ") + e.what();
        return false;
    }
}

bool parsePositiveId(const std::string& value, const std::string& label,
    long long& parsed, std::string& errorMessage)
{
    std::string text = trim(value);
    if (text.empty())
    {
        parsed = 0;
        return true;
    }

    try
    {
        size_t pos = 0;
        parsed = std::stoll(text, &pos);
        if (pos != text.size() || parsed <= 0)
        {
            errorMessage = label + "必须是正整数";
            return false;
        }
        return true;
    }
    catch (const std::exception&)
    {
        errorMessage = label + "必须是正整数";
        return false;
    }
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
    tm.tm_min = 0;
    tm.tm_sec = 0;
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

bool validateDateField(const std::string& value, const std::string& label, std::string& errorMessage)
{
    std::tm tm {};
    if (!parseDate(value, tm))
    {
        errorMessage = label + "格式应为 YYYY-MM-DD";
        return false;
    }
    return true;
}

bool validateLength(const std::string& value, size_t maxBytes,
    const std::string& label, std::string& errorMessage)
{
    if (value.size() > maxBytes)
    {
        errorMessage = label + "长度不能超过 " + std::to_string(maxBytes) + " 字节";
        return false;
    }
    return true;
}

bool normalizeIntField(std::string& value, int minValue, int maxValue,
    const std::string& label, std::string& errorMessage)
{
    value = trim(value);
    if (value.empty())
    {
        return true;
    }

    try
    {
        size_t pos = 0;
        int parsed = std::stoi(value, &pos);
        if (pos != value.size())
        {
            errorMessage = label + "必须是整数";
            return false;
        }
        if (parsed < minValue || parsed > maxValue)
        {
            errorMessage = label + "范围应为 " + std::to_string(minValue) + "-" + std::to_string(maxValue);
            return false;
        }
        value = std::to_string(parsed);
        return true;
    }
    catch (const std::exception&)
    {
        errorMessage = label + "必须是整数";
        return false;
    }
}

bool normalizeDecimalField(std::string& value, double minValue, double maxValue,
    const std::string& label, std::string& errorMessage)
{
    value = trim(value);
    if (value.empty())
    {
        return true;
    }

    try
    {
        size_t pos = 0;
        double parsed = std::stod(value, &pos);
        if (pos != value.size())
        {
            errorMessage = label + "必须是数字";
            return false;
        }
        if (parsed < minValue || parsed > maxValue)
        {
            std::ostringstream oss;
            oss << label << "范围应为 " << minValue << "-" << maxValue;
            errorMessage = oss.str();
            return false;
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << parsed;
        value = oss.str();
        return true;
    }
    catch (const std::exception&)
    {
        errorMessage = label + "必须是数字";
        return false;
    }
}

bool normalizeRpeField(std::string& value, const std::string& label, std::string& errorMessage)
{
    return normalizeDecimalField(value, 1.0, 10.0, label, errorMessage);
}

bool normalizeRirField(std::string& value, const std::string& label, std::string& errorMessage)
{
    return normalizeDecimalField(value, 0.0, 10.0, label, errorMessage);
}

std::string nullString(sql::ResultSet* res, const std::string& field)
{
    return res->isNull(field) ? "" : res->getString(field);
}

json calendarJsonFromResult(sql::ResultSet* res)
{
    json item;
    item["id"] = res->getInt64("id");
    item["calendar_date"] = nullString(res, "calendar_date");
    item["item_type"] = nullString(res, "item_type");
    item["title"] = nullString(res, "title");
    item["plan_content"] = nullString(res, "plan_content");
    item["status"] = nullString(res, "status");
    item["model_type"] = nullString(res, "model_type");
    item["created_at"] = nullString(res, "created_at");
    item["updated_at"] = nullString(res, "updated_at");
    return item;
}

json recordJsonFromResult(sql::ResultSet* res)
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

std::string profileDisplay(const std::string& value)
{
    return trim(value).empty() ? "未填写" : value;
}

std::string buildPrompt(const ProfileData& profile, const std::string& startDate)
{
    std::ostringstream oss;
    oss
        << "你是一个谨慎、专业的 AI 私人健身教练。请基于用户档案生成从 "
        << startDate << " 开始的一周训练计划。\n\n"
        << "用户档案：\n"
        << "- 性别：" << profileDisplay(profile.gender) << "\n"
        << "- 年龄：" << profileDisplay(profile.age) << "\n"
        << "- 身高cm：" << profileDisplay(profile.heightCm) << "\n"
        << "- 体重kg：" << profileDisplay(profile.weightKg) << "\n"
        << "- 训练目标：" << profileDisplay(profile.goal) << "\n"
        << "- 训练水平：" << profileDisplay(profile.trainingLevel) << "\n"
        << "- 每周训练天数：" << profile.weeklyDays << "\n"
        << "- 可用器械/训练环境：" << profileDisplay(profile.equipment) << "\n"
        << "- 伤病或限制说明：" << profileDisplay(profile.injuryNote) << "\n\n"
        << "输出要求：\n"
        << "1. 用中文输出，先给总体说明，再给 Day 1 到 Day 7。\n"
        << "2. 每一天必须使用固定标题格式：Day N - YYYY-MM-DD - 标题。\n"
        << "3. 每一天必须包含一行：训练类型：plan 或 训练类型：rest。\n"
        << "4. 训练日写出动作名称、组数、次数或时间、重量策略、RPE/RIR、组间休息、注意事项。\n"
        << "5. 安排热身、拉伸/恢复、渐进超负荷建议和安全提醒。\n"
        << "6. 如果用户是 beginner，不要给过高强度；weekly_days 少时优先全身训练，weekly_days 多时也要安排恢复。\n"
        << "7. 不做医疗诊断；如有伤病、严重疼痛、胸闷或头晕，建议咨询医生或专业人士。\n"
        << "8. 不建议极端节食、危险训练或过度训练。\n\n"
        << "请按以下日期生成 Day 1 到 Day 7：\n";

    for (int i = 0; i < 7; ++i)
    {
        oss << "Day " << (i + 1) << " 日期：" << addDays(startDate, i) << "\n";
    }

    return oss.str();
}

std::string normalizeModelType(std::string modelType)
{
    modelType = trim(modelType);
    if (modelType == "2")
    {
        return "2";
    }
    return "1";
}

std::string inferItemType(const std::string& title, const std::string& content)
{
    std::string text = toLower(title + "\n" + content);
    if (text.find("rest") != std::string::npos || text.find("休息") != std::string::npos)
    {
        return "rest";
    }
    return "plan";
}

std::string cleanDayTitle(const std::string& line, int dayIndex, const std::string& date)
{
    std::string title = line;
    title = std::regex_replace(title, std::regex(R"(^\s*#+\s*)"), "");
    title = std::regex_replace(title, std::regex("Day\\s*" + std::to_string(dayIndex), std::regex::icase), "");
    title = std::regex_replace(title, std::regex(date), "");
    title = std::regex_replace(title, std::regex(R"(^\s*[-:：|]+\s*)"), "");
    title = trim(title);
    return title.empty() ? ("第" + std::to_string(dayIndex) + "天训练安排") : title;
}

std::vector<CalendarItem> parseCalendarItems(const std::string& aiText,
    const std::string& startDate, int weeklyDays)
{
    std::vector<std::string> lines;
    std::istringstream stream(aiText);
    std::string line;
    while (std::getline(stream, line))
    {
        lines.push_back(line);
    }

    std::regex marker(R"(^\s*#+?\s*Day\s*([1-7])\b.*$)", std::regex::icase);
    std::vector<std::pair<int, size_t>> markers;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::smatch match;
        if (std::regex_match(lines[i], match, marker))
        {
            markers.push_back({ std::stoi(match[1].str()), i });
        }
    }

    std::vector<CalendarItem> items(7);
    std::vector<bool> filled(7, false);

    for (size_t i = 0; i < markers.size(); ++i)
    {
        int dayIndex = markers[i].first;
        if (dayIndex < 1 || dayIndex > 7)
        {
            continue;
        }

        size_t begin = markers[i].second;
        size_t end = (i + 1 < markers.size()) ? markers[i + 1].second : lines.size();
        std::ostringstream block;
        for (size_t row = begin; row < end; ++row)
        {
            block << lines[row] << "\n";
        }

        std::string date = addDays(startDate, dayIndex - 1);
        std::string title = cleanDayTitle(lines[begin], dayIndex, date);
        std::string content = trim(block.str());
        std::string itemType = inferItemType(title, content);

        items[dayIndex - 1] = {
            date,
            itemType,
            title,
            content,
            itemType == "rest" ? "rest" : "planned"
        };
        filled[dayIndex - 1] = true;
    }

    int planDays = std::max(1, std::min(weeklyDays <= 0 ? 4 : weeklyDays, 7));
    for (int i = 0; i < 7; ++i)
    {
        if (filled[i])
        {
            continue;
        }

        bool isPlanDay = i < planDays;
        std::string date = addDays(startDate, i);
        items[i] = {
            date,
            isPlanDay ? "plan" : "rest",
            isPlanDay ? ("第" + std::to_string(i + 1) + "天训练计划") : "休息日",
            i == 0 ? aiText : (isPlanDay ? "请参考本周完整计划，按当天状态完成训练并记录动作表现。" : "恢复、拉伸、散步和保证睡眠。"),
            isPlanDay ? "planned" : "rest"
        };
    }

    return items;
}

bool validateRecord(RecordInput& record, std::string& errorMessage)
{
    record.exerciseName = trim(record.exerciseName);
    if (record.exerciseName.empty())
    {
        errorMessage = "动作名称不能为空";
        return false;
    }
    if (!validateLength(record.exerciseName, 100, "动作名称", errorMessage)) return false;
    if (!normalizeDecimalField(record.weightKg, 0.0, 1000.0, "动作重量", errorMessage)) return false;
    if (!normalizeIntField(record.sets, 1, 50, "组数", errorMessage)) return false;
    if (!normalizeIntField(record.reps, 0, 1000, "次数", errorMessage)) return false;
    if (!normalizeRpeField(record.rpe, "RPE", errorMessage)) return false;
    if (!normalizeRirField(record.rir, "RIR", errorMessage)) return false;
    if (!normalizeIntField(record.restSeconds, 0, 3600, "组间休息", errorMessage)) return false;
    if (record.sets.empty())
    {
        record.sets = "1";
    }
    return true;
}

} // namespace

void FitnessCalendarHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    switch (action_)
    {
    case Action::GeneratePlan:
        handleGeneratePlan(req, resp);
        break;
    case Action::ListCalendar:
        handleListCalendar(req, resp);
        break;
    case Action::GetDay:
        handleGetDay(req, resp);
        break;
    case Action::SaveRecord:
        handleSaveRecord(req, resp);
        break;
    case Action::UpdateStatus:
        handleUpdateStatus(req, resp);
        break;
    case Action::ListRecords:
        handleListRecords(req, resp);
        break;
    }
}

bool FitnessCalendarHandler::getLoggedInUserId(const http::HttpRequest& req, http::HttpResponse* resp, int& userId)
{
    auto session = server_->getSessionManager()->getSession(req, resp);
    if (session->getValue("isLoggedIn") != "true")
    {
        json body;
        body["success"] = false;
        body["message"] = "请先登录";
        sendJson(req, resp, http::HttpResponse::k401Unauthorized, "Unauthorized", body, true);
        return false;
    }

    try
    {
        userId = std::stoi(session->getValue("userId"));
    }
    catch (const std::exception&)
    {
        json body;
        body["success"] = false;
        body["message"] = "登录状态异常，请重新登录";
        sendJson(req, resp, http::HttpResponse::k401Unauthorized, "Unauthorized", body, true);
        return false;
    }

    return true;
}

void FitnessCalendarHandler::sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
    http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
    const json& body, bool close)
{
    std::string responseBody = body.dump(4);
    server_->packageResp(req.getVersion(), statusCode, statusMessage, close,
        "application/json", responseBody.size(), responseBody, resp);
}

bool loadProfile(http::MysqlUtil& mysqlUtil, int userId, ProfileData& profile)
{
    std::string sql =
        "SELECT gender, age, height_cm, weight_kg, goal, training_level, weekly_days, equipment, injury_note "
        "FROM fitness_profile WHERE user_id = ? LIMIT 1";
    auto res = mysqlUtil.executeQuery(sql, userId);
    if (!res->next())
    {
        return false;
    }

    profile.gender = nullString(res, "gender");
    profile.age = res->isNull("age") ? "" : std::to_string(res->getInt("age"));
    profile.heightCm = res->isNull("height_cm") ? "" : std::to_string(res->getDouble("height_cm"));
    profile.weightKg = res->isNull("weight_kg") ? "" : std::to_string(res->getDouble("weight_kg"));
    profile.goal = nullString(res, "goal");
    profile.trainingLevel = nullString(res, "training_level");
    profile.weeklyDays = res->isNull("weekly_days") ? 4 : std::max(0, std::min(res->getInt("weekly_days"), 7));
    profile.equipment = nullString(res, "equipment");
    profile.injuryNote = nullString(res, "injury_note");

    profile.snapshot["gender"] = profile.gender;
    profile.snapshot["age"] = profile.age;
    profile.snapshot["height_cm"] = profile.heightCm;
    profile.snapshot["weight_kg"] = profile.weightKg;
    profile.snapshot["goal"] = profile.goal;
    profile.snapshot["training_level"] = profile.trainingLevel;
    profile.snapshot["weekly_days"] = profile.weeklyDays;
    profile.snapshot["equipment"] = profile.equipment;
    profile.snapshot["injury_note"] = profile.injuryNote;
    return true;
}

void upsertCalendarItem(http::MysqlUtil& mysqlUtil, int userId, const CalendarItem& item,
    const std::string& modelType, const std::string& profileSnapshot)
{
    std::string sql =
        "INSERT INTO fitness_calendar "
        "(user_id, calendar_date, item_type, title, plan_content, status, model_type, profile_snapshot) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE "
        "item_type = VALUES(item_type), "
        "title = VALUES(title), "
        "plan_content = VALUES(plan_content), "
        "status = VALUES(status), "
        "model_type = VALUES(model_type), "
        "profile_snapshot = VALUES(profile_snapshot)";

    mysqlUtil.executeUpdate(sql,
        userId,
        std::as_const(item.date),
        std::as_const(item.itemType),
        std::as_const(item.title),
        std::as_const(item.planContent),
        std::as_const(item.status),
        std::as_const(modelType),
        std::as_const(profileSnapshot));
}

long long findCalendarId(http::MysqlUtil& mysqlUtil, int userId, const std::string& date)
{
    std::string sql = "SELECT id FROM fitness_calendar WHERE user_id = ? AND calendar_date = ? LIMIT 1";
    auto res = mysqlUtil.executeQuery(sql, userId, std::as_const(date));
    return res->next() ? res->getInt64("id") : 0;
}

long long ensureCalendarForDate(http::MysqlUtil& mysqlUtil, int userId, const std::string& date,
    const std::string& status = "planned")
{
    long long calendarId = findCalendarId(mysqlUtil, userId, date);
    if (calendarId > 0)
    {
        return calendarId;
    }

    CalendarItem item {
        date,
        status == "rest" ? "rest" : "custom",
        status == "rest" ? "休息日" : "自定义训练记录",
        "",
        status
    };
    std::string empty;
    upsertCalendarItem(mysqlUtil, userId, item, empty, empty);
    return findCalendarId(mysqlUtil, userId, date);
}

json queryCalendarForDate(http::MysqlUtil& mysqlUtil, int userId, const std::string& date)
{
    std::string sql =
        "SELECT id, DATE_FORMAT(calendar_date, '%Y-%m-%d') AS calendar_date, item_type, title, plan_content, "
        "status, model_type, DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at, "
        "DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at "
        "FROM fitness_calendar WHERE user_id = ? AND calendar_date = ? LIMIT 1";
    auto res = mysqlUtil.executeQuery(sql, userId, std::as_const(date));
    if (!res->next())
    {
        return nullptr;
    }
    return calendarJsonFromResult(res);
}

json queryRecordsForDate(http::MysqlUtil& mysqlUtil, int userId, const std::string& date)
{
    std::string sql =
        "SELECT id, calendar_id, DATE_FORMAT(record_date, '%Y-%m-%d') AS record_date, exercise_name, "
        "weight_kg, reps, sets, rpe, rir, rest_seconds, duration_minutes, completed, feeling_note, sort_order "
        "FROM training_record WHERE user_id = ? AND record_date = ? ORDER BY sort_order ASC, id ASC";
    auto res = mysqlUtil.executeQuery(sql, userId, std::as_const(date));
    json records = json::array();
    while (res->next())
    {
        records.push_back(recordJsonFromResult(res));
    }
    return records;
}

json buildDayResponse(http::MysqlUtil& mysqlUtil, int userId, const std::string& date)
{
    json body;
    body["success"] = true;
    body["calendar"] = queryCalendarForDate(mysqlUtil, userId, date);
    body["records"] = queryRecordsForDate(mysqlUtil, userId, date);
    return body;
}

json calendarItemsToJson(const std::vector<CalendarItem>& calendarItems)
{
    json calendar = json::array();
    for (const auto& item : calendarItems)
    {
        json row;
        row["calendar_date"] = item.date;
        row["date"] = item.date;
        row["title"] = item.title;
        row["item_type"] = item.itemType;
        row["status"] = item.status;
        row["plan_content"] = item.planContent;
        calendar.push_back(row);
    }
    return calendar;
}

void FitnessCalendarHandler::handleGeneratePlan(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!getLoggedInUserId(req, resp, userId))
    {
        return;
    }

    try
    {
        json requestBody = json::object();
        std::string errorMessage;
        if (!parseRequestJson(req, requestBody, errorMessage, true))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        std::string startDate = getJsonString(requestBody, "startDate");
        if (startDate.empty())
        {
            startDate = todayDate();
        }

        if (!validateDateField(startDate, "startDate", errorMessage))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        ProfileData profile;
        if (!loadProfile(mysqlUtil_, userId, profile))
        {
            json body;
            body["success"] = false;
            body["message"] = "请先填写我的档案，再生成训练计划";
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, false);
            return;
        }

        std::string modelType = normalizeModelType(getJsonString(requestBody, "modelType"));
        std::string prompt = buildPrompt(profile, startDate);

        auto strategy = StrategyFactory::instance().create(modelType);
        AIHelper helper;
        helper.setStrategy(strategy);
        std::vector<std::pair<std::string, long long>> messages = {{ prompt, 0 }};
        json aiResponse = helper.request(strategy->buildRequest(messages));
        std::string aiText = trim(strategy->parseResponse(aiResponse));
        if (aiText.empty())
        {
            json body;
            body["success"] = false;
            body["message"] = "AI 未返回可用训练计划";
            sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
            return;
        }

        auto calendarItems = parseCalendarItems(aiText, startDate, profile.weeklyDays);
        std::string profileSnapshot = profile.snapshot.dump();
        for (const auto& item : calendarItems)
        {
            upsertCalendarItem(mysqlUtil_, userId, item, modelType, profileSnapshot);
        }

        json calendar = json::array();
        for (const auto& item : calendarItems)
        {
            json row;
            row["calendar_date"] = item.date;
            row["date"] = item.date;
            row["title"] = item.title;
            row["item_type"] = item.itemType;
            row["status"] = item.status;
            row["plan_content"] = item.planContent;
            calendar.push_back(row);
        }

        json body;
        body["success"] = true;
        body["message"] = "训练计划已生成并写入训练日历";
        body["calendar"] = calendar;
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK", body, false);
    }
    catch (const std::exception& e)
    {
        json body;
        body["success"] = false;
        body["message"] = std::string("生成训练计划失败: ") + e.what();
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
    }
}

void FitnessCalendarHandler::handleGeneratePlanStream(const http::HttpRequest& req, http::HttpStreamWriter& writer)
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

    json requestBody = json::object();
    std::string errorMessage;
    if (!parseRequestJson(req, requestBody, errorMessage, true))
    {
        writer.sendError(errorMessage);
        writer.sendDone();
        writer.close();
        return;
    }

    std::string startDate = getJsonString(requestBody, "startDate");
    if (startDate.empty())
    {
        startDate = todayDate();
    }

    if (!validateDateField(startDate, "startDate", errorMessage))
    {
        writer.sendError(errorMessage);
        writer.sendDone();
        writer.close();
        return;
    }

    std::string modelType = normalizeModelType(getJsonString(requestBody, "modelType"));
    writer.sendStatus("accepted");

    std::thread([writer, userId, startDate, modelType]() mutable {
        try
        {
            http::MysqlUtil mysqlUtil;

            writer.sendStatus("reading_profile");
            ProfileData profile;
            if (!loadProfile(mysqlUtil, userId, profile))
            {
                writer.sendError("请先填写我的档案，再生成训练计划");
                writer.sendDone();
                writer.close();
                return;
            }

            writer.sendStatus("calling_ai");
            std::string prompt = buildPrompt(profile, startDate);
            auto strategy = StrategyFactory::instance().create(modelType);
            AIHelper helper;
            helper.setStrategy(strategy);
            std::vector<std::pair<std::string, long long>> messages = {{ prompt, 0 }};

            std::string aiText;
            try
            {
                aiText = helper.requestStream(
                    strategy->buildStreamRequest(messages),
                    [&writer](const std::string& chunk) {
                        writer.sendMessage(chunk);
                    });
            }
            catch (const std::exception&)
            {
                writer.sendStatus("stream_fallback");
            }
            aiText = trim(aiText);

            if (aiText.empty())
            {
                json aiResponse = helper.request(strategy->buildRequest(messages));
                aiText = trim(strategy->parseResponse(aiResponse));
                if (!aiText.empty())
                {
                    writer.sendMessage(aiText);
                }
            }

            if (aiText.empty())
            {
                writer.sendError("AI 未返回可用训练计划");
                writer.sendDone();
                writer.close();
                return;
            }

            writer.sendStatus("writing_calendar");
            auto calendarItems = parseCalendarItems(aiText, startDate, profile.weeklyDays);
            std::string profileSnapshot = profile.snapshot.dump();
            for (const auto& item : calendarItems)
            {
                upsertCalendarItem(mysqlUtil, userId, item, modelType, profileSnapshot);
            }

            writer.sendEvent("calendar", calendarItemsToJson(calendarItems).dump());
            writer.sendStatus("complete");
            writer.sendDone();
        }
        catch (const std::exception& e)
        {
            writer.sendError(std::string("生成训练计划失败: ") + e.what());
            writer.sendDone();
        }
        writer.close();
    }).detach();
}

void FitnessCalendarHandler::handleListCalendar(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!getLoggedInUserId(req, resp, userId))
    {
        return;
    }

    try
    {
        std::string startDate = req.getQueryParameters("start_date");
        std::string endDate = req.getQueryParameters("end_date");
        if (startDate.empty())
        {
            startDate = todayDate();
        }
        if (endDate.empty())
        {
            endDate = addDays(startDate, 6);
        }

        std::string errorMessage;
        if (!validateDateField(startDate, "start_date", errorMessage) ||
            !validateDateField(endDate, "end_date", errorMessage))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        std::string sql =
            "SELECT id, DATE_FORMAT(calendar_date, '%Y-%m-%d') AS calendar_date, item_type, title, plan_content, "
            "status, model_type, DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at, "
            "DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at "
            "FROM fitness_calendar WHERE user_id = ? AND calendar_date BETWEEN ? AND ? "
            "ORDER BY calendar_date ASC";
        auto res = mysqlUtil_.executeQuery(sql, userId, std::as_const(startDate), std::as_const(endDate));

        json calendar = json::array();
        while (res->next())
        {
            calendar.push_back(calendarJsonFromResult(res));
        }

        json body;
        body["success"] = true;
        body["calendar"] = calendar;
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK", body, false);
    }
    catch (const std::exception& e)
    {
        json body;
        body["success"] = false;
        body["message"] = std::string("读取训练日历失败: ") + e.what();
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
    }
}

void FitnessCalendarHandler::handleGetDay(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!getLoggedInUserId(req, resp, userId))
    {
        return;
    }

    try
    {
        std::string date = req.getQueryParameters("date");
        if (date.empty())
        {
            date = todayDate();
        }

        std::string errorMessage;
        if (!validateDateField(date, "date", errorMessage))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        sendJson(req, resp, http::HttpResponse::k200Ok, "OK", buildDayResponse(mysqlUtil_, userId, date), false);
    }
    catch (const std::exception& e)
    {
        json body;
        body["success"] = false;
        body["message"] = std::string("读取当天训练详情失败: ") + e.what();
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
    }
}

void FitnessCalendarHandler::handleSaveRecord(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!getLoggedInUserId(req, resp, userId))
    {
        return;
    }

    try
    {
        json requestBody;
        std::string errorMessage;
        if (!parseRequestJson(req, requestBody, errorMessage, false))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        std::string date = getJsonString(requestBody, "date");
        if (!validateDateField(date, "date", errorMessage))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        std::string durationMinutes = getJsonString(requestBody, "durationMinutes");
        std::string feelingNote = trim(getJsonString(requestBody, "feelingNote"));
        if (!normalizeIntField(durationMinutes, 0, 600, "锻炼时长", errorMessage) ||
            !validateLength(feelingNote, 1000, "本次训练感受", errorMessage))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        if (!requestBody.contains("records") || !requestBody["records"].is_array())
        {
            json body;
            body["success"] = false;
            body["message"] = "records 必须是数组";
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        long long calendarId = 0;
        std::string calendarIdValue = getJsonString(requestBody, "calendarId");
        if (!parsePositiveId(calendarIdValue, "calendarId", calendarId, errorMessage))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        if (calendarId > 0)
        {
            std::string checkSql =
                "SELECT id FROM fitness_calendar WHERE id = ? AND user_id = ? AND calendar_date = ? LIMIT 1";
            auto check = mysqlUtil_.executeQuery(checkSql, calendarId, userId, std::as_const(date));
            if (!check->next())
            {
                json body;
                body["success"] = false;
                body["message"] = "calendarId 不属于当前用户或提交日期";
                sendJson(req, resp, http::HttpResponse::k403Forbidden, "Forbidden", body, true);
                return;
            }
        }
        else
        {
            calendarId = ensureCalendarForDate(mysqlUtil_, userId, date);
        }

        std::vector<RecordInput> records;
        bool hasCompleted = false;
        for (const auto& item : requestBody["records"])
        {
            RecordInput record;
            record.exerciseName = getJsonStringAny(item, {"exerciseName", "exercise_name"});
            record.weightKg = getJsonStringAny(item, {"weightKg", "weight_kg"});
            record.sets = getJsonString(item, "sets");
            record.reps = getJsonString(item, "reps");
            record.rpe = getJsonString(item, "rpe");
            record.rir = getJsonString(item, "rir");
            record.restSeconds = getJsonStringAny(item, {"restSeconds", "rest_seconds"});
            record.completed = getJsonBool(item, "completed");

            if (!validateRecord(record, errorMessage))
            {
                json body;
                body["success"] = false;
                body["message"] = errorMessage;
                sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
                return;
            }
            hasCompleted = hasCompleted || record.completed;
            records.push_back(record);
        }

        std::string deleteSql = "DELETE FROM training_record WHERE user_id = ? AND record_date = ?";
        mysqlUtil_.executeUpdate(deleteSql, userId, std::as_const(date));

        std::string insertSql =
            "INSERT INTO training_record "
            "(user_id, calendar_id, record_date, exercise_name, weight_kg, reps, sets, rpe, rir, "
            "rest_seconds, duration_minutes, completed, feeling_note, sort_order) "
            "VALUES (?, ?, ?, ?, NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), "
            "NULLIF(?, ''), NULLIF(?, ''), ?, ?, ?)";

        for (size_t i = 0; i < records.size(); ++i)
        {
            const auto& record = records[i];
            int completed = record.completed ? 1 : 0;
            int order = static_cast<int>(i);
            mysqlUtil_.executeUpdate(insertSql,
                userId,
                calendarId,
                std::as_const(date),
                std::as_const(record.exerciseName),
                std::as_const(record.weightKg),
                std::as_const(record.reps),
                std::as_const(record.sets),
                std::as_const(record.rpe),
                std::as_const(record.rir),
                std::as_const(record.restSeconds),
                std::as_const(durationMinutes),
                completed,
                std::as_const(feelingNote),
                order);
        }

        std::string status = hasCompleted ? "completed" : "planned";
        std::string updateSql =
            "UPDATE fitness_calendar SET status = ? WHERE user_id = ? AND calendar_date = ?";
        mysqlUtil_.executeUpdate(updateSql, std::as_const(status), userId, std::as_const(date));

        json body = buildDayResponse(mysqlUtil_, userId, date);
        body["message"] = "训练记录保存成功";
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK", body, false);
    }
    catch (const std::exception& e)
    {
        json body;
        body["success"] = false;
        body["message"] = std::string("保存训练记录失败: ") + e.what();
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
    }
}

void FitnessCalendarHandler::handleUpdateStatus(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!getLoggedInUserId(req, resp, userId))
    {
        return;
    }

    try
    {
        json requestBody;
        std::string errorMessage;
        if (!parseRequestJson(req, requestBody, errorMessage, false))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        std::string date = getJsonString(requestBody, "date");
        std::string status = trim(getJsonString(requestBody, "status"));

        if (!validateDateField(date, "date", errorMessage))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        const std::set<std::string> allowed = {"planned", "completed", "skipped", "rest"};
        if (allowed.find(status) == allowed.end())
        {
            json body;
            body["success"] = false;
            body["message"] = "status 只允许 planned/completed/skipped/rest";
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        ensureCalendarForDate(mysqlUtil_, userId, date, status);

        std::string sql =
            "UPDATE fitness_calendar SET status = ?, item_type = IF(? = 'rest', 'rest', item_type) "
            "WHERE user_id = ? AND calendar_date = ?";
        mysqlUtil_.executeUpdate(sql, std::as_const(status), std::as_const(status), userId, std::as_const(date));

        json body = buildDayResponse(mysqlUtil_, userId, date);
        body["message"] = "训练状态已更新";
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK", body, false);
    }
    catch (const std::exception& e)
    {
        json body;
        body["success"] = false;
        body["message"] = std::string("更新训练状态失败: ") + e.what();
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
    }
}

void FitnessCalendarHandler::handleListRecords(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!getLoggedInUserId(req, resp, userId))
    {
        return;
    }

    try
    {
        std::string endDate = req.getQueryParameters("end_date");
        if (endDate.empty())
        {
            endDate = todayDate();
        }
        std::string startDate = req.getQueryParameters("start_date");
        if (startDate.empty())
        {
            startDate = addDays(endDate, -29);
        }

        std::string errorMessage;
        if (!validateDateField(startDate, "start_date", errorMessage) ||
            !validateDateField(endDate, "end_date", errorMessage))
        {
            json body;
            body["success"] = false;
            body["message"] = errorMessage;
            sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
            return;
        }

        std::string sql =
            "SELECT id, calendar_id, DATE_FORMAT(record_date, '%Y-%m-%d') AS record_date, exercise_name, "
            "weight_kg, reps, sets, rpe, rir, rest_seconds, duration_minutes, completed, feeling_note, sort_order "
            "FROM training_record WHERE user_id = ? AND record_date BETWEEN ? AND ? "
            "ORDER BY record_date DESC, sort_order ASC, id ASC";
        auto res = mysqlUtil_.executeQuery(sql, userId, std::as_const(startDate), std::as_const(endDate));

        json records = json::array();
        while (res->next())
        {
            records.push_back(recordJsonFromResult(res));
        }

        json body;
        body["success"] = true;
        body["records"] = records;
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK", body, false);
    }
    catch (const std::exception& e)
    {
        json body;
        body["success"] = false;
        body["message"] = std::string("读取训练记录失败: ") + e.what();
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
    }
}
