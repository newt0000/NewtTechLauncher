#pragma once

#include <windows.h>

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "InstallEngine.h"
#include "Models.h"
#include "Settings.h"

class MainWindow
{
public:
    bool create(HINSTANCE instance, int showCommand);
    int run();

private:
    enum class Page
    {
        Home,
        Modpacks,
        Downloads,
        Settings
    };

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    Page page_ = Page::Modpacks;

    std::vector<Modpack> packs_;
    PackManifest currentManifest_;
    int selectedPack_ = -1;

    std::wstring statusText_ = L"Starting...";
    std::wstring errorText_;

    LauncherSettings settings_;

    InstallProgress installProgress_;
    std::mutex progressMutex_;
    bool installWorkerRunning_ = false;

    HFONT fontNormal_ = nullptr;
    HFONT fontSmall_ = nullptr;
    HFONT fontMeta_ = nullptr;
    HFONT fontTitle_ = nullptr;
    HFONT fontBrand_ = nullptr;
    HFONT fontHero_ = nullptr;

    std::unordered_map<std::wstring, HBITMAP> imageCache_;

    static constexpr UINT WM_INSTALL_PROGRESS = WM_APP + 1;
    static constexpr UINT WM_INSTALL_DONE = WM_APP + 2;

    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);

    void createFonts();
    void destroyResources();

    void paint(HDC dc);
    void paintSidebar(HDC dc, const RECT& client);
    void paintHome(HDC dc, const RECT& client);
    void paintModpacks(HDC dc, const RECT& client);
    void paintDownloads(HDC dc, const RECT& client);
    void paintSettings(HDC dc, const RECT& client);

    void paintPackList(HDC dc, const RECT& client);
    void paintPackDetails(HDC dc, const RECT& client);
    void drawBitmapCover(HDC dc, HBITMAP bitmap, const RECT& target);

    void refreshPacks();
    void selectPack(int index);
    void loadManifest(const Modpack& pack);
    void ensureArtwork(const Modpack& pack);

    void startInstallOrRepair();
    void openInstalledInstance();
    void launchOfficialMinecraftLauncher();
    bool currentPackInstalled() const;

    void openInstallRoot();
    void resetInstallRoot();
    void adjustMemory(int deltaMb);

    void parseIndex(const std::string& json);
    PackManifest parseManifest(const std::string& json);
    static std::wstring utf8ToWide(const std::string& value);

    void setStatus(const std::wstring& text);
    void setError(const std::wstring& text);
    void clearError();

    int hitTestSidebar(int x, int y) const;
    int hitTestPackList(int x, int y) const;

    bool pointInRect(int x, int y, const RECT& r) const;

    RECT refreshRect(const RECT& client) const;
    RECT repairRect(const RECT& client) const;
    RECT installRect(const RECT& client) const;

    RECT openFolderRect(const RECT& client) const;
    RECT resetFolderRect(const RECT& client) const;
    RECT memoryMinusRect(const RECT& client) const;
    RECT memoryPlusRect(const RECT& client) const;

    static constexpr int TITLEBAR_HEIGHT = 38;

    void paintTitleBar(HDC dc, const RECT& fullClient);
    RECT titleMinimizeRect(const RECT& fullClient) const;
    RECT titleMaximizeRect(const RECT& fullClient) const;
    RECT titleCloseRect(const RECT& fullClient) const;

    LRESULT hitTestNonClient(int screenX, int screenY) const;
    void applyModernWindowStyle();

};
