#pragma once

#include <windows.h>

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "InstallEngine.h"
#include "Models.h"
#include "NewsManager.h"
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
    std::vector<NewsItem> news_;
    std::wstring newsStatus_ = L"News has not been loaded yet.";
    int openNewsIndex_ = -1;

    int newsModalScrollY_ = 0;
    int newsModalMaxScroll_ = 0;
    bool newsModalScrollbarDragging_ = false;
    int newsModalScrollbarDragOffset_ = 0;

    struct NewsMarkdownLink
    {
        RECT rect{};
        std::wstring url;
    };

    std::vector<NewsMarkdownLink> newsModalLinks_;
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
    void drawBitmapFit(HDC dc, HBITMAP bitmap, const RECT& target);

    void refreshPacks();
    void selectPack(int index);
    void loadManifest(const Modpack& pack);
    void ensureArtwork(const Modpack& pack);

    void refreshNews();
    void ensureNewsArtwork();

    RECT newsRefreshRect(const RECT& client) const;
    RECT newsCardRect(const RECT& client, int index) const;
    RECT newsModalRect(const RECT& client) const;
    RECT newsModalCloseRect(const RECT& client) const;
    RECT newsModalScrollbarTrackRect(const RECT& client) const;
    RECT newsModalScrollbarThumbRect(const RECT& client) const;
    void setNewsModalScroll(int value);
    LONG renderNewsMarkdown(
        HDC dc,
        const std::wstring& markdown,
        const RECT& bounds,
        bool draw
    );
    void paintNewsModal(HDC dc, const RECT& client);

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

    // Home page scrolling
    static constexpr int HOME_CONTENT_HEIGHT = 980;
    int homeScrollY_ = 0;
    bool homeScrollbarDragging_ = false;
    int homeScrollbarDragOffset_ = 0;

    int homeMaxScroll(const RECT& client) const;
    RECT homeScrollbarTrackRect(const RECT& client) const;
    RECT homeScrollbarThumbRect(const RECT& client) const;
    void setHomeScroll(int value, const RECT& client);
    void paintHomeScrollbar(HDC dc, const RECT& client);

    static constexpr int TITLEBAR_HEIGHT = 38;

    void paintTitleBar(HDC dc, const RECT& fullClient);
    RECT titleMinimizeRect(const RECT& fullClient) const;
    RECT titleMaximizeRect(const RECT& fullClient) const;
    RECT titleCloseRect(const RECT& fullClient) const;

    LRESULT hitTestNonClient(int screenX, int screenY) const;
    void applyModernWindowStyle();

};
