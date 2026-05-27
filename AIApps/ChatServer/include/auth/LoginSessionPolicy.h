#pragma once

#include <string>
#include <unordered_map>

namespace auth
{

inline std::string recordSuccessfulLogin(
    std::unordered_map<int, bool>& onlineUsers,
    std::unordered_map<int, std::string>& activeSessionIds,
    int userId,
    const std::string& newSessionId)
{
    std::string oldSessionId;
    auto old = activeSessionIds.find(userId);
    if (old != activeSessionIds.end() && old->second != newSessionId)
    {
        oldSessionId = old->second;
    }

    onlineUsers[userId] = true;
    activeSessionIds[userId] = newSessionId;
    return oldSessionId;
}

inline bool recordLogout(
    std::unordered_map<int, bool>& onlineUsers,
    std::unordered_map<int, std::string>& activeSessionIds,
    int userId,
    const std::string& sessionId)
{
    auto current = activeSessionIds.find(userId);
    if (current == activeSessionIds.end())
    {
        onlineUsers.erase(userId);
        return false;
    }

    if (current->second != sessionId)
    {
        return false;
    }

    activeSessionIds.erase(current);
    onlineUsers.erase(userId);
    return true;
}

} // namespace auth
