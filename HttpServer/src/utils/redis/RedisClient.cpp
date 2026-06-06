#include "../../../include/utils/redis/RedisClient.h"

#include <muduo/base/Logging.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace http
{
namespace redis
{

namespace
{

std::string envOrDefault(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return fallback;
    }
    return value;
}

struct RedisReply
{
    char type { '\0' };
    bool nil { false };
    long long integer { 0 };
    std::string text;
};

bool sendAll(int fd, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0)
        {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool readByte(int fd, char& ch)
{
    ssize_t n = ::recv(fd, &ch, 1, 0);
    return n == 1;
}

bool readLine(int fd, std::string& line)
{
    line.clear();
    char ch = '\0';
    while (readByte(fd, ch))
    {
        if (ch == '\r')
        {
            char next = '\0';
            if (!readByte(fd, next) || next != '\n')
            {
                return false;
            }
            return true;
        }
        line.push_back(ch);
    }
    return false;
}

bool readExact(int fd, size_t length, std::string& output)
{
    output.clear();
    output.resize(length);
    size_t received = 0;
    while (received < length)
    {
        ssize_t n = ::recv(fd, &output[received], length - received, 0);
        if (n <= 0)
        {
            return false;
        }
        received += static_cast<size_t>(n);
    }

    char cr = '\0';
    char lf = '\0';
    return readByte(fd, cr) && readByte(fd, lf) && cr == '\r' && lf == '\n';
}

std::string encodeCommand(const std::vector<std::string>& args)
{
    std::ostringstream oss;
    oss << "*" << args.size() << "\r\n";
    for (const auto& arg : args)
    {
        oss << "$" << arg.size() << "\r\n";
        oss << arg << "\r\n";
    }
    return oss.str();
}

bool readReply(int fd, RedisReply& reply)
{
    reply = RedisReply {};
    if (!readByte(fd, reply.type))
    {
        return false;
    }

    std::string line;
    switch (reply.type)
    {
    case '+':
    case '-':
        if (!readLine(fd, reply.text))
        {
            return false;
        }
        return reply.type != '-';
    case ':':
        if (!readLine(fd, line))
        {
            return false;
        }
        reply.integer = std::stoll(line);
        return true;
    case '$':
        if (!readLine(fd, line))
        {
            return false;
        }
        {
            long long length = std::stoll(line);
            if (length < 0)
            {
                reply.nil = true;
                return true;
            }
            return readExact(fd, static_cast<size_t>(length), reply.text);
        }
    default:
        return false;
    }
}

} // namespace

RedisClient::RedisClient()
{
    const std::string host = envOrDefault("REDIS_HOST", "127.0.0.1");
    const int port = std::stoi(envOrDefault("REDIS_PORT", "6379"));
    const std::string password = envOrDefault("REDIS_PASSWORD", "");
    connect(host, port, password);
}

RedisClient::RedisClient(const std::string& host, int port, const std::string& password)
{
    connect(host, port, password);
}

RedisClient::~RedisClient()
{
    if (socketFd_ >= 0)
    {
        ::close(socketFd_);
        socketFd_ = -1;
    }
}

void RedisClient::connect(const std::string& host, int port, const std::string& password)
{
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const std::string portText = std::to_string(port);
    int rc = ::getaddrinfo(host.c_str(), portText.c_str(), &hints, &result);
    if (rc != 0)
    {
        LOG_ERROR << "Redis address resolution failed";
        return;
    }

    for (addrinfo* current = result; current != nullptr; current = current->ai_next)
    {
        int fd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (fd < 0)
        {
            continue;
        }

        if (::connect(fd, current->ai_addr, current->ai_addrlen) == 0)
        {
            socketFd_ = fd;
            break;
        }

        ::close(fd);
    }
    ::freeaddrinfo(result);

    if (socketFd_ < 0)
    {
        LOG_ERROR << "Redis connection failed: " << std::strerror(errno);
        return;
    }

    if (!password.empty())
    {
        RedisReply reply;
        if (!sendAll(socketFd_, encodeCommand({"AUTH", password})) || !readReply(socketFd_, reply))
        {
            LOG_ERROR << "Redis AUTH failed";
            ::close(socketFd_);
            socketFd_ = -1;
            return;
        }
    }

    LOG_INFO << "Redis connection established";
}

bool RedisClient::isConnected() const
{
    return socketFd_ >= 0;
}

std::optional<std::string> RedisClient::get(const std::string& key)
{
    if (!isConnected())
    {
        return std::nullopt;
    }

    RedisReply reply;
    if (!sendAll(socketFd_, encodeCommand({"GET", key})) || !readReply(socketFd_, reply))
    {
        return std::nullopt;
    }
    if (reply.nil)
    {
        return std::nullopt;
    }
    if (reply.type != '$')
    {
        return std::nullopt;
    }
    return reply.text;
}

bool RedisClient::set(const std::string& key, const std::string& value)
{
    if (!isConnected())
    {
        return false;
    }
    RedisReply reply;
    return sendAll(socketFd_, encodeCommand({"SET", key, value})) &&
           readReply(socketFd_, reply) &&
           reply.type == '+';
}

bool RedisClient::setex(const std::string& key, int seconds, const std::string& value)
{
    if (!isConnected())
    {
        return false;
    }
    RedisReply reply;
    return sendAll(socketFd_, encodeCommand({"SETEX", key, std::to_string(seconds), value})) &&
           readReply(socketFd_, reply) &&
           reply.type == '+';
}

bool RedisClient::del(const std::string& key)
{
    if (!isConnected())
    {
        return false;
    }
    RedisReply reply;
    return sendAll(socketFd_, encodeCommand({"DEL", key})) &&
           readReply(socketFd_, reply) &&
           reply.type == ':';
}

long long RedisClient::incr(const std::string& key)
{
    if (!isConnected())
    {
        return -1;
    }
    RedisReply reply;
    if (!sendAll(socketFd_, encodeCommand({"INCR", key})) ||
        !readReply(socketFd_, reply) ||
        reply.type != ':')
    {
        return -1;
    }
    return reply.integer;
}

bool RedisClient::expire(const std::string& key, int seconds)
{
    if (!isConnected())
    {
        return false;
    }
    RedisReply reply;
    return sendAll(socketFd_, encodeCommand({"EXPIRE", key, std::to_string(seconds)})) &&
           readReply(socketFd_, reply) &&
           reply.type == ':';
}

std::shared_ptr<RedisClient> makeRedisClientFromEnv()
{
    return std::make_shared<RedisClient>();
}

} // namespace redis
} // namespace http
