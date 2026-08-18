#pragma once

#include "Models.h"
#include "VersionManager.h"

#include <string>

class MinecraftProfile
{
public:
    static bool createOrUpdate(
        const PackManifest& manifest,
        const VersionPackageInfo& version,
        const std::wstring& instanceRoot,
        const std::wstring& iconUrl,
        int memoryMb
    );

    static bool openOfficialLauncher();

private:
    static std::wstring launcherProfilesFile(
        const std::wstring& minecraftRoot
    );

    static std::wstring iconFromUrl(
        const std::wstring& iconUrl
    );

    static bool writeWithPowerShell(
        const std::wstring& profileFile,
        const std::wstring& profileKey,
        const std::wstring& name,
        const std::wstring& gameDir,
        const std::wstring& versionId,
        const std::wstring& icon,
        int memoryMb
    );

    static std::wstring escapePs(
        const std::wstring& value
    );

    static std::wstring findLauncherExecutable();
};
