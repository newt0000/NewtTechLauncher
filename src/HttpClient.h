#pragma once

#include <functional>
#include <string>

class HttpClient
{
public:
    static std::string getUtf8(const std::wstring& url);

    static void downloadToFile(
        const std::wstring& url,
        const std::wstring& destination,
        const std::function<void(unsigned long long, unsigned long long)>& progress = {}
    );
};
