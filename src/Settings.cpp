#include "Settings.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>

std::wstring LauncherSettings::defaultInstallRoot()
{
    PWSTR roaming = nullptr;

    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            0,
            nullptr,
            &roaming)))
    {
        return L".\\instances";
    }

    std::wstring result =
        std::wstring(roaming) +
        L"\\NewtTech Launcher\\instances";

    CoTaskMemFree(roaming);

    return result;
}

std::wstring LauncherSettings::configPath()
{
    PWSTR roaming = nullptr;

    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            0,
            nullptr,
            &roaming)))
    {
        return L"launcher-settings.ini";
    }

    std::wstring result =
        std::wstring(roaming) +
        L"\\NewtTech Launcher\\settings.ini";

    CoTaskMemFree(roaming);

    return result;
}

LauncherSettings LauncherSettings::load()
{
    LauncherSettings settings;
    settings.installRoot = defaultInstallRoot();

    const std::wstring path = configPath();

    wchar_t rootBuffer[4096]{};

    GetPrivateProfileStringW(
        L"Launcher",
        L"InstallRoot",
        settings.installRoot.c_str(),
        rootBuffer,
        static_cast<DWORD>(std::size(rootBuffer)),
        path.c_str()
    );

    settings.installRoot = rootBuffer;

    settings.memoryMb =
        GetPrivateProfileIntW(
            L"Launcher",
            L"MemoryMB",
            8192,
            path.c_str()
        );

    return settings;
}

void LauncherSettings::save() const
{
    const std::filesystem::path path(configPath());
    std::filesystem::create_directories(path.parent_path());

    WritePrivateProfileStringW(
        L"Launcher",
        L"InstallRoot",
        installRoot.c_str(),
        path.c_str()
    );

    WritePrivateProfileStringW(
        L"Launcher",
        L"MemoryMB",
        std::to_wstring(memoryMb).c_str(),
        path.c_str()
    );
}
