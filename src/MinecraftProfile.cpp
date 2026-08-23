#include "MinecraftProfile.h"

#include "HttpClient.h"

#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>

namespace
{
std::wstring quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

std::wstring knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR path = nullptr;

    if (FAILED(
        SHGetKnownFolderPath(
            id,
            0,
            nullptr,
            &path
        )
    ))
    {
        return {};
    }

    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

bool runAndWait(
    const std::wstring& command)
{
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
}

bool MinecraftProfile::createOrUpdate(
    const PackManifest& manifest,
    const VersionPackageInfo& version,
    const std::wstring& instanceRoot,
    const std::wstring& iconUrl,
    int memoryMb)
{
    const std::wstring minecraftRoot =
        VersionManager::minecraftRoot();

    if (minecraftRoot.empty())
        return false;

    const std::filesystem::path expectedVersion =
        std::filesystem::path(minecraftRoot) /
        L"versions" /
        version.versionId;

    if (!std::filesystem::exists(expectedVersion))
        return false;

    const std::wstring gameDir =
        (
            std::filesystem::path(instanceRoot) /
            manifest.id
        ).wstring();

    std::filesystem::create_directories(
        gameDir
    );

    const std::wstring profileKey =
        L"newttech_" +
        manifest.id;

    const std::wstring name =
        L"NewtTech - " +
        manifest.name;

    const std::wstring icon =
        iconFromUrl(iconUrl);

    const int adjustedMemory =
        safeMemoryMb(memoryMb);

    const std::vector<std::wstring> files =
        launcherProfileFiles(minecraftRoot);

    bool wroteAtLeastOne = false;

    for (const std::wstring& profileFile : files)
    {
        // Retry a few times because older launcher builds and slower Windows
        // systems can briefly hold or rewrite launcher_profiles*.json.
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            if (writeWithPowerShell(
                    profileFile,
                    profileKey,
                    name,
                    gameDir,
                    version.versionId,
                    icon,
                    adjustedMemory))
            {
                wroteAtLeastOne = true;
                break;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    250 * (attempt + 1)
                )
            );
        }
    }

    return wroteAtLeastOne;
}

std::vector<std::wstring>
MinecraftProfile::launcherProfileFiles(
    const std::wstring& minecraftRoot)
{
    std::vector<std::wstring> result;

    const std::filesystem::path classic =
        std::filesystem::path(minecraftRoot) /
        L"launcher_profiles.json";

    const std::filesystem::path store =
        std::filesystem::path(minecraftRoot) /
        L"launcher_profiles_microsoft_store.json";

    if (std::filesystem::exists(classic))
        result.push_back(classic.wstring());

    if (std::filesystem::exists(store))
        result.push_back(store.wstring());

    // Older/alternate launcher installs normally use launcher_profiles.json.
    // Create it if neither known profile file exists.
    if (result.empty())
    {
        std::ofstream output(
            classic,
            std::ios::binary | std::ios::trunc
        );

        if (!output)
            return {};

        output <<
            "{\n"
            "  \"profiles\": {},\n"
            "  \"settings\": {},\n"
            "  \"version\": 3\n"
            "}\n";

        result.push_back(
            classic.wstring()
        );
    }

    // Back up every profile file we intend to touch.
    for (const std::wstring& file : result)
    {
        try
        {
            const std::filesystem::path source(file);
            const std::filesystem::path backup =
                source.parent_path() /
                (
                    source.stem().wstring() +
                    L".newttech-backup.json"
                );

            std::filesystem::copy_file(
                source,
                backup,
                std::filesystem::copy_options::overwrite_existing
            );
        }
        catch (...)
        {
        }
    }

    return result;
}

int MinecraftProfile::safeMemoryMb(
    int requestedMb)
{
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);

    if (!GlobalMemoryStatusEx(&memory))
        return std::clamp(
            requestedMb,
            2048,
            16384
        );

    const unsigned long long totalMb =
        memory.ullTotalPhys /
        (1024ull * 1024ull);

    int maximum = 16384;

    if (totalMb <= 10ull * 1024ull)
        maximum = 4096;
    else if (totalMb <= 16ull * 1024ull)
        maximum = 6144;
    else if (totalMb <= 24ull * 1024ull)
        maximum = 8192;
    else
        maximum = 12288;

    return std::clamp(
        requestedMb,
        2048,
        maximum
    );
}

