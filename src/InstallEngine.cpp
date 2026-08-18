#include "InstallEngine.h"

#include "HttpClient.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
std::wstring normalizeHex(std::wstring value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](wchar_t c)
        {
            return static_cast<wchar_t>(
                towlower(c)
            );
        }
    );

    return value;
}

bool fileMatches(
    const PackFile& file,
    const std::filesystem::path& local)
{
    if (!std::filesystem::is_regular_file(local))
        return false;

    if (
        file.size > 0 &&
        static_cast<long long>(
            std::filesystem::file_size(local)
        ) != file.size
    )
        return false;

    if (!file.sha256.empty())
    {
        const std::wstring actual =
            InstallEngine::sha256File(
                local.wstring()
            );

        return normalizeHex(actual) ==
               normalizeHex(file.sha256);
    }

    return true;
}
}

std::wstring InstallEngine::packInstanceRoot(
    const std::wstring& root,
    const std::wstring& packId)
{
    return (
        std::filesystem::path(root) /
        packId
    ).wstring();
}

bool InstallEngine::isInstalled(
    const PackManifest& manifest,
    const std::wstring& instanceRoot)
{
    if (manifest.id.empty())
        return false;

    const std::filesystem::path root(
        packInstanceRoot(
            instanceRoot,
            manifest.id
        )
    );

    if (!std::filesystem::exists(root))
        return false;

    for (const PackFile& file : manifest.files)
    {
        const std::filesystem::path local =
            root /
            std::filesystem::path(file.path);

        if (!fileMatches(file, local))
            return false;
    }

    return true;
}

void InstallEngine::installOrRepair(
    const PackManifest& manifest,
    const std::wstring& instanceRoot,
    const ProgressCallback& callback)
{
    if (manifest.id.empty())
        throw std::runtime_error("Pack manifest has no ID.");

    const std::filesystem::path root(
        packInstanceRoot(
            instanceRoot,
            manifest.id
        )
    );

    std::filesystem::create_directories(root);

    const int total =
        static_cast<int>(
            manifest.files.size()
        );

    InstallProgress progress;
    progress.active = true;
    progress.totalFiles = total;
    progress.title = L"Preparing " + manifest.name;

    if (callback)
        callback(progress);

    int current = 0;

    for (const PackFile& file : manifest.files)
    {
        ++current;

        const std::filesystem::path local =
            root /
            std::filesystem::path(file.path);

        progress.currentFile = current;
        progress.totalFiles = total;
        progress.title =
            L"Installing " +
            manifest.name;

        progress.detail = file.path;

        if (callback)
            callback(progress);

        const std::wstring policy =
            file.policy.empty()
                ? L"replace"
                : file.policy;

        if (
            policy == L"install-if-missing" &&
            std::filesystem::exists(local)
        )
        {
            progress.percent =
                total > 0
                    ? (current * 100 / total)
                    : 100;

            if (callback)
                callback(progress);

            continue;
        }

        if (fileMatches(file, local))
        {
            progress.percent =
                total > 0
                    ? (current * 100 / total)
                    : 100;

            if (callback)
                callback(progress);

            continue;
        }

        std::filesystem::create_directories(
            local.parent_path()
        );

        const std::filesystem::path temp =
            local.wstring() +
            L".download";

        HttpClient::downloadToFile(
            file.url,
            temp.wstring(),
            [&](unsigned long long downloaded,
                unsigned long long fileTotal)
            {
                if (fileTotal > 0)
                {
                    const int withinFile =
                        static_cast<int>(
                            downloaded * 100 /
                            fileTotal
                        );

                    const int base =
                        total > 0
                            ? ((current - 1) * 100 / total)
                            : 0;

                    const int span =
                        total > 0
                            ? (100 / total)
                            : 100;

                    progress.percent =
                        std::min(
                            99,
                            base +
                            (withinFile * span / 100)
                        );
                }

                if (callback)
                    callback(progress);
            }
        );

        if (!file.sha256.empty())
        {
            const std::wstring actual =
                sha256File(
                    temp.wstring()
                );

            if (
                normalizeHex(actual) !=
                normalizeHex(file.sha256)
            )
            {
                std::filesystem::remove(temp);

                throw std::runtime_error(
                    "SHA-256 verification failed."
                );
            }
        }

        if (std::filesystem::exists(local))
            std::filesystem::remove(local);

        std::filesystem::rename(
            temp,
            local
        );

        progress.percent =
            total > 0
                ? (current * 100 / total)
                : 100;

        if (callback)
            callback(progress);
    }

    // Save a tiny local instance marker.
    {
        std::ofstream marker(
            root / ".newttech-instance",
            std::ios::binary | std::ios::trunc
        );

        marker
            << "pack=";

        for (wchar_t c : manifest.id)
        {
            if (c >= 0 && c < 128)
                marker << static_cast<char>(c);
        }
    }

    progress.active = false;
    progress.complete = true;
    progress.percent = 100;
    progress.title = manifest.name + L" is ready";
    progress.detail =
        std::to_wstring(total) +
        L" managed files verified";

    if (callback)
        callback(progress);
}

std::wstring InstallEngine::sha256File(
    const std::wstring& path)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    NTSTATUS status =
        BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0
        );

    if (status < 0)
        throw std::runtime_error(
            "Unable to initialize SHA-256."
        );

    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD resultLength = 0;

    BCryptGetProperty(
        algorithm,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength),
        sizeof(objectLength),
        &resultLength,
        0
    );

    BCryptGetProperty(
        algorithm,
        BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashLength),
        sizeof(hashLength),
        &resultLength,
        0
    );

    std::vector<UCHAR> object(objectLength);
    std::vector<UCHAR> digest(hashLength);

    status =
        BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            objectLength,
            nullptr,
            0,
            0
        );

    if (status < 0)
    {
        BCryptCloseAlgorithmProvider(
            algorithm,
            0
        );

        throw std::runtime_error(
            "Unable to create SHA-256 hash."
        );
    }

    std::ifstream input(
        std::filesystem::path(path),
        std::ios::binary
    );

    if (!input)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(
            algorithm,
            0
        );

        throw std::runtime_error(
            "Unable to read file for verification."
        );
    }

    std::vector<char> buffer(
        1024 * 1024
    );

    while (input)
    {
        input.read(
            buffer.data(),
            static_cast<std::streamsize>(
                buffer.size()
            )
        );

        const std::streamsize count =
            input.gcount();

        if (count > 0)
        {
            status =
                BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(
                        buffer.data()
                    ),
                    static_cast<ULONG>(count),
                    0
                );

            if (status < 0)
                break;
        }
    }

    if (status >= 0)
    {
        status =
            BCryptFinishHash(
                hash,
                digest.data(),
                hashLength,
                0
            );
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(
        algorithm,
        0
    );

    if (status < 0)
        throw std::runtime_error(
            "Unable to calculate SHA-256."
        );

    std::wstringstream stream;

    stream
        << std::hex
        << std::setfill(L'0');

    for (UCHAR byte : digest)
        stream
            << std::setw(2)
            << static_cast<int>(byte);

    return stream.str();
}
