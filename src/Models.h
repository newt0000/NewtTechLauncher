#pragma once

#include <string>
#include <vector>

struct Modpack
{
    std::wstring id;
    std::wstring name;
    std::wstring description;
    std::wstring iconUrl;
    std::wstring bannerUrl;
    std::wstring manifestUrl;
    std::wstring accent = L"#ff23b4";
    bool enabled = true;
    bool featured = false;
};

struct MinecraftInfo
{
    std::wstring version;
    std::wstring loader;
    std::wstring loaderVersion;
};

struct JavaInfo
{
    int minimumVersion = 17;
    int recommendedMemory = 8192;
};

struct ServerInfo
{
    std::wstring address;
};

struct PackFile
{
    std::wstring path;
    std::wstring url;
    long long size = 0;
    std::wstring sha256;
    std::wstring policy = L"replace";
};

struct PackManifest
{
    std::wstring id;
    std::wstring name;
    std::wstring version;
    MinecraftInfo minecraft;
    JavaInfo java;
    ServerInfo server;
    std::vector<PackFile> files;
};
