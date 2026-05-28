#include "../include/handlers/ExerciseLibraryHandler.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace
{

std::string jsonValueToString(const json& value)
{
    if (value.is_null())
    {
        return "";
    }
    if (value.is_string())
    {
        return fitness::trimExerciseText(value.get<std::string>());
    }
    if (value.is_number_integer())
    {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_float())
    {
        return std::to_string(value.get<double>());
    }
    if (value.is_boolean())
    {
        return value.get<bool>() ? "true" : "false";
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

std::string getJsonStringAny(const json& body, const std::string& firstKey, const std::string& secondKey)
{
    if (body.contains(firstKey))
    {
        return jsonValueToString(body[firstKey]);
    }
    if (body.contains(secondKey))
    {
        return jsonValueToString(body[secondKey]);
    }
    return "";
}

bool parseJsonBody(const http::HttpRequest& req, json& body, std::string& errorMessage)
{
    const std::string rawBody = req.getBody();
    if (rawBody.empty())
    {
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

bool parsePositiveId(const json& body, long long& id, std::string& errorMessage)
{
    std::string text = getJsonString(body, "id");
    text = fitness::trimExerciseText(text);
    if (text.empty())
    {
        errorMessage = "id 不能为空";
        return false;
    }

    try
    {
        size_t pos = 0;
        id = std::stoll(text, &pos);
        if (pos != text.size() || id <= 0)
        {
            errorMessage = "id 必须是正整数";
            return false;
        }
        return true;
    }
    catch (const std::exception&)
    {
        errorMessage = "id 必须是正整数";
        return false;
    }
}

fitness::ExerciseInput exerciseInputFromJson(const json& body)
{
    fitness::ExerciseInput input;
    input.name = getJsonString(body, "name");
    input.category = getJsonString(body, "category");
    input.primaryMuscle = getJsonStringAny(body, "primaryMuscle", "primary_muscle");
    input.secondaryMuscles = getJsonStringAny(body, "secondaryMuscles", "secondary_muscles");
    input.equipment = getJsonString(body, "equipment");
    input.difficulty = getJsonString(body, "difficulty");
    input.description = getJsonString(body, "description");
    input.tips = getJsonString(body, "tips");
    fitness::normalizeExerciseInput(input);
    return input;
}

json exerciseJsonFromResult(sql::ResultSet* res)
{
    json row;
    row["id"] = res->getInt64("id");
    row["name"] = res->isNull("name") ? "" : res->getString("name");
    row["category"] = res->isNull("category") ? "" : res->getString("category");
    row["primaryMuscle"] = res->isNull("primary_muscle") ? "" : res->getString("primary_muscle");
    row["secondaryMuscles"] = res->isNull("secondary_muscles") ? "" : res->getString("secondary_muscles");
    row["equipment"] = res->isNull("equipment") ? "" : res->getString("equipment");
    row["difficulty"] = res->isNull("difficulty") ? "" : res->getString("difficulty");
    row["description"] = res->isNull("description") ? "" : res->getString("description");
    row["tips"] = res->isNull("tips") ? "" : res->getString("tips");
    row["isSystem"] = !res->isNull("is_system") && res->getInt("is_system") != 0;
    return row;
}

std::string selectExerciseSql()
{
    return "SELECT id, user_id, name, category, primary_muscle, secondary_muscles, "
           "equipment, difficulty, description, tips, is_system "
           "FROM exercise_library ";
}

bool toIncludeCustom(const std::string& value)
{
    std::string text = value;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    text = fitness::trimExerciseText(text);
    return !(text == "0" || text == "false" || text == "no");
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

std::string queryParam(const http::HttpRequest& req, const std::string& key)
{
    return fitness::trimExerciseText(urlDecode(req.getQueryParameters(key)));
}

} // namespace

void ExerciseLibraryHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    switch (action_)
    {
    case Action::List:
        handleList(req, resp);
        break;
    case Action::Create:
        handleCreate(req, resp);
        break;
    case Action::Update:
        handleUpdate(req, resp);
        break;
    case Action::Delete:
        handleDelete(req, resp);
        break;
    }
}

bool ExerciseLibraryHandler::requireLogin(const http::HttpRequest& req, http::HttpResponse* resp, int& userId)
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

void ExerciseLibraryHandler::handleList(const http::HttpRequest& req, http::HttpResponse* resp)
{
    int userId = 0;
    if (!requireLogin(req, resp, userId))
    {
        return;
    }

    try
    {
        std::string keyword = queryParam(req, "keyword");
        std::string category = queryParam(req, "category");
        std::string primaryMuscle = queryParam(req, "primary_muscle");
        if (primaryMuscle.empty())
        {
            primaryMuscle = queryParam(req, "primaryMuscle");
        }
        int includeCustom = toIncludeCustom(queryParam(req, "include_custom")) ? 1 : 0;

        std::string sql = selectExerciseSql() +
            "WHERE ((is_system = 1 AND user_id = 0) OR (? = 1 AND is_system = 0 AND user_id = ?)) "
            "AND (? = '' OR name LIKE CONCAT('%', ?, '%') OR description LIKE CONCAT('%', ?, '%')) "
            "AND (? = '' OR category = ?) "
            "AND (? = '' OR primary_muscle = ?) "
            "ORDER BY is_system DESC, category ASC, name ASC";

        auto res = mysqlUtil_.executeQuery(sql,
            includeCustom,
            userId,
            std::as_const(keyword),
            std::as_const(keyword),
            std::as_const(keyword),
            std::as_const(category),
            std::as_const(category),
            std::as_const(primaryMuscle),
            std::as_const(primaryMuscle));

        json exercises = json::array();
        while (res->next())
        {
            long long exerciseUserId = res->isNull("user_id") ? 0 : res->getInt64("user_id");
            bool isSystem = !res->isNull("is_system") && res->getInt("is_system") != 0;
            if (fitness::canReadExercise(exerciseUserId, isSystem, userId))
            {
                exercises.push_back(exerciseJsonFromResult(res));
            }
        }

        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"exercises", exercises}}, false);
    }
    catch (const std::exception& e)
    {
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false}, {"message", std::string("读取动作库失败，请确认 exercise_library 表已初始化: ") + e.what()}},
            true);
    }
}

