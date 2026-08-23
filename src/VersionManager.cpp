#include "VersionManager.h"

#include "AppConfig.h"
#include "HttpClient.h"
#include "JsonLite.h"

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
std::wstring quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

std::wstring lower(std::wstring value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](wchar_t c) {
            return static_cast<wchar_t>(towlower(c));
        }
    );
    return value;
}

void writeUtf8File(
    const std::filesystem::path& path,
    const std::string& bytes)
{
    std::ofstream out(
        path,
        std::ios::binary | std::ios::trunc
    );

    if (!out)
        throw std::runtime_error(
            "Unable to write Minecraft version metadata."
        );

    out.write(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!out)
        throw std::runtime_error(
            "Unable to finish writing Minecraft version metadata."
        );
}
}

VersionPackageInfo VersionManager::fetchPackageInfo(
    const PackManifest& manifest)
{
    if (
        manifest.minecraft.loader.empty() ||
        manifest.minecraft.loaderVersion.empty() ||
        manifest.minecraft.version.empty()
    )
    {
        throw std::runtime_error(
            "Manifest is missing Minecraft/loader version information."
        );
    }

    std::wstring loader =
        lower(manifest.minecraft.loader);

    const std::wstring manifestUrl =
        std::wstring(AppConfig::VERSION_BASE_URL) +
        L"/" +
        loader +
        L"/" +
        manifest.minecraft.loaderVersion +
        L"/manifest.json";

    JsonValue root =
        JsonLite::parse(
            HttpClient::getUtf8(manifestUrl)
        );

    VersionPackageInfo result;
    result.loader = manifest.minecraft.loader;
    result.loaderVersion = manifest.minecraft.loaderVersion;
    result.minecraftVersion = manifest.minecraft.version;

    result.versionId =
        utf8ToWide(
            root.get("versionId").asString()
        );

    result.archiveUrl =
        utf8ToWide(
            root.get("archive").asString()
        );

    result.sha256 =
        utf8ToWide(
            root.get("sha256").asString()
        );

    const std::wstring serverMinecraft =
        utf8ToWide(
            root.get("minecraft").asString()
        );

    const std::wstring serverLoader =
        utf8ToWide(
            root.get("loader").asString()
        );

    const std::wstring serverLoaderVersion =
        utf8ToWide(
            root.get("loaderVersion").asString()
        );

    if (
        result.versionId.empty() ||
        result.archiveUrl.empty()
    )
    {
        throw std::runtime_error(
            "Server version manifest is incomplete."
        );
    }

    if (
        !serverMinecraft.empty() &&
        serverMinecraft != manifest.minecraft.version
    )
    {
        throw std::runtime_error(
            "Server loader package targets a different Minecraft version."
        );
    }

    if (
        !serverLoader.empty() &&
        lower(serverLoader) != loader
    )
    {
        throw std::runtime_error(
            "Server version package loader does not match the modpack."
        );
    }

    if (
        !serverLoaderVersion.empty() &&
        serverLoaderVersion != manifest.minecraft.loaderVersion
    )
    {
        throw std::runtime_error(
            "Server loader package version does not match the modpack."
        );
    }

    return result;
}

std::wstring VersionManager::minecraftRoot()
{
    PWSTR roaming = nullptr;

    if (FAILED(
        SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            0,
            nullptr,
            &roaming
        )
    ))
    {
        return {};
    }

    std::wstring result =
        std::wstring(roaming) +
        L"\\.minecraft";

    CoTaskMemFree(roaming);
    return result;
}

bool VersionManager::isMinecraftBaseInstalled(
    const std::wstring& minecraftVersion)
{
    const std::wstring root = minecraftRoot();

    if (root.empty() || minecraftVersion.empty())
        return false;

    const std::filesystem::path dir =
        std::filesystem::path(root) /
        L"versions" /
        minecraftVersion;

    return
        std::filesystem::exists(
            dir / (minecraftVersion + L".json")
        ) &&
        std::filesystem::exists(
            dir / (minecraftVersion + L".jar")
        );
}

