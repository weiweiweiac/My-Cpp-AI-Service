#pragma once

#include "FitnessToolService.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"

#include <string>
#include <vector>

namespace agent
{

struct AgentToolSchema
{
    std::string name;
    std::string legacyToolName;
    std::string intent;
    std::string description;
    json parameters;
    std::vector<std::string> requiredFields;
    std::vector<std::string> examples;

    json toJson() const;
};

struct AgentToolCall
{
    std::string toolName;
    std::string legacyToolName;
    std::string rawUserMessage;
    std::string intent;
    json arguments { json::object() };
    double confidence { 0.0 };
    bool needSecondLLMCall { false };

    bool matched() const;
};

struct AgentToolValidationResult
{
    bool success { false };
    std::string message;
    std::vector<std::string> missingFields;
};

struct AgentToolResult
{
    bool success { false };
    std::string toolName;
    json resultJson { json::object() };
    std::string errorMessage;

    json toJson() const;
};

class AgentTrace
{
public:
    static AgentTrace start(const std::string& type, const AgentToolCall& call);

    void markValidation(bool success, const std::string& error);
    void markToolResult(bool success, const json& result);
    void markFinalAnswer(bool success, const std::string& error);
    void markError(const std::string& error);

    json toJson() const;

    std::string traceId;
    std::string type;
    std::string userMessage;
    std::string intent;
    std::string selectedTool;
    json arguments { json::object() };
    std::string validationStatus { "pending" };
    std::string validationError;
    std::string toolStatus { "not_started" };
    std::string toolResultSummary;
    bool needSecondLLMCall { false };
    std::string finalAnswerStatus { "not_started" };
    std::string errorMessage;
    std::string createdAt;
};

std::vector<AgentToolSchema> defaultAgentToolSchemas();
json agentToolSchemasToJson(const std::vector<AgentToolSchema>& schemas);
std::string standardToolName(const std::string& name);
std::string legacyToolName(const std::string& standardName);
std::string intentForTool(const std::string& standardName);

class AgentToolRouter
{
public:
    explicit AgentToolRouter(std::vector<AgentToolSchema> schemas);

    AgentToolCall route(const std::string& userMessage,
        const json& explicitArguments = json::object(),
        const std::string& explicitToolName = "",
        bool needSecondLLMCall = false) const;

private:
    const AgentToolSchema* findSchema(const std::string& standardName) const;
    json extractArguments(const std::string& userMessage, const std::string& standardName) const;

    std::vector<AgentToolSchema> schemas_;
};

class AgentToolValidator
{
public:
    explicit AgentToolValidator(std::vector<AgentToolSchema> schemas);

    AgentToolValidationResult validate(const AgentToolCall& call) const;

private:
    const AgentToolSchema* findSchema(const std::string& standardName) const;

    std::vector<AgentToolSchema> schemas_;
};

class AgentToolExecutor
{
public:
    explicit AgentToolExecutor(tools::FitnessToolService& service);

    AgentToolResult execute(const AgentToolCall& call, int userId) const;

private:
    tools::FitnessToolService& service_;
};

} // namespace agent
