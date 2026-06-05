#include "AIApps/ChatServer/include/auth/PasswordHasher.h"

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
    const std::string password = "correct horse battery staple";
    const std::string saltA = auth::PasswordHasher::generateSalt();
    const std::string saltB = auth::PasswordHasher::generateSalt();

    require(!saltA.empty(), "generated salt should not be empty");
    require(saltA != saltB, "two generated salts should differ");

    const std::string hashA = auth::PasswordHasher::hashPassword(password, saltA);
    const std::string hashB = auth::PasswordHasher::hashPassword(password, saltB);

    require(!hashA.empty(), "password hash should not be empty");
    require(hashA != password, "password hash should not equal plaintext");
    require(hashA != hashB, "same password with different salts should hash differently");
    require(auth::PasswordHasher::verifyPassword(password, saltA, hashA),
        "correct password should verify");
    require(!auth::PasswordHasher::verifyPassword("wrong password", saltA, hashA),
        "wrong password should fail verification");

    return 0;
}