void ExerciseLibraryHandler::handleCreate(const http::HttpRequest& req, http::HttpResponse* resp)
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

    fitness::ExerciseInput input = exerciseInputFromJson(requestBody);
    auto validation = fitness::validateExerciseInput(input);
    if (!validation.valid)
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", validation.message}}, true);
        return;
    }

    try
    {
        std::string duplicateSql =
            "SELECT id FROM exercise_library WHERE name = ? AND (user_id = 0 OR user_id = ?) LIMIT 1";
        auto duplicate = mysqlUtil_.executeQuery(duplicateSql, std::as_const(input.name), userId);
        if (duplicate->next())
        {
            sendJson(req, resp, http::HttpResponse::k409Conflict, "Conflict",
                json{{"success", false}, {"message", "动作名称已存在，请换一个名称"}}, true);
            return;
        }

        std::string insertSql =
            "INSERT INTO exercise_library "
            "(user_id, name, category, primary_muscle, secondary_muscles, equipment, difficulty, description, tips, is_system) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0)";
        mysqlUtil_.executeUpdate(insertSql,
            userId,
            std::as_const(input.name),
            std::as_const(input.category),
            std::as_const(input.primaryMuscle),
            std::as_const(input.secondaryMuscles),
            std::as_const(input.equipment),
            std::as_const(input.difficulty),
            std::as_const(input.description),
            std::as_const(input.tips));

        std::string selectSql = selectExerciseSql() +
            "WHERE user_id = ? AND name = ? AND is_system = 0 LIMIT 1";
        auto created = mysqlUtil_.executeQuery(selectSql, userId, std::as_const(input.name));
        if (!created->next())
        {
            throw std::runtime_error("created exercise not found");
        }

        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"message", "动作创建成功"}, {"exercise", exerciseJsonFromResult(created)}},
            false);
    }
    catch (const std::exception& e)
    {
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false}, {"message", std::string("创建动作失败: ") + e.what()}}, true);
    }
}

