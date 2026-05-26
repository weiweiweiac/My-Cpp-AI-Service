#pragma once

#include <cstdio>
#include <memory>
#include <string>

namespace muduo
{
namespace net
{
class TcpConnection;
}
}

namespace http
{

class HttpStreamWriter
{
public:
    explicit HttpStreamWriter(const std::shared_ptr<muduo::net::TcpConnection>& conn);

    void sendSseHeader();
    void sendEvent(const std::string& event, const std::string& data);
    void sendMessage(const std::string& content);
    void sendStatus(const std::string& message);
    void sendError(const std::string& message);
    void sendDone();
    void close();

    bool headerSent() const { return headerSent_; }

    static std::string sseHeader()
    {
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/event-stream; charset=utf-8\r\n"
               "Cache-Control: no-cache\r\n"
               "Connection: keep-alive\r\n"
               "X-Accel-Buffering: no\r\n"
               "\r\n";
    }

    static std::string formatEvent(const std::string& event, const std::string& data)
    {
        return "event: " + event + "\n"
            + "data: " + data + "\n\n";
    }

    static std::string jsonData(const std::string& key, const std::string& value)
    {
        return "{\"" + jsonEscape(key) + "\":\"" + jsonEscape(value) + "\"}";
    }

    static std::string jsonEscape(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8);
        for (unsigned char ch : value)
        {
            switch (ch)
            {
            case '\"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20)
                {
                    char buffer[7] = {0};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                    escaped += buffer;
                }
                else
                {
                    escaped += static_cast<char>(ch);
                }
                break;
            }
        }
        return escaped;
    }

private:
    void sendRaw(const std::string& data);

private:
    std::shared_ptr<muduo::net::TcpConnection> conn_;
    bool headerSent_;
};

} // namespace http