void VersionManager::ensureMinecraftBaseVersion(
    const std::wstring& minecraftVersion)
{
    if (minecraftVersion.empty())
        throw std::runtime_error(
            "Minecraft version is empty."
        );

    if (isMinecraftBaseInstalled(minecraftVersion))
        return;

    const std::wstring root = minecraftRoot();

    if (root.empty())
        throw std::runtime_error(
            "Unable to locate %APPDATA%\\.minecraft."
        );

    std::filesystem::create_directories(
        std::filesystem::path(root) / L"versions"
    );

    JsonValue manifest =
        JsonLite::parse(
            HttpClient::getUtf8(
                AppConfig::MOJANG_VERSION_MANIFEST
            )
        );

    std::string metadataUrl;

    for (const JsonValue& entry :
         manifest.get("versions").asArray())
    {
        if (
            utf8ToWide(
                entry.get("id").asString()
            ) == minecraftVersion
        )
        {
            metadataUrl =
                entry.get("url").asString();
            break;
        }
    }

    if (metadataUrl.empty())
        throw std::runtime_error(
            "The requested Minecraft version was not found in Mojang's version manifest."
        );

    const std::string versionJson =
        HttpClient::getUtf8(
            utf8ToWide(metadataUrl)
        );

    JsonValue versionRoot =
        JsonLite::parse(versionJson);

    const std::wstring clientUrl =
        utf8ToWide(
            versionRoot
                .get("downloads")
                .get("client")
                .get("url")
                .asString()
        );

    if (clientUrl.empty())
        throw std::runtime_error(
            "Mojang version metadata does not contain a client download."
        );

    const std::filesystem::path versionDir =
        std::filesystem::path(root) /
        L"versions" /
        minecraftVersion;

    std::filesystem::create_directories(
        versionDir
    );

    const std::filesystem::path jsonPath =
        versionDir /
        (minecraftVersion + L".json");

    const std::filesystem::path jarPath =
        versionDir /
        (minecraftVersion + L".jar");

    // Write metadata first. The official launcher can use this metadata to
    // resolve libraries/assets it does not already have.
    writeUtf8File(
        jsonPath,
        versionJson
    );

    if (!std::filesystem::exists(jarPath))
    {
        HttpClient::downloadToFile(
            clientUrl,
            jarPath.wstring()
        );
    }

    if (!isMinecraftBaseInstalled(minecraftVersion))
        throw std::runtime_error(
            "Minecraft base version download completed, but the version files are still incomplete."
        );
}

bool VersionManager::isInstalled(
    const VersionPackageInfo& package)
{
    const std::wstring root =
        minecraftRoot();

    if (root.empty())
        return false;

    const std::filesystem::path versionDir =
        std::filesystem::path(root) /
        L"versions" /
        package.versionId;

    const std::filesystem::path json =
        versionDir /
        (
            package.versionId +
            L".json"
        );

    return
        std::filesystem::exists(versionDir) &&
        std::filesystem::exists(json);
}

