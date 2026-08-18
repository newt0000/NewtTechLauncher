#include "MinecraftIntegration.h"

#include "HttpClient.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
std::wstring quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

bool runAndWait(
    const std::wstring& executable,
    const std::wstring& arguments,
    const std::wstring& workingDirectory,
    DWORD& exitCode)
{
    std::wstring commandLine =
        quote(executable) + L" " + arguments;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    PROCESS_INFORMATION process{};

    std::vector<wchar_t> mutableCommand(
        commandLine.begin(),
        commandLine.end()
    );
    mutableCommand.push_back(L'\0');

    BOOL created = CreateProcessW(
        executable.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startup,
        &process
    );

    if (!created)
        return false;

    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exitCode);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    return true;
}

std::wstring knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR path = nullptr;

    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
        return {};

    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}
}

MinecraftPreparationResult MinecraftIntegration::prepare(
    const PackManifest& manifest,
    const std::wstring& instanceRoot,
    int memoryMb)
{
    MinecraftPreparationResult result;

    if (
        manifest.minecraft.version.empty() ||
        manifest.minecraft.loader.empty() ||
        manifest.minecraft.loaderVersion.empty()
    )
    {
        result.message =
            L"The pack manifest is missing Minecraft or loader version information.";
        return result;
    }

    result.minecraftRoot = minecraftRoot();

    if (
        result.minecraftRoot.empty() ||
        !std::filesystem::exists(result.minecraftRoot)
    )
    {
        result.message =
            L"Minecraft Java data was not found under %APPDATA%\\.minecraft. "
            L"Open Minecraft Java Edition once from the official launcher, then try again.";
        return result;
    }

    const std::filesystem::path vanillaVersion =
        std::filesystem::path(result.minecraftRoot) /
        L"versions" /
        manifest.minecraft.version;

    if (!std::filesystem::exists(vanillaVersion))
    {
        result.message =
            L"Minecraft Java " +
            manifest.minecraft.version +
            L" is not installed in the official launcher yet. "
            L"Run that vanilla version once, close Minecraft, and press Play again.";
        return result;
    }

    result.javaExecutable =
        findJavaExecutable(result.minecraftRoot);

    if (result.javaExecutable.empty())
    {
        result.message =
            L"No Java runtime was found in the existing Minecraft installation.";
        return result;
    }

    result.forgeVersionId =
        detectForgeVersionId(
            result.minecraftRoot,
            manifest
        );

    if (result.forgeVersionId.empty())
    {
        if (_wcsicmp(manifest.minecraft.loader.c_str(), L"Forge") != 0)
        {
            result.message =
                L"Automatic loader preparation currently supports Forge packs.";
            return result;
        }

        if (!installForge(
            result.minecraftRoot,
            manifest,
            result.javaExecutable
        ))
        {
            result.message =
                L"Forge installation failed. Check that Minecraft Java is closed and try again.";
            return result;
        }

        result.forgeInstalledNow = true;

        result.forgeVersionId =
            detectForgeVersionId(
                result.minecraftRoot,
                manifest
            );

        if (result.forgeVersionId.empty())
        {
            result.message =
                L"Forge installer completed, but the installed Forge profile could not be found.";
            return result;
        }
    }

    const std::wstring gameDirectory =
        (
            std::filesystem::path(instanceRoot) /
            manifest.id
        ).wstring();

    std::filesystem::create_directories(gameDirectory);

    if (!writeLauncherProfile(
        result.minecraftRoot,
        manifest,
        gameDirectory,
        result.forgeVersionId,
        memoryMb,
        result.profileFile
    ))
    {
        result.message =
            L"Forge is installed, but the NewtTech profile could not be written to the Minecraft Launcher.";
        return result;
    }

    result.ready = true;
    result.message =
        L"Java Edition profile prepared: " +
        manifest.name +
        L" (" +
        result.forgeVersionId +
        L")";

    return result;
}

std::wstring MinecraftIntegration::minecraftRoot()
{
    const std::wstring roaming =
        knownFolder(FOLDERID_RoamingAppData);

    if (roaming.empty())
        return {};

    return (
        std::filesystem::path(roaming) /
        L".minecraft"
    ).wstring();
}

