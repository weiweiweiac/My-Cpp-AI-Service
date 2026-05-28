#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace fitness
{

struct ExerciseInput
{
    std::string name;
    std::string category;
    std::string primaryMuscle;
    std::string secondaryMuscles;
    std::string equipment;
    std::string difficulty;
    std::string description;
    std::string tips;
};

struct ExerciseValidationResult
{
    bool valid = false;
    std::string message;
};

inline std::string trimExerciseText(const std::string& value)
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

inline void normalizeExerciseInput(ExerciseInput& input)
{
    input.name = trimExerciseText(input.name);
    input.category = trimExerciseText(input.category);
    input.primaryMuscle = trimExerciseText(input.primaryMuscle);
    input.secondaryMuscles = trimExerciseText(input.secondaryMuscles);
    input.equipment = trimExerciseText(input.equipment);
    input.difficulty = trimExerciseText(input.difficulty);
    input.description = trimExerciseText(input.description);
    input.tips = trimExerciseText(input.tips);
}

inline bool validateExerciseLength(const std::string& value, size_t maxBytes,
    const std::string& label, std::string& errorMessage)
{
    if (value.size() > maxBytes)
    {
        errorMessage = label + "长度不能超过 " + std::to_string(maxBytes) + " 字节";
        return false;
    }
    return true;
}

inline ExerciseValidationResult validateExerciseInput(const ExerciseInput& rawInput)
{
    ExerciseInput input = rawInput;
    normalizeExerciseInput(input);

    if (input.name.empty())
    {
        return {false, "动作名称不能为空"};
    }

    std::string errorMessage;
    if (!validateExerciseLength(input.name, 100, "动作名称", errorMessage) ||
        !validateExerciseLength(input.category, 50, "分类", errorMessage) ||
        !validateExerciseLength(input.primaryMuscle, 100, "目标肌群", errorMessage) ||
        !validateExerciseLength(input.secondaryMuscles, 255, "辅助肌群", errorMessage) ||
        !validateExerciseLength(input.equipment, 100, "器械", errorMessage) ||
        !validateExerciseLength(input.difficulty, 50, "难度", errorMessage) ||
        !validateExerciseLength(input.description, 2000, "动作说明", errorMessage) ||
        !validateExerciseLength(input.tips, 2000, "注意事项", errorMessage))
    {
        return {false, errorMessage};
    }

    return {true, "ok"};
}

inline bool canReadExercise(long long exerciseUserId, bool isSystem, int currentUserId)
{
    return isSystem || (currentUserId > 0 && exerciseUserId == currentUserId);
}

inline bool canModifyExercise(long long exerciseUserId, bool isSystem, int currentUserId)
{
    return !isSystem && exerciseUserId > 0 && exerciseUserId == currentUserId;
}

} // namespace fitness
