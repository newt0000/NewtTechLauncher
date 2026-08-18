#include "HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace
{
struct Handles
{
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    ~Handles()
    {
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
};

void createRequest(const std::wstring& url, Handles& h)
{
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);

    wchar_t host[512]{};
    wchar_t path[4096]{};

    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        throw std::runtime_error("Invalid download URL.");

    h.session = WinHttpOpen(
        L"NewtTechLauncher/0.4",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!h.session)
        throw std::runtime_error("Unable to initialize WinHTTP.");

    h.connection = WinHttpConnect(
        h.session,
        std::wstring(host, uc.dwHostNameLength).c_str(),
        uc.nPort,
        0
    );

    if (!h.connection)
        throw std::runtime_error("Unable to connect to download server.");

    std::wstring requestPath(path, uc.dwUrlPathLength);
    if (requestPath.empty())
        requestPath = L"/";

    DWORD flags =
        uc.nScheme == INTERNET_SCHEME_HTTPS
            ? WINHTTP_FLAG_SECURE
            : 0;

    h.request = WinHttpOpenRequest(
        h.connection,
        L"GET",
        requestPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!h.request)
        throw std::runtime_error("Unable to create HTTP request.");

    if (!WinHttpSendRequest(
            h.request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(h.request, nullptr))
    {
        throw std::runtime_error("HTTP request failed.");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);

    WinHttpQueryHeaders(
        h.request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX
    );

    if (status < 200 || status >= 300)
        throw std::runtime_error(
            "Server returned HTTP " +
            std::to_string(status) +
            "."
        );
}

unsigned long long contentLength(HINTERNET request)
{
    wchar_t buffer[64]{};
    DWORD size = sizeof(buffer);

    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX,
            buffer,
            &size,
            WINHTTP_NO_HEADER_INDEX))
    {
        return 0;
    }

    try
    {
        return std::stoull(buffer);
    }
    catch (...)
    {
        return 0;
    }
}
}

std::string HttpClient::getUtf8(const std::wstring& url)
{
    Handles h;
    createRequest(url, h);

    std::string result;

    for (;;)
    {
        DWORD available = 0;

        if (!WinHttpQueryDataAvailable(h.request, &available) || available == 0)
            break;

        std::vector<char> buffer(available);
        DWORD read = 0;

        if (!WinHttpReadData(
                h.request,
                buffer.data(),
                available,
                &read))
            throw std::runtime_error("Unable to read HTTP response.");

        result.append(buffer.data(), read);
    }

    return result;
}

void HttpClient::downloadToFile(
    const std::wstring& url,
    const std::wstring& destination,
    const std::function<void(unsigned long long, unsigned long long)>& progress)
{
    Handles h;
    createRequest(url, h);

    const unsigned long long total = contentLength(h.request);

    std::filesystem::path dest(destination);
    std::filesystem::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary | std::ios::trunc);

    if (!out)
        throw std::runtime_error("Unable to create local file.");

    unsigned long long downloaded = 0;

    for (;;)
    {
        DWORD available = 0;

        if (!WinHttpQueryDataAvailable(h.request, &available))
            throw std::runtime_error("Unable to query download data.");

        if (available == 0)
            break;

        std::vector<char> buffer(available);
        DWORD read = 0;

        if (!WinHttpReadData(
                h.request,
                buffer.data(),
                available,
                &read))
            throw std::runtime_error("Unable to read download.");

        out.write(buffer.data(), static_cast<std::streamsize>(read));

        if (!out)
            throw std::runtime_error("Unable to write downloaded file.");

        downloaded += read;

        if (progress)
            progress(downloaded, total);
    }
}
