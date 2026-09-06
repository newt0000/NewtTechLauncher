#pragma once
#include <string>

struct AuthUser {
    std::wstring name;
    std::wstring username;
    std::wstring minecraftUsername;
};

class AuthManager {
public:
    static bool login(const std::wstring& username, const std::wstring& password,
                      AuthUser& user, std::wstring& error);
    static bool restore(AuthUser& user);
    static void heartbeat();
    static void logout();
    static bool hasToken();

private:
    static std::wstring token_;
    static bool saveToken(const std::wstring& token);
    static std::wstring loadToken();
    static void deleteToken();
};