void ExerciseLibraryHandler::handleUpdate(const http::HttpRequest& req, http::HttpResponse* resp)
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

    long long id = 0;
    if (!parsePositiveId(requestBody, id, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    fitness::ExerciseInput input = exerciseInputFromJson(requestBody);
    auto validation = fitness::validateExerciseInput(input);
    if (!validation.valid)
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", validation.message}}, true);
        return;
    }

    try
    {
        std::string checkSql = "SELECT user_id, is_system FROM exercise_library WHERE id = ? LIMIT 1";
        auto check = mysqlUtil_.executeQuery(checkSql, id);
        if (!check->next())
        {
            sendJson(req, resp, http::HttpResponse::k404NotFound, "Not Found",
                json{{"success", false}, {"message", "动作不存在"}}, true);
            return;
        }

        long long exerciseUserId = check->isNull("user_id") ? 0 : check->getInt64("user_id");
        bool isSystem = !check->isNull("is_system") && check->getInt("is_system") != 0;
        if (isSystem || exerciseUserId == 0)
        {
            sendJson(req, resp, http::HttpResponse::k403Forbidden, "Forbidden",
                json{{"success", false}, {"message", "系统动作不能编辑"}}, true);
            return;
        }
        if (!fitness::canModifyExercise(exerciseUserId, isSystem, userId))
        {
            sendJson(req, resp, http::HttpResponse::k403Forbidden, "Forbidden",
                json{{"success", false}, {"message", "只能编辑自己的自定义动作"}}, true);
            return;
        }

        std::string duplicateSql =
            "SELECT id FROM exercise_library "
            "WHERE name = ? AND (user_id = 0 OR user_id = ?) AND id <> ? LIMIT 1";
        auto duplicate = mysqlUtil_.executeQuery(duplicateSql, std::as_const(input.name), userId, id);
        if (duplicate->next())
        {
            sendJson(req, resp, http::HttpResponse::k409Conflict, "Conflict",
                json{{"success", false}, {"message", "动作名称已存在，请换一个名称"}}, true);
            return;
        }

        std::string updateSql =
            "UPDATE exercise_library SET "
            "name = ?, category = ?, primary_muscle = ?, secondary_muscles = ?, "
            "equipment = ?, difficulty = ?, description = ?, tips = ? "
            "WHERE id = ? AND user_id = ? AND is_system = 0";
        mysqlUtil_.executeUpdate(updateSql,
            std::as_const(input.name),
            std::as_const(input.category),
            std::as_const(input.primaryMuscle),
            std::as_const(input.secondaryMuscles),
            std::as_const(input.equipment),
            std::as_const(input.difficulty),
            std::as_const(input.description),
            std::as_const(input.tips),
            id,
            userId);

        std::string selectSql = selectExerciseSql() +
            "WHERE id = ? AND user_id = ? AND is_system = 0 LIMIT 1";
        auto updated = mysqlUtil_.executeQuery(selectSql, id, userId);
        if (!updated->next())
        {
            throw std::runtime_error("updated exercise not found");
        }

        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"message", "动作更新成功"}, {"exercise", exerciseJsonFromResult(updated)}},
            false);
    }
    catch (const std::exception& e)
    {
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false}, {"message", std::string("更新动作失败: ") + e.what()}}, true);
    }
}

void ExerciseLibraryHandler::handleDelete(const http::HttpRequest& req, http::HttpResponse* resp)
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

    long long id = 0;
    if (!parsePositiveId(requestBody, id, errorMessage))
    {
        sendJson(req, resp, http::HttpResponse::k400BadRequest, "Bad Request",
            json{{"success", false}, {"message", errorMessage}}, true);
        return;
    }

    try
    {
        std::string checkSql = "SELECT user_id, is_system FROM exercise_library WHERE id = ? LIMIT 1";
        auto check = mysqlUtil_.executeQuery(checkSql, id);
        if (!check->next())
        {
            sendJson(req, resp, http::HttpResponse::k404NotFound, "Not Found",
                json{{"success", false}, {"message", "动作不存在"}}, true);
            return;
        }

        long long exerciseUserId = check->isNull("user_id") ? 0 : check->getInt64("user_id");
        bool isSystem = !check->isNull("is_system") && check->getInt("is_system") != 0;
        if (isSystem || exerciseUserId == 0)
        {
            sendJson(req, resp, http::HttpResponse::k403Forbidden, "Forbidden",
                json{{"success", false}, {"message", "系统动作不能删除"}}, true);
            return;
        }
        if (!fitness::canModifyExercise(exerciseUserId, isSystem, userId))
        {
            sendJson(req, resp, http::HttpResponse::k403Forbidden, "Forbidden",
                json{{"success", false}, {"message", "只能删除自己的自定义动作"}}, true);
            return;
        }

        std::string deleteSql = "DELETE FROM exercise_library WHERE id = ? AND user_id = ? AND is_system = 0";
        mysqlUtil_.executeUpdate(deleteSql, id, userId);

        sendJson(req, resp, http::HttpResponse::k200Ok, "OK",
            json{{"success", true}, {"message", "动作删除成功"}}, false);
    }
    catch (const std::exception& e)
    {
        sendJson(req, resp, http::HttpResponse::k500InternalServerError, "Internal Server Error",
            json{{"success", false}, {"message", std::string("删除动作失败: ") + e.what()}}, true);
    }
}

void ExerciseLibraryHandler::sendJson(const http::HttpRequest& req, http::HttpResponse* resp,
    http::HttpResponse::HttpStatusCode statusCode, const std::string& statusMessage,
    const json& body, bool close)
{
    std::string responseBody = body.dump(4);
    server_->packageResp(req.getVersion(), statusCode, statusMessage, close,
        "application/json", responseBody.size(), responseBody, resp);
}