void VersionManager::install(
    const VersionPackageInfo& package)
{
    const std::wstring root =
        minecraftRoot();

    if (root.empty())
        throw std::runtime_error(
            "Unable to locate the existing .minecraft directory."
        );

    if (!std::filesystem::exists(root))
        throw std::runtime_error(
            "Minecraft Java has not created %APPDATA%\\.minecraft yet."
        );

    const std::filesystem::path cache =
        std::filesystem::path(root) /
        L"newttech-cache";

    std::filesystem::create_directories(cache);

    const std::filesystem::path archive =
        cache /
        (
            L"version-" +
            package.loaderVersion +
            L".zip"
        );

    const std::filesystem::path extractDir =
        cache /
        (
            L"extract-" +
            package.loaderVersion
        );

    HttpClient::downloadToFile(
        package.archiveUrl,
        archive.wstring()
    );

    if (!package.sha256.empty())
    {
        const std::wstring actual =
            sha256File(
                archive.wstring()
            );

        if (
            lower(actual) !=
            lower(package.sha256)
        )
        {
            std::filesystem::remove(archive);

            throw std::runtime_error(
                "Loader version package failed SHA-256 verification."
            );
        }
    }

    if (std::filesystem::exists(extractDir))
        std::filesystem::remove_all(extractDir);

    std::filesystem::create_directories(extractDir);

    const std::wstring script =
        L"$ErrorActionPreference='Stop'; "
        L"Expand-Archive -LiteralPath " +
        quote(archive.wstring()) +
        L" -DestinationPath " +
        quote(extractDir.wstring()) +
        L" -Force; "
        L"Get-ChildItem -LiteralPath " +
        quote(extractDir.wstring()) +
        L" | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination " +
        quote(root) +
        L" -Recurse -Force };";

    if (!runPowerShell(script))
        throw std::runtime_error(
            "Unable to extract the loader version package into .minecraft."
        );

    if (!isInstalled(package))
        throw std::runtime_error(
            "Loader package extracted, but the expected version profile was not found."
        );

    std::filesystem::remove_all(extractDir);
}

VersionPackageInfo VersionManager::ensurePackRuntime(
    const PackManifest& manifest)
{
    ensureMinecraftBaseVersion(
        manifest.minecraft.version
    );

    VersionPackageInfo package =
        fetchPackageInfo(manifest);

    if (!isInstalled(package))
        install(package);

    return package;
}

bool VersionManager::runPowerShell(
    const std::wstring& script)
{
    std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command " +
        quote(script);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    PROCESS_INFORMATION process{};

    std::vector<wchar_t> mutableCommand(
        command.begin(),
        command.end()
    );

    mutableCommand.push_back(L'\0');

    if (!CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process))
    {
        return false;
    }

    WaitForSingleObject(
        process.hProcess,
        INFINITE
    );

    DWORD exitCode = 1;

    GetExitCodeProcess(
        process.hProcess,
        &exitCode
    );

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    return exitCode == 0;
}

std::wstring VersionManager::utf8ToWide(
    const std::string& value)
{
    if (value.empty())
        return {};

    int length =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0
        );

    std::wstring result(
        length,
        L'\0'
    );

    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        length
    );

    return result;
}

std::wstring VersionManager::sha256File(
    const std::wstring& path)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD copied = 0;

    if (BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0) < 0)
    {
        throw std::runtime_error(
            "Unable to initialize SHA-256."
        );
    }

    if (
        BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength),
            &copied,
            0
        ) < 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength),
            sizeof(hashLength),
            &copied,
            0
        ) < 0
    )
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error(
            "Unable to configure SHA-256."
        );
    }

    std::vector<unsigned char> object(objectLength);
    std::vector<unsigned char> digest(hashLength);

    if (BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            objectLength,
            nullptr,
            0,
            0) < 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error(
            "Unable to create SHA-256 hash."
        );
    }

    std::ifstream file(
        std::filesystem::path(path),
        std::ios::binary
    );

    if (!file)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error(
            "Unable to read downloaded version package."
        );
    }

    std::vector<char> buffer(1024 * 1024);

    while (file)
    {
        file.read(
            buffer.data(),
            static_cast<std::streamsize>(buffer.size())
        );

        const std::streamsize read =
            file.gcount();

        if (read > 0)
        {
            if (BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(read),
                    0) < 0)
            {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                throw std::runtime_error(
                    "Unable to hash downloaded version package."
                );
            }
        }
    }

    if (BCryptFinishHash(
            hash,
            digest.data(),
            hashLength,
            0) < 0)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error(
            "Unable to finish SHA-256 hash."
        );
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::wstringstream stream;
    stream << std::hex << std::setfill(L'0');

    for (unsigned char byte : digest)
        stream << std::setw(2) << static_cast<unsigned int>(byte);

    return stream.str();
}
