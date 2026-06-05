#include "../../include/auth/PasswordHasher.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace auth
{

namespace
{

constexpr int kIterations = 120000;
constexpr size_t kHashBytes = 32;

std::string toHex(const unsigned char* data, size_t size)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i)
    {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

bool constantTimeEquals(const std::string& left, const std::string& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    unsigned char diff = 0;
    for (size_t i = 0; i < left.size(); ++i)
    {
        diff |= static_cast<unsigned char>(left[i] ^ right[i]);
    }
    return diff == 0;
}

} // namespace

std::string PasswordHasher::generateSalt(size_t byteCount)
{
    std::vector<unsigned char> salt(byteCount);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1)
    {
        throw std::runtime_error("failed to generate password salt");
    }
    return toHex(salt.data(), salt.size());
}

std::string PasswordHasher::hashPassword(const std::string& password, const std::string& salt)
{
    std::vector<unsigned char> hash(kHashBytes);
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(),
            static_cast<int>(password.size()),
            reinterpret_cast<const unsigned char*>(salt.data()),
            static_cast<int>(salt.size()),
            kIterations,
            EVP_sha256(),
            static_cast<int>(hash.size()),
            hash.data()) != 1)
    {
        throw std::runtime_error("failed to hash password");
    }

    return toHex(hash.data(), hash.size());
}

bool PasswordHasher::verifyPassword(
    const std::string& password,
    const std::string& salt,
    const std::string& expectedHash)
{
    if (salt.empty() || expectedHash.empty())
    {
        return false;
    }
    return constantTimeEquals(hashPassword(password, salt), expectedHash);
}

} // namespace auth
