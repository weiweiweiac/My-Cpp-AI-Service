#pragma once

#include <string>

namespace auth
{

class PasswordHasher
{
public:
    static std::string generateSalt(size_t byteCount = 16);
    static std::string hashPassword(const std::string& password, const std::string& salt);
    static bool verifyPassword(
        const std::string& password,
        const std::string& salt,
        const std::string& expectedHash);
};

} // namespace auth