std::wstring MinecraftIntegration::findJavaExecutable(
    const std::wstring& root)
{
    const std::filesystem::path runtime =
        std::filesystem::path(root) /
        L"runtime";

    if (std::filesystem::exists(runtime))
    {
        try
        {
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(
                     runtime,
                     std::filesystem::directory_options::skip_permission_denied
                 ))
            {
                if (
                    entry.is_regular_file() &&
                    _wcsicmp(
                        entry.path().filename().c_str(),
                        L"javaw.exe"
                    ) == 0
                )
                {
                    return entry.path().wstring();
                }
            }
        }
        catch (...)
        {
        }
    }

    wchar_t javaHome[4096]{};

    DWORD count = GetEnvironmentVariableW(
        L"JAVA_HOME",
        javaHome,
        static_cast<DWORD>(std::size(javaHome))
    );

    if (count > 0)
    {
        const std::filesystem::path candidate =
            std::filesystem::path(javaHome) /
            L"bin" /
            L"javaw.exe";

        if (std::filesystem::exists(candidate))
            return candidate.wstring();
    }

    return {};
}

std::wstring MinecraftIntegration::detectForgeVersionId(
    const std::wstring& root,
    const PackManifest& manifest)
{
    const std::filesystem::path versions =
        std::filesystem::path(root) /
        L"versions";

    if (!std::filesystem::exists(versions))
        return {};

    const std::wstring exactName =
        manifest.minecraft.version +
        L"-forge-" +
        manifest.minecraft.loaderVersion;

    if (std::filesystem::exists(versions / exactName))
        return exactName;

    try
    {
        for (const auto& entry :
             std::filesystem::directory_iterator(versions))
        {
            if (!entry.is_directory())
                continue;

            const std::wstring name =
                entry.path().filename().wstring();

            if (
                name.find(manifest.minecraft.version) != std::wstring::npos &&
                name.find(manifest.minecraft.loaderVersion) != std::wstring::npos &&
                (
                    name.find(L"forge") != std::wstring::npos ||
                    name.find(L"Forge") != std::wstring::npos
                )
            )
            {
                return name;
            }
        }
    }
    catch (...)
    {
    }

    return {};
}

bool MinecraftIntegration::installForge(
    const std::wstring& root,
    const PackManifest& manifest,
    const std::wstring& javaExecutable)
{
    const std::wstring combined =
        manifest.minecraft.version +
        L"-" +
        manifest.minecraft.loaderVersion;

    const std::wstring url =
        L"https://maven.minecraftforge.net/net/minecraftforge/forge/" +
        combined +
        L"/forge-" +
        combined +
        L"-installer.jar";

    const std::filesystem::path cacheDir =
        std::filesystem::path(root) /
        L"newttech-cache";

    std::filesystem::create_directories(cacheDir);

    const std::filesystem::path installer =
        cacheDir /
        (
            L"forge-" +
            combined +
            L"-installer.jar"
        );

    try
    {
        HttpClient::downloadToFile(
            url,
            installer.wstring()
        );
    }
    catch (...)
    {
        return false;
    }

    DWORD exitCode = 1;

    if (
        runAndWait(
            javaExecutable,
            L"-jar " +
                quote(installer.wstring()) +
                L" --installClient",
            root,
            exitCode
        ) &&
        exitCode == 0
    )
    {
        return true;
    }

    HINSTANCE opened = ShellExecuteW(
        nullptr,
        L"open",
        javaExecutable.c_str(),
        (
            L"-jar " +
            quote(installer.wstring())
        ).c_str(),
        root.c_str(),
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<INT_PTR>(opened) <= 32)
        return false;

    MessageBoxW(
        nullptr,
        L"The official Forge installer has been opened.\n\n"
        L"Choose Install Client and let it finish. Then click OK here.",
        L"NewtTech Launcher",
        MB_OK | MB_ICONINFORMATION
    );

    return true;
}

