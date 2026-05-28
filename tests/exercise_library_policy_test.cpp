#include "AIApps/ChatServer/include/fitness/ExerciseLibraryPolicy.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
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

std::string readFile(const std::string& path)
{
    std::ifstream input(path);
    require(input.good(), "expected file to exist: " + path);

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

size_t countOccurrences(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    size_t pos = text.find(needle);
    while (pos != std::string::npos)
    {
        ++count;
        pos = text.find(needle, pos + needle.size());
    }
    return count;
}

} // namespace

int main()
{
    fitness::ExerciseInput emptyName;
    emptyName.name = "   ";
    require(!fitness::validateExerciseInput(emptyName).valid,
        "empty exercise name should be invalid");

    fitness::ExerciseInput longName;
    longName.name = std::string(101, 'a');
    require(!fitness::validateExerciseInput(longName).valid,
        "exercise name longer than 100 bytes should be invalid");

    fitness::ExerciseInput valid;
    valid.name = "Bench Press";
    valid.category = "Chest";
    valid.primaryMuscle = "Pectoralis Major";
    valid.secondaryMuscles = "Triceps,Anterior Deltoid";
    valid.equipment = "Barbell";
    valid.difficulty = "Intermediate";
    valid.description = std::string(2000, 'd');
    valid.tips = std::string(2000, 't');
    require(fitness::validateExerciseInput(valid).valid,
        "valid exercise input should pass validation");

    fitness::ExerciseInput tooLongDescription = valid;
    tooLongDescription.description = std::string(2001, 'd');
    require(!fitness::validateExerciseInput(tooLongDescription).valid,
        "description longer than 2000 bytes should be invalid");

    require(fitness::canReadExercise(0, true, 7),
        "user should read system exercise");
    require(fitness::canReadExercise(7, false, 7),
        "user should read own custom exercise");
    require(!fitness::canReadExercise(8, false, 7),
        "user should not read another user's custom exercise");
    require(!fitness::canReadExercise(0, false, 7),
        "non-system exercise with user_id 0 should not be readable");

    require(!fitness::canModifyExercise(0, true, 7),
        "system exercise should not be editable or deletable");
    require(fitness::canModifyExercise(7, false, 7),
        "user should edit or delete own custom exercise");
    require(!fitness::canModifyExercise(8, false, 7),
        "user should not edit or delete another user's custom exercise");

    const std::string schema = readFile("docs/sql/exercise_library.sql");
    require(schema.find("CREATE TABLE IF NOT EXISTS exercise_library") != std::string::npos,
        "exercise_library schema should create the table");
    require(schema.find("UNIQUE KEY uk_exercise_user_name") != std::string::npos,
        "exercise_library schema should prevent duplicate names per user");

    const std::string seed = readFile("docs/sql/exercise_library_seed.sql");
    require(seed.find("INSERT IGNORE INTO exercise_library") != std::string::npos,
        "seed sql should use INSERT IGNORE");
    require(countOccurrences(seed, "(0,") >= 39,
        "seed sql should contain at least 39 system exercise rows with user_id=0");

    return 0;
}
