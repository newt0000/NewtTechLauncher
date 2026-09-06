#include "AuthManager.h"
#include "HttpClient.h"
#include "JsonLite.h"
#include <windows.h>
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>

std::wstring AuthManager::token_;

namespace {
std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,value.c_str(),(int)value.size(),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,value.c_str(),(int)value.size(),out.data(),n,nullptr,nullptr);
    return out;
}
std::wstring utf8ToWideA(const std::string& value) {
    if (value.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,value.c_str(),(int)value.size(),nullptr,0);
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,value.c_str(),(int)value.size(),out.data(),n);
    return out;
}
std::string escapeJson(const std::string& s) {
    std::string o;
    for(char c:s){ if(c=='\\'||c=='"') o+='\\'; if(c=='\n'){o+="\\n";continue;} o+=c; }
    return o;
}
std::filesystem::path tokenDirectory() {
    wchar_t local[MAX_PATH]{};
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
    if(n==0 || n>=MAX_PATH) return {};
    auto dir=std::filesystem::path(local)/L"NewtTech Launcher";
    std::error_code ec; std::filesystem::create_directories(dir,ec);
    return dir;
}
std::filesystem::path tokenPath() {
    auto dir=tokenDirectory();
    return dir.empty()?std::filesystem::path{}:dir/L"account.dat";
}
std::filesystem::path legacyTokenPath() {
    wchar_t local[MAX_PATH]{};
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
    if(n==0 || n>=MAX_PATH) return {};
    return std::filesystem::path(local)/L"NewtTech"/L"Launcher"/L"account.dat";
}
bool parseUser(const std::string& body, AuthUser& user, std::wstring& token) {
    JsonValue root=JsonLite::parse(body);
    if(!root.get("ok").asBool()) return false;
    token=utf8ToWideA(root.get("token").asString());
    const JsonValue& u=root.get("user");
    user.name=utf8ToWideA(u.get("name").asString());
    user.username=utf8ToWideA(u.get("username").asString());
    user.minecraftUsername=utf8ToWideA(u.get("minecraft_username").asString());
    return true;
}
}

bool AuthManager::saveToken(const std::wstring& token) {
    if(token.empty()) return false;
    auto path=tokenPath(); if(path.empty()) return false;
    DATA_BLOB in{},out{};
    in.pbData=reinterpret_cast<BYTE*>(const_cast<wchar_t*>(token.c_str()));
    in.cbData=static_cast<DWORD>((token.size()+1)*sizeof(wchar_t));
    if(!CryptProtectData(&in,L"NewtTech Launcher Account",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) return false;
    auto temp=path; temp+=L".tmp";
    std::ofstream f(temp,std::ios::binary|std::ios::trunc);
    if(!f){LocalFree(out.pbData);return false;}
    f.write(reinterpret_cast<const char*>(out.pbData),out.cbData); f.flush(); bool ok=!!f; f.close(); LocalFree(out.pbData);
    if(!ok){std::error_code ec;std::filesystem::remove(temp,ec);return false;}
    std::error_code ec; std::filesystem::remove(path,ec); ec.clear(); std::filesystem::rename(temp,path,ec); return !ec;
}
std::wstring AuthManager::loadToken() {
    auto path=tokenPath(); if(path.empty()) return {};
    if(!std::filesystem::exists(path)) {
        auto legacy=legacyTokenPath();
        if(!legacy.empty() && std::filesystem::exists(legacy)) {
            std::error_code ec; std::filesystem::copy_file(legacy,path,std::filesystem::copy_options::overwrite_existing,ec);
        }
    }
    std::ifstream f(path,std::ios::binary); if(!f) return {};
    std::vector<BYTE> b((std::istreambuf_iterator<char>(f)),{}); if(b.empty()) return {};
    DATA_BLOB in{static_cast<DWORD>(b.size()),b.data()},out{};
    if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) return {};
    std::wstring t(reinterpret_cast<wchar_t*>(out.pbData)); LocalFree(out.pbData); return t;
}
void AuthManager::deleteToken(){
    std::error_code ec; auto p=tokenPath(); if(!p.empty()) std::filesystem::remove(p,ec);
    ec.clear(); auto l=legacyTokenPath(); if(!l.empty()) std::filesystem::remove(l,ec); token_.clear();
}
bool AuthManager::hasToken(){ if(token_.empty()) token_=loadToken(); return !token_.empty(); }

bool AuthManager::login(const std::wstring& username,const std::wstring& password,
                        AuthUser& user,std::wstring& error) {
    try {
        std::string body="{\"username\":\""+escapeJson(wideToUtf8(username))+
                         "\",\"password\":\""+escapeJson(wideToUtf8(password))+"\"}";
        std::string response=HttpClient::postJsonUtf8(
            L"https://launcher.newttech.net/account/api/login.php",body);
        std::wstring token;
        if(!parseUser(response,user,token)||token.empty()){ error=L"Sign in was denied."; return false; }
        token_=token;
        if(!saveToken(token_)) { token_.clear(); error=L"Signed in, but the cached login could not be saved."; return false; }
        return true;
    } catch(const std::exception& e){ error=utf8ToWideA(e.what()); return false; }
}
bool AuthManager::restore(AuthUser& user)
{
    token_ = loadToken();

    if (token_.empty())
        return false;

    try
    {
        const std::string response =
            HttpClient::postJsonUtf8(
                L"https://launcher.newttech.net/account/api/session.php",
                "{\"token\":\"" + escapeJson(wideToUtf8(token_)) + "\"}",
                {
                    {
                        L"Authorization",
                        L"Bearer " + token_
                    }
                }
            );

        const JsonValue root =
            JsonLite::parse(response);

        if (!root.get("ok").asBool())
        {
            deleteToken();
            return false;
        }

        const JsonValue& u =
            root.get("user");

        const std::string username =
            u.get("username").asString();

        if (username.empty())
        {
            deleteToken();
            return false;
        }

        user.name =
            utf8ToWideA(
                u.get("name").asString()
            );

        user.username =
            utf8ToWideA(username);

        user.minecraftUsername =
            utf8ToWideA(
                u.get("minecraft_username").asString()
            );

        // Refresh/migrate the DPAPI cache after confirmed validation.
        saveToken(token_);

        return true;
    }
    catch (...)
    {
        // A failed validation cannot be treated as authenticated.
        // Keep the token on disk here so a temporary network outage does not
        // destroy an otherwise valid cached login.
        token_.clear();
        return false;
    }
}
void AuthManager::heartbeat() {
    if(!hasToken()) return;
    try { HttpClient::postJsonUtf8(L"https://launcher.newttech.net/account/api/heartbeat.php",
                                   "{\"token\":\"" + escapeJson(wideToUtf8(token_)) + "\"}",
                                   {{L"Authorization",L"Bearer "+token_}}); } catch(...) {}
}
void AuthManager::logout() {
    if(hasToken()) {
        try { HttpClient::postJsonUtf8(L"https://launcher.newttech.net/account/api/logout.php",
                                       "{\"token\":\"" + escapeJson(wideToUtf8(token_)) + "\"}",
                                       {{L"Authorization",L"Bearer "+token_}}); } catch(...) {}
    }
    deleteToken();
}
