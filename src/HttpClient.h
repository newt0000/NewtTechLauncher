#pragma once

#include <functional>
#include <string>
#include <vector>
#include <utility>

class HttpClient
{
public:
    static std::string getUtf8(const std::wstring& url);
    static std::string postJsonUtf8(
        const std::wstring& url,
        const std::string& json,
        const std::vector<std::pair<std::wstring, std::wstring>>& headers = {}
    );

    static void downloadToFile(
        const std::wstring& url,
        const std::wstring& destination,
        const std::function<void(unsigned long long, unsigned long long)>& progress = {}
    );
};