std::wstring MinecraftProfile::iconFromUrl(
    const std::wstring& iconUrl)
{
    if (iconUrl.empty())
        return L"Furnace";

    std::string bytes;

    try
    {
        bytes =
            HttpClient::getUtf8(iconUrl);
    }
    catch (...)
    {
        return L"Furnace";
    }

    if (bytes.empty())
        return L"Furnace";

    DWORD outputLength = 0;

    if (!CryptBinaryToStringA(
            reinterpret_cast<const BYTE*>(bytes.data()),
            static_cast<DWORD>(bytes.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            nullptr,
            &outputLength))
    {
        return L"Furnace";
    }

    std::string encoded(
        outputLength,
        '\0'
    );

    if (!CryptBinaryToStringA(
            reinterpret_cast<const BYTE*>(bytes.data()),
            static_cast<DWORD>(bytes.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            encoded.data(),
            &outputLength))
    {
        return L"Furnace";
    }

    if (
        !encoded.empty() &&
        encoded.back() == '\0'
    )
    {
        encoded.pop_back();
    }

    int wideLength =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            encoded.data(),
            static_cast<int>(encoded.size()),
            nullptr,
            0
        );

    std::wstring wide(
        wideLength,
        L'\0'
    );

    MultiByteToWideChar(
        CP_UTF8,
        0,
        encoded.data(),
        static_cast<int>(encoded.size()),
        wide.data(),
        wideLength
    );

    return
        L"data:image/png;base64," +
        wide;
}

std::wstring MinecraftProfile::escapePs(
    const std::wstring& value)
{
    std::wstring result;

    for (wchar_t c : value)
    {
        if (c == L'\'')
            result += L"''";
        else
            result += c;
    }

    return result;
}

bool MinecraftProfile::writeWithPowerShell(
    const std::wstring& profileFile,
    const std::wstring& profileKey,
    const std::wstring& name,
    const std::wstring& gameDir,
    const std::wstring& versionId,
    const std::wstring& icon,
    int memoryMb)
{
    const std::filesystem::path path(profileFile);
    const std::filesystem::path minecraftRoot =
        path.parent_path();

    const std::filesystem::path scriptPath =
        minecraftRoot /
        L"newttech-profile.ps1";

    const std::filesystem::path resultPath =
        minecraftRoot /
        L"newttech-profile-result.txt";

    const std::filesystem::path tempPath =
        path.parent_path() /
        (
            path.filename().wstring() +
            L".newttech.tmp"
        );

    std::wofstream script(
        scriptPath,
        std::ios::trunc
    );

    if (!script)
        return false;

    script <<
        L"$ErrorActionPreference='Stop'\n"
        L"$path='" << escapePs(profileFile) << L"'\n"
        L"$tmp='" << escapePs(tempPath.wstring()) << L"'\n"
        L"$result='" << escapePs(resultPath.wstring()) << L"'\n"
        L"$key='" << escapePs(profileKey) << L"'\n"
        L"$raw=Get-Content -Raw -LiteralPath $path\n"
        L"if([string]::IsNullOrWhiteSpace($raw)){throw 'launcher profile file is empty'}\n"
        L"$obj=$raw|ConvertFrom-Json\n"
        L"if($null -eq $obj.profiles){$obj|Add-Member -NotePropertyName profiles -NotePropertyValue ([pscustomobject]@{}) -Force}\n"
        L"$now=(Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ')\n"
        L"$profile=[pscustomobject]@{\n"
        L" name='" << escapePs(name) << L"'\n"
        L" type='custom'\n"
        L" created=$now\n"
        L" lastUsed=$now\n"
        L" lastVersionId='" << escapePs(versionId) << L"'\n"
        L" gameDir='" << escapePs(gameDir) << L"'\n"
        L" javaArgs='-Xmx" << memoryMb << L"M'\n"
        L" icon='" << escapePs(icon) << L"'\n"
        L"}\n"
        L"$existing=$obj.profiles.PSObject.Properties[$key]\n"
        L"if($null -ne $existing){$existing.Value=$profile}else{$obj.profiles|Add-Member -NotePropertyName $key -NotePropertyValue $profile -Force}\n"
        L"if($obj.PSObject.Properties['selectedProfile']){$obj.selectedProfile=$key}else{$obj|Add-Member -NotePropertyName selectedProfile -NotePropertyValue $key -Force}\n"
        L"$json=$obj|ConvertTo-Json -Depth 64\n"
        L"[System.IO.File]::WriteAllText($tmp,$json,(New-Object System.Text.UTF8Encoding($false)))\n"
        L"$verify=(Get-Content -Raw -LiteralPath $tmp)|ConvertFrom-Json\n"
        L"if($null -eq $verify.profiles.PSObject.Properties[$key]){throw 'Profile verification failed'}\n"
        L"if($verify.profiles.$key.lastVersionId -ne '" << escapePs(versionId) << L"'){throw 'Version ID verification failed'}\n"
        L"if($verify.profiles.$key.gameDir -ne '" << escapePs(gameDir) << L"'){throw 'Game directory verification failed'}\n"
        L"Move-Item -LiteralPath $tmp -Destination $path -Force\n"
        L"$verify2=(Get-Content -Raw -LiteralPath $path)|ConvertFrom-Json\n"
        L"if($null -eq $verify2.profiles.PSObject.Properties[$key]){throw 'Final profile verification failed'}\n"
        L"[System.IO.File]::WriteAllText($result,'OK',(New-Object System.Text.UTF8Encoding($false)))\n";

    script.close();

    try
    {
        if (std::filesystem::exists(resultPath))
            std::filesystem::remove(resultPath);
        if (std::filesystem::exists(tempPath))
            std::filesystem::remove(tempPath);
    }
    catch (...)
    {
    }

    const std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " +
        quote(scriptPath.wstring());

    if (!runAndWait(command))
        return false;

    if (!std::filesystem::exists(resultPath))
        return false;

    std::ifstream resultFile(
        resultPath,
        std::ios::binary
    );

    std::string result(
        (std::istreambuf_iterator<char>(resultFile)),
        std::istreambuf_iterator<char>()
    );

    return result == "OK";
}

std::wstring MinecraftProfile::findLauncherExecutable()
{
    const std::wstring pf86 =
        knownFolder(FOLDERID_ProgramFilesX86);

    if (!pf86.empty())
    {
        const std::filesystem::path candidate =
            std::filesystem::path(pf86) /
            L"Minecraft Launcher" /
            L"MinecraftLauncher.exe";

        if (std::filesystem::exists(candidate))
            return candidate.wstring();
    }

    const std::wstring pf =
        knownFolder(FOLDERID_ProgramFiles);

    if (!pf.empty())
    {
        const std::filesystem::path candidate =
            std::filesystem::path(pf) /
            L"Minecraft Launcher" /
            L"MinecraftLauncher.exe";

        if (std::filesystem::exists(candidate))
            return candidate.wstring();
    }

    const std::wstring local =
        knownFolder(FOLDERID_LocalAppData);

    if (!local.empty())
    {
        const std::filesystem::path candidate =
            std::filesystem::path(local) /
            L"Programs" /
            L"Minecraft Launcher" /
            L"MinecraftLauncher.exe";

        if (std::filesystem::exists(candidate))
            return candidate.wstring();
    }

    return {};
}

bool MinecraftProfile::openOfficialLauncher()
{
    const std::wstring exe =
        findLauncherExecutable();

    if (!exe.empty())
    {
        HINSTANCE result =
            ShellExecuteW(
                nullptr,
                L"open",
                exe.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL
            );

        return
            reinterpret_cast<INT_PTR>(result) > 32;
    }

    HINSTANCE result =
        ShellExecuteW(
            nullptr,
            L"open",
            L"shell:AppsFolder\\Microsoft.4297127D64EC6_8wekyb3d8bbwe!Minecraft",
            nullptr,
            nullptr,
            SW_SHOWNORMAL
        );

    return
        reinterpret_cast<INT_PTR>(result) > 32;
}
