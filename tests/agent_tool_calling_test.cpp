#include "AIApps/ChatServer/include/tools/AgentToolCalling.h"

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
    agent::AgentToolRouter router(agent::defaultAgentToolSchemas());
    agent::AgentToolValidator validator(agent::defaultAgentToolSchemas());

    auto bmiCall = router.route("我身高175cm体重70kg，BMI是多少");
    require(bmiCall.toolName == "bmi_calculator", "BMI question should route to bmi_calculator");
    require(bmiCall.intent == "calculate_bmi", "BMI question should expose calculate_bmi intent");
    require(bmiCall.arguments["heightCm"].get<double>() == 175.0, "BMI extraction should parse heightCm");
    require(bmiCall.arguments["weightKg"].get<double>() == 70.0, "BMI extraction should parse weightKg");
    auto bmiValidation = validator.validate(bmiCall);
    require(bmiValidation.success, "BMI extracted arguments should validate");

    auto tdeeCall = router.route("男，22岁，175cm，70kg，每周训练4次，帮我算TDEE");
    require(tdeeCall.toolName == "tdee_calculator", "TDEE question should route to tdee_calculator");
    require(tdeeCall.arguments["gender"].get<std::string>() == "male", "TDEE extraction should parse gender");
    require(tdeeCall.arguments["age"].get<int>() == 22, "TDEE extraction should parse age");
    require(tdeeCall.arguments["heightCm"].get<double>() == 175.0, "TDEE extraction should parse heightCm");
    require(tdeeCall.arguments["weightKg"].get<double>() == 70.0, "TDEE extraction should parse weightKg");
    require(tdeeCall.arguments["activityLevel"].get<std::string>() == "moderate",
        "TDEE extraction should map weekly training frequency to moderate activityLevel");
    require(validator.validate(tdeeCall).success, "TDEE extracted arguments should validate");

    auto volumeCall = router.route("卧推80kg做8次4组，训练容量是多少");
    require(volumeCall.toolName == "training_volume_calculator",
        "Training volume question should route to training_volume_calculator");
    require(volumeCall.arguments["weight"].get<double>() == 80.0, "Volume extraction should parse weight");
    require(volumeCall.arguments["reps"].get<int>() == 8, "Volume extraction should parse reps");
    require(volumeCall.arguments["sets"].get<int>() == 4, "Volume extraction should parse sets");
    require(validator.validate(volumeCall).success, "Volume extracted arguments should validate");

    auto missingBmi = router.route("帮我算BMI");
    auto missingValidation = validator.validate(missingBmi);
    require(!missingValidation.success, "Missing BMI parameters should fail validation");
    require(missingValidation.message.find("heightCm") != std::string::npos,
        "Missing BMI validation should mention heightCm");
    require(missingValidation.message.find("weightKg") != std::string::npos,
        "Missing BMI validation should mention weightKg");

    auto successTrace = agent::AgentTrace::start("tool_call", bmiCall);
    successTrace.markValidation(true, "");
    successTrace.markToolResult(true, json{{"success", true}, {"bmi", 22.86}});
    require(!successTrace.traceId.empty(), "Successful trace should have traceId");
    require(successTrace.toJson()["validationStatus"].get<std::string>() == "success",
        "Successful trace should record validationStatus");

    auto failedTrace = agent::AgentTrace::start("tool_call", missingBmi);
    failedTrace.markValidation(false, missingValidation.message);
    require(!failedTrace.traceId.empty(), "Failed trace should have traceId");
    require(failedTrace.toJson()["validationStatus"].get<std::string>() == "failed",
        "Failed trace should record validationStatus");

    return 0;
}
