#pragma once

#include <string>

struct LauncherSettings
{
    std::wstring installRoot;
    int memoryMb = 8192;

    static LauncherSettings load();
    void save() const;

    static std::wstring defaultInstallRoot();
    static std::wstring configPath();
};
