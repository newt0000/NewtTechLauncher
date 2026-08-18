#pragma once

#include "Models.h"

#include <string>

struct MinecraftPreparationResult
{
    bool ready = false;
    bool forgeInstalledNow = false;

    std::wstring minecraftRoot;
    std::wstring javaExecutable;
    std::wstring forgeVersionId;
    std::wstring profileFile;
    std::wstring message;
};

class MinecraftIntegration
{
public:
    static MinecraftPreparationResult prepare(
        const PackManifest& manifest,
        const std::wstring& instanceRoot,
        int memoryMb
    );

    static bool launchOfficialLauncher();

    static std::wstring minecraftRoot();
    static std::wstring findJavaExecutable(
        const std::wstring& minecraftRoot
    );

private:
    static std::wstring detectForgeVersionId(
        const std::wstring& minecraftRoot,
        const PackManifest& manifest
    );

    static bool installForge(
        const std::wstring& minecraftRoot,
        const PackManifest& manifest,
        const std::wstring& javaExecutable
    );

    static bool writeLauncherProfile(
        const std::wstring& minecraftRoot,
        const PackManifest& manifest,
        const std::wstring& gameDirectory,
        const std::wstring& forgeVersionId,
        int memoryMb,
        std::wstring& profileFile
    );

    static std::wstring findLauncherExecutable();
    static std::wstring escapePowerShellSingleQuoted(
        const std::wstring& value
    );
};
