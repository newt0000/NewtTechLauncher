#pragma once

#include "Models.h"

#include <atomic>
#include <functional>
#include <string>

struct InstallProgress
{
    std::wstring title;
    std::wstring detail;
    int currentFile = 0;
    int totalFiles = 0;
    int percent = 0;
    bool active = false;
    bool failed = false;
    bool complete = false;
};

class InstallEngine
{
public:
    using ProgressCallback =
        std::function<void(const InstallProgress&)>;

    static bool isInstalled(
        const PackManifest& manifest,
        const std::wstring& instanceRoot
    );

    static void installOrRepair(
        const PackManifest& manifest,
        const std::wstring& instanceRoot,
        const ProgressCallback& callback
    );

    static std::wstring sha256File(
        const std::wstring& path
    );

    static std::wstring packInstanceRoot(
        const std::wstring& root,
        const std::wstring& packId
    );
};