std::wstring MinecraftIntegration::escapePowerShellSingleQuoted(
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

bool MinecraftIntegration::writeLauncherProfile(
    const std::wstring& root,
    const PackManifest& manifest,
    const std::wstring& gameDirectory,
    const std::wstring& forgeVersionId,
    int memoryMb,
    std::wstring& profileFile)
{
    std::filesystem::path target =
        std::filesystem::path(root) /
        L"launcher_profiles_microsoft_store.json";

    if (!std::filesystem::exists(target))
    {
        target =
            std::filesystem::path(root) /
            L"launcher_profiles.json";
    }

    if (!std::filesystem::exists(target))
    {
        std::ofstream blank(
            target,
            std::ios::binary | std::ios::trunc
        );

        blank <<
            "{\n"
            "  \"profiles\": {},\n"
            "  \"settings\": {},\n"
            "  \"version\": 3\n"
            "}\n";
    }

    profileFile = target.wstring();

    const std::wstring profileKey =
        L"newttech_" + manifest.id;

    const std::wstring profileName =
        L"NewtTech - " + manifest.name;

    const std::wstring javaArgs =
        L"-Xmx" +
        std::to_wstring(memoryMb) +
        L"M";

    const std::filesystem::path scriptPath =
        std::filesystem::path(root) /
        L"newttech-profile.ps1";

    std::wofstream script(scriptPath, std::ios::trunc);

    if (!script)
        return false;

    script
        << L"$ErrorActionPreference = 'Stop'\n"
        << L"$path = '" << escapePowerShellSingleQuoted(profileFile) << L"'\n"
        << L"$raw = Get-Content -Raw -LiteralPath $path\n"
        << L"if ([string]::IsNullOrWhiteSpace($raw)) { $obj = [pscustomobject]@{} } else { $obj = $raw | ConvertFrom-Json }\n"
        << L"if ($null -eq $obj.profiles) { $obj | Add-Member -NotePropertyName profiles -NotePropertyValue ([pscustomobject]@{}) }\n"
        << L"$profile = [pscustomobject]@{\n"
        << L"  name = '" << escapePowerShellSingleQuoted(profileName) << L"'\n"
        << L"  type = 'custom'\n"
        << L"  created = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ')\n"
        << L"  lastUsed = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ')\n"
        << L"  lastVersionId = '" << escapePowerShellSingleQuoted(forgeVersionId) << L"'\n"
        << L"  gameDir = '" << escapePowerShellSingleQuoted(gameDirectory) << L"'\n"
        << L"  javaArgs = '" << escapePowerShellSingleQuoted(javaArgs) << L"'\n"
        << L"}\n"
        << L"$key = '" << escapePowerShellSingleQuoted(profileKey) << L"'\n"
        << L"$existing = $obj.profiles.PSObject.Properties[$key]\n"
        << L"if ($null -ne $existing) { $existing.Value = $profile } else { $obj.profiles | Add-Member -NotePropertyName $key -NotePropertyValue $profile }\n"
        << L"if ($obj.PSObject.Properties['selectedProfile']) { $obj.selectedProfile = $key } else { $obj | Add-Member -NotePropertyName selectedProfile -NotePropertyValue $key }\n"
        << L"$obj | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $path -Encoding UTF8\n";

    script.close();

    DWORD exitCode = 1;

    return
        runAndWait(
            L"powershell.exe",
            L"-NoProfile -ExecutionPolicy Bypass -File " +
                quote(scriptPath.wstring()),
            root,
            exitCode
        ) &&
        exitCode == 0;
}

std::wstring MinecraftIntegration::findLauncherExecutable()
{
    std::vector<std::filesystem::path> candidates;

    const std::wstring pf86 =
        knownFolder(FOLDERID_ProgramFilesX86);

    if (!pf86.empty())
    {
        candidates.push_back(
            std::filesystem::path(pf86) /
            L"Minecraft Launcher" /
            L"MinecraftLauncher.exe"
        );
    }

    const std::wstring local =
        knownFolder(FOLDERID_LocalAppData);

    if (!local.empty())
    {
        candidates.push_back(
            std::filesystem::path(local) /
            L"Programs" /
            L"Minecraft Launcher" /
            L"MinecraftLauncher.exe"
        );
    }

    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
            return candidate.wstring();
    }

    return {};
}

bool MinecraftIntegration::launchOfficialLauncher()
{
    const std::wstring executable =
        findLauncherExecutable();

    if (!executable.empty())
    {
        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"open",
            executable.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL
        );

        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        L"shell:AppsFolder\\Microsoft.4297127D64EC6_8wekyb3d8bbwe!Minecraft",
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );

    return reinterpret_cast<INT_PTR>(result) > 32;
}
