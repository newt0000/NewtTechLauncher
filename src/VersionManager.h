#pragma once

#include "Models.h"

#include <string>

struct VersionPackageInfo
{
    std::wstring loader;
    std::wstring loaderVersion;
    std::wstring minecraftVersion;

    std::wstring versionId;
    std::wstring archiveUrl;
    std::wstring sha256;
};

class VersionManager
{
public:
    static VersionPackageInfo fetchPackageInfo(
        const PackManifest& manifest
    );

    // Ensures the vanilla Minecraft version JSON/client JAR exist first,
    // then the pack loader version package can be installed normally.
    static void ensureMinecraftBaseVersion(
        const std::wstring& minecraftVersion
    );

    static bool isMinecraftBaseInstalled(
        const std::wstring& minecraftVersion
    );

    static bool isInstalled(
        const VersionPackageInfo& package
    );

    static void install(
        const VersionPackageInfo& package
    );

    static VersionPackageInfo ensurePackRuntime(
        const PackManifest& manifest
    );

    static std::wstring minecraftRoot();

private:
    static std::wstring utf8ToWide(
        const std::string& value
    );

    static std::wstring sha256File(
        const std::wstring& path
    );

    static bool runPowerShell(
        const std::wstring& script
    );
};
