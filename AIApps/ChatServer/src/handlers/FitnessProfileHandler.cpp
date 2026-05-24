#include "../include/handlers/FitnessProfileHandler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace
{

struct FitnessProfileData
{
    std::string gender;
    std::string age;
    std::string heightCm;
    std::string weightKg;
    std::string goal;
    std::string trainingLevel;
    std::string weeklyDays;
    std::string equipment;
    std::string injuryNote;
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

std::string getJsonField(const json& body, const std::string& key)
{
    if (!body.contains(key))
    {
        return "";
    }
    return jsonValueToString(body[key]);
}

int fromHex(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::string urlDecode(const std::string& value)
{
    std::string decoded;
    decoded.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '+')
        {
            decoded.push_back(' ');
        }
        else if (value[i] == '%' && i + 2 < value.size())
        {
            int high = fromHex(value[i + 1]);
            int low = fromHex(value[i + 2]);
            if (high >= 0 && low >= 0)
            {
                decoded.push_back(static_cast<char>(high * 16 + low));
                i += 2;
            }
            else
            {
                decoded.push_back(value[i]);
            }
        }
        else
        {
            decoded.push_back(value[i]);
        }
    }

    return decoded;
}

std::map<std::string, std::string> parseFormBody(const std::string& body)
{
    std::map<std::string, std::string> fields;
    size_t start = 0;

    while (start <= body.size())
    {
        size_t end = body.find('&', start);
        if (end == std::string::npos)
        {
            end = body.size();
        }

        std::string pair = body.substr(start, end - start);
        size_t eq = pair.find('=');
        if (eq != std::string::npos)
        {
            fields[urlDecode(pair.substr(0, eq))] = trim(urlDecode(pair.substr(eq + 1)));
        }

        if (end == body.size())
        {
            break;
        }
        start = end + 1;
    }

    return fields;
}

std::string getFormField(const std::map<std::string, std::string>& fields, const std::string& key)
{
    auto it = fields.find(key);
    return it == fields.end() ? "" : it->second;
}

bool parseProfileData(const http::HttpRequest& req, FitnessProfileData& data, std::string& errorMessage)
{
    const std::string body = req.getBody();
    if (body.empty())
    {
        errorMessage = "请求体不能为空";
        return false;
    }

    const std::string contentType = req.getHeader("Content-Type");
    try
    {
        if (contentType.find("application/x-www-form-urlencoded") != std::string::npos)
        {
            auto fields = parseFormBody(body);
            data.gender = getFormField(fields, "gender");
            data.age = getFormField(fields, "age");
            data.heightCm = getFormField(fields, "height_cm");
            data.weightKg = getFormField(fields, "weight_kg");
            data.goal = getFormField(fields, "goal");
            data.trainingLevel = getFormField(fields, "training_level");
            data.weeklyDays = getFormField(fields, "weekly_days");
            data.equipment = getFormField(fields, "equipment");
            data.injuryNote = getFormField(fields, "injury_note");
        }
        else
        {
            auto parsed = json::parse(body);
            data.gender = getJsonField(parsed, "gender");
            data.age = getJsonField(parsed, "age");
            data.heightCm = getJsonField(parsed, "height_cm");
            data.weightKg = getJsonField(parsed, "weight_kg");
            data.goal = getJsonField(parsed, "goal");
            data.trainingLevel = getJsonField(parsed, "training_level");
            data.weeklyDays = getJsonField(parsed, "weekly_days");
            data.equipment = getJsonField(parsed, "equipment");
            data.injuryNote = getJsonField(parsed, "injury_note");
        }
    }
    catch (const std::exception& e)
    {
        errorMessage = std::string("请求数据格式错误: ") + e.what();
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
        if (!std::isfinite(parsed))
        {
            errorMessage = label + "必须是有效数字";
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

bool validateProfileData(FitnessProfileData& data, std::string& errorMessage)
{
    data.gender = trim(data.gender);
    data.goal = trim(data.goal);
    data.trainingLevel = trim(data.trainingLevel);
    data.equipment = trim(data.equipment);
    data.injuryNote = trim(data.injuryNote);

    if (!validateLength(data.gender, 20, "性别", errorMessage)) return false;
    if (!validateLength(data.goal, 100, "训练目标", errorMessage)) return false;
    if (!validateLength(data.trainingLevel, 50, "训练水平", errorMessage)) return false;
    if (!validateLength(data.equipment, 255, "可用器械/训练环境", errorMessage)) return false;
    if (!validateLength(data.injuryNote, 1000, "伤病或限制说明", errorMessage)) return false;

    if (!normalizeIntField(data.age, 10, 100, "年龄", errorMessage)) return false;
    if (!normalizeDecimalField(data.heightCm, 80.0, 250.0, "身高", errorMessage)) return false;
    if (!normalizeDecimalField(data.weightKg, 20.0, 300.0, "体重", errorMessage)) return false;
    if (!normalizeIntField(data.weeklyDays, 0, 7, "每周训练天数", errorMessage)) return false;

    return true;
}

} // namespace

void FitnessProfileHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    if (action_ == Action::GetProfile)
    {
        handleGetProfile(req, resp);
        return;
    }

    handleSaveProfile(req, resp);
}

bool FitnessProfileHandler::getLoggedInUserId(const http::HttpRequest& req, http::HttpResponse* resp, int& userId)
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

void FitnessProfileHandler::handleGetProfile(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!getLoggedInUserId(req, resp, userId))
    {
        return;
    }

    try
    {
        std::string sql =
            "SELECT gender, age, height_cm, weight_kg, goal, training_level, "
            "weekly_days, equipment, injury_note "
            "FROM fitness_profile WHERE user_id = ? LIMIT 1";

        auto res = mysqlUtil_.executeQuery(sql, userId);

        json body;
        body["success"] = true;
        if (!res->next())
        {
            body["hasProfile"] = false;
            body["profile"] = nullptr;
        }
        else
        {
            json profile;
            profile["gender"] = res->isNull("gender") ? "" : res->getString("gender");
            profile["age"] = res->isNull("age") ? json(nullptr) : json(res->getInt("age"));
            profile["height_cm"] = res->isNull("height_cm") ? json(nullptr) : json(res->getDouble("height_cm"));
            profile["weight_kg"] = res->isNull("weight_kg") ? json(nullptr) : json(res->getDouble("weight_kg"));
            profile["goal"] = res->isNull("goal") ? "" : res->getString("goal");
            profile["training_level"] = res->isNull("training_level") ? "" : res->getString("training_level");
            profile["weekly_days"] = res->isNull("weekly_days") ? json(nullptr) : json(res->getInt("weekly_days"));
            profile["equipment"] = res->isNull("equipment") ? "" : res->getString("equipment");
            profile["injury_note"] = res->isNull("injury_note") ? "" : res->getString("injury_note");

            body["hasProfile"] = true;
            body["profile"] = profile;
        }

        sendJson(req, resp, http::HttpResponse::k200Ok, "OK", body, false);
    }
    catch (const std::exception& e)
    {
        json body;
        body["success"] = false;
        body["message"] = std::string("读取健身档案失败，请确认 fitness_profile 表已初始化: ") + e.what();
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
    }
}

void FitnessProfileHandler::handleSaveProfile(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!getLoggedInUserId(req, resp, userId))
    {
        return;
    }

    FitnessProfileData data;
    std::string errorMessage;
    if (!parseProfileData(req, data, errorMessage) || !validateProfileData(data, errorMessage))
    {
        json body;
        body["success"] = false;
        body["message"] = errorMessage;
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request", body, true);
        return;
    }

    try
    {
        std::string sql =
            "INSERT INTO fitness_profile "
            "(user_id, gender, age, height_cm, weight_kg, goal, training_level, weekly_days, equipment, injury_note) "
            "VALUES (?, ?, NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), ?, ?, NULLIF(?, ''), ?, ?) "
            "ON DUPLICATE KEY UPDATE "
            "gender = VALUES(gender), "
            "age = VALUES(age), "
            "height_cm = VALUES(height_cm), "
            "weight_kg = VALUES(weight_kg), "
            "goal = VALUES(goal), "
            "training_level = VALUES(training_level), "
            "weekly_days = VALUES(weekly_days), "
            "equipment = VALUES(equipment), "
            "injury_note = VALUES(injury_note)";

        mysqlUtil_.executeUpdate(sql,
            userId,
            data.gender,
            data.age,
            data.heightCm,
            data.weightKg,
            data.goal,
            data.trainingLevel,
            data.weeklyDays,
            data.equipment,
            data.injuryNote);

        json body;
        body["success"] = true;
        body["message"] = "健身档案保存成功";
        sendJson(req, resp, http::HttpResponse::k200Ok, "OK", body, false);
    }
    catch (const std::exception& e)
    {
        json body;
        body["success"] = false;
        body["message"] = std::string("保存健身档案失败，请确认 fitness_profile 表已初始化: ") + e.what();
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error", body, true);
    }
}

void FitnessProfileHandler::sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
    http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
    const json& body, bool close)
{
    std::string responseBody = body.dump(4);
    server_->packageResp(req.getVersion(), statusCode, statusMessage, close,
        "application/json", responseBody.size(), responseBody, resp);
}
