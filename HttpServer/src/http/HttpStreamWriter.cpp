#include "../../include/http/HttpStreamWriter.h"

#include <muduo/net/TcpConnection.h>

namespace http
{

HttpStreamWriter::HttpStreamWriter(const std::shared_ptr<muduo::net::TcpConnection>& conn)
    : conn_(conn)
    , headerSent_(false)
{
}

void HttpStreamWriter::sendSseHeader()
{
    if (headerSent_)
    {
        return;
    }
    sendRaw(sseHeader());
    headerSent_ = true;
}

void HttpStreamWriter::sendEvent(const std::string& event, const std::string& data)
{
    if (!headerSent_)
    {
        sendSseHeader();
    }
    sendRaw(formatEvent(event, data));
}

void HttpStreamWriter::sendMessage(const std::string& content)
{
    sendEvent("message", jsonData("content", content));
}

void HttpStreamWriter::sendStatus(const std::string& message)
{
    sendEvent("status", jsonData("message", message));
}

void HttpStreamWriter::sendError(const std::string& message)
{
    sendEvent("error", jsonData("message", message));
}

void HttpStreamWriter::sendDone()
{
    sendEvent("done", doneData());
}

void HttpStreamWriter::close()
{
    if (conn_)
    {
        conn_->shutdown();
    }
}

void HttpStreamWriter::sendRaw(const std::string& data)
{
    if (conn_ && conn_->connected())
    {
        conn_->send(data);
    }
}

} // namespace http
