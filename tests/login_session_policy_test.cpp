#include "AIApps/ChatServer/include/auth/LoginSessionPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

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
    std::unordered_map<int, bool> onlineUsers;
    std::unordered_map<int, std::string> activeSessionIds;

    std::string oldSession = auth::recordSuccessfulLogin(
        onlineUsers, activeSessionIds, 7, "session-a");
    require(oldSession.empty(), "first login should not destroy any old session");
    require(onlineUsers[7], "first login should mark user online");
    require(activeSessionIds[7] == "session-a", "first login should store active session");

    oldSession = auth::recordSuccessfulLogin(
        onlineUsers, activeSessionIds, 7, "session-b");
    require(oldSession == "session-a", "second login should return old session for destruction");
    require(onlineUsers[7], "second login should keep user online");
    require(activeSessionIds[7] == "session-b", "second login should replace active session");

    bool cleared = auth::recordLogout(
        onlineUsers, activeSessionIds, 7, "session-a");
    require(!cleared, "logout from stale session should not clear current login");
    require(onlineUsers[7], "stale logout should leave current user online");
    require(activeSessionIds[7] == "session-b", "stale logout should keep newer session");

    cleared = auth::recordLogout(
        onlineUsers, activeSessionIds, 7, "session-b");
    require(cleared, "logout from active session should clear current login");
    require(onlineUsers.find(7) == onlineUsers.end(), "active logout should erase online marker");
    require(activeSessionIds.find(7) == activeSessionIds.end(), "active logout should erase session mapping");

    cleared = auth::recordLogout(
        onlineUsers, activeSessionIds, 7, "session-b");
    require(!cleared, "repeated logout should be idempotent");

    return 0;
}
