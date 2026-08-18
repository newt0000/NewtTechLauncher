#include "MainWindow.h"

#include "AppConfig.h"
#include "HttpClient.h"
#include "ImageLoader.h"
#include "JsonLite.h"
#include "VersionManager.h"
#include "MinecraftProfile.h"

#include <objbase.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include <dwmapi.h>

namespace
{
constexpr COLORREF BG            = RGB(5, 10, 24);
constexpr COLORREF PANEL         = RGB(8, 17, 38);
constexpr COLORREF CARD          = RGB(12, 25, 51);
constexpr COLORREF CARD_SELECTED = RGB(19, 38, 70);
constexpr COLORREF BORDER        = RGB(25, 52, 83);
constexpr COLORREF TEXT          = RGB(239, 247, 255);
constexpr COLORREF TEXT_DARK     = RGB(5, 10, 24);
constexpr COLORREF MUTED         = RGB(124, 151, 178);
constexpr COLORREF ACCENT        = RGB(255, 35, 180);
constexpr COLORREF CYAN          = RGB(0, 225, 255);
constexpr COLORREF SUCCESS       = RGB(84, 230, 196);
constexpr COLORREF ERROR_BG      = RGB(48, 14, 42);
constexpr COLORREF ERROR_TEXT    = RGB(255, 125, 205);

void fillRectColor(
    HDC dc,
    const RECT& rect,
    COLORREF color)
{
    HBRUSH brush =
        CreateSolidBrush(color);

    FillRect(
        dc,
        &rect,
        brush
    );

    DeleteObject(brush);
}

void drawTextSimple(
    HDC dc,
    const std::wstring& text,
    RECT rect,
    HFONT font,
    COLORREF color,
    UINT format)
{
    HFONT oldFont =
        static_cast<HFONT>(
            SelectObject(
                dc,
                font
            )
        );

    SetTextColor(
        dc,
        color
    );

    SetBkMode(
        dc,
        TRANSPARENT
    );

    DrawTextW(
        dc,
        text.c_str(),
        static_cast<int>(
            text.size()
        ),
        &rect,
        format
    );

    SelectObject(
        dc,
        oldFont
    );
}

std::wstring basenameForDisplay(
    const std::wstring& path)
{
    try
    {
        return std::filesystem::path(path)
            .filename()
            .wstring();
    }
    catch (...)
    {
        return path;
    }
}
}

bool MainWindow::create(
    HINSTANCE instance,
    int showCommand)
{
    instance_ = instance;

    CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED
    );

    settings_ =
        LauncherSettings::load();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance_;
    wc.lpfnWndProc =
        &MainWindow::windowProc;
    wc.lpszClassName =
        L"NewtTechLauncherWindow";
    wc.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW
        );
    wc.hbrBackground =
        CreateSolidBrush(BG);

    if (!RegisterClassExW(&wc))
        return false;

    hwnd_ =
        CreateWindowExW(
            0,
            wc.lpszClassName,
            AppConfig::APP_NAME,
            WS_POPUP |
            WS_THICKFRAME |
            WS_MINIMIZEBOX |
            WS_MAXIMIZEBOX |
            WS_SYSMENU,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1180,
            758,
            nullptr,
            nullptr,
            instance_,
            this
        );

    if (!hwnd_)
        return false;

    createFonts();
    applyModernWindowStyle();

    ShowWindow(
        hwnd_,
        showCommand
    );

    UpdateWindow(hwnd_);

    refreshPacks();

    return true;
}

int MainWindow::run()
{
    MSG msg{};

    while (
        GetMessageW(
            &msg,
            nullptr,
            0,
            0
        ) > 0
    )
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(
        msg.wParam
    );
}

LRESULT CALLBACK MainWindow::windowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    MainWindow* self = nullptr;

    if (message == WM_NCCREATE)
    {
        auto* create =
            reinterpret_cast<CREATESTRUCTW*>(
                lParam
            );

        self =
            static_cast<MainWindow*>(
                create->lpCreateParams
            );

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                self
            )
        );

        self->hwnd_ = hwnd;
    }
    else
    {
        self =
            reinterpret_cast<MainWindow*>(
                GetWindowLongPtrW(
                    hwnd,
                    GWLP_USERDATA
                )
            );
    }

    return self
        ? self->handleMessage(
            hwnd,
            message,
            wParam,
            lParam
        )
        : DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam
        );
}

LRESULT MainWindow::handleMessage(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};

            HDC dc =
                BeginPaint(
                    hwnd,
                    &ps
                );

            paint(dc);

            EndPaint(
                hwnd,
                &ps
            );

            return 0;
        }

        case WM_LBUTTONUP:
        {
            const int x =
                GET_X_LPARAM(lParam);

            const int y =
                GET_Y_LPARAM(lParam);

            RECT fullClient{};
            GetClientRect(
                hwnd_,
                &fullClient
            );

            if (y < TITLEBAR_HEIGHT)
            {
                if (pointInRect(x, y, titleCloseRect(fullClient)))
                {
                    SendMessageW(hwnd_, WM_CLOSE, 0, 0);
                    return 0;
                }

                if (pointInRect(x, y, titleMinimizeRect(fullClient)))
                {
                    ShowWindow(hwnd_, SW_MINIMIZE);
                    return 0;
                }

                if (pointInRect(x, y, titleMaximizeRect(fullClient)))
                {
                    ShowWindow(
                        hwnd_,
                        IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE
                    );
                    return 0;
                }
            }

            const int contentY =
                y - TITLEBAR_HEIGHT;

            if (contentY < 0)
                return 0;

            const int nav =
                hitTestSidebar(
                    x,
                    contentY
                );

            if (nav >= 0)
            {
                page_ =
                    static_cast<Page>(nav);

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            RECT client{};
            GetClientRect(
                hwnd_,
                &client
            );

            if (
                page_ == Page::Modpacks &&
                pointInRect(
                    x,
                    y,
                    refreshRect(client)
                )
            )
            {
                refreshPacks();
                return 0;
            }

            if (page_ == Page::Modpacks)
            {
                const int pack =
                    hitTestPackList(
                        x,
                        contentY
                    );

                if (pack >= 0)
                {
                    selectPack(pack);
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        contentY,
                        repairRect(client)
                    )
                )
                {
                    startInstallOrRepair();
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        contentY,
                        installRect(client)
                    )
                )
                {
                    if (currentPackInstalled())
                    {
                        try
                        {
                            const VersionPackageInfo version =
                                VersionManager::fetchPackageInfo(
                                    currentManifest_
                                );

                            if (!VersionManager::isInstalled(version))
                            {
                                setStatus(
                                    L"Downloading " +
                                    currentManifest_.minecraft.loader +
                                    L" " +
                                    currentManifest_.minecraft.loaderVersion +
                                    L"..."
                                );

                                VersionManager::install(version);
                            }

                            const std::wstring iconUrl =
                                (
                                    selectedPack_ >= 0 &&
                                    selectedPack_ < static_cast<int>(packs_.size())
                                )
                                    ? packs_[selectedPack_].iconUrl
                                    : L"";

                            if (!MinecraftProfile::createOrUpdate(
                                    currentManifest_,
                                    version,
                                    settings_.installRoot,
                                    iconUrl,
                                    settings_.memoryMb))
                            {
                                setError(
                                    L"The Minecraft Java installation profile could not be created."
                                );
                                return 0;
                            }

                            setStatus(
                                L"Java profile ready • " +
                                version.versionId
                            );

                            if (!MinecraftProfile::openOfficialLauncher())
                            {
                                setError(
                                    L"The profile was created, but the official Minecraft Launcher could not be opened."
                                );
                            }
                        }
                        catch (const std::exception& error)
                        {
                            setError(
                                L"Unable to prepare Minecraft Java profile.\n\n" +
                                utf8ToWide(error.what())
                            );
                        }
                    }
                    else
                    {
                        startInstallOrRepair();
                    }

                    return 0;
                }
            }

            if (page_ == Page::Home)
            {
                RECT openPacks{
                    250,
                    310,
                    420,
                    356
                };

                if (
                    pointInRect(
                        x,
                        contentY,
                        openPacks
                    )
                )
                {
                    page_ =
                        Page::Modpacks;

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }
            }

            if (page_ == Page::Downloads)
            {
                RECT openFolder{
                    250,
                    430,
                    445,
                    476
                };

                if (
                    pointInRect(
                        x,
                        contentY,
                        openFolder
                    )
                )
                {
                    openInstallRoot();
                    return 0;
                }
            }

            if (page_ == Page::Settings)
            {
                if (
                    pointInRect(
                        x,
                        contentY,
                        openFolderRect(client)
                    )
                )
                {
                    openInstallRoot();
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        contentY,
                        resetFolderRect(client)
                    )
                )
                {
                    resetInstallRoot();
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        contentY,
                        memoryMinusRect(client)
                    )
                )
                {
                    adjustMemory(-1024);
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        contentY,
                        memoryPlusRect(client)
                    )
                )
                {
                    adjustMemory(1024);
                    return 0;
                }
            }

            return 0;
        }

        case WM_INSTALL_PROGRESS:
        {
            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

            return 0;
        }

        case WM_INSTALL_DONE:
        {
            installWorkerRunning_ = false;

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

            return 0;
        }

        case WM_NCCALCSIZE:
        {
            if (wParam)
                return 0;

            break;
        }

        case WM_NCHITTEST:
        {
            return hitTestNonClient(
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            );
        }

        case WM_GETMINMAXINFO:
        {
            auto* info =
                reinterpret_cast<MINMAXINFO*>(
                    lParam
                );

            HMONITOR monitor =
                MonitorFromWindow(
                    hwnd_,
                    MONITOR_DEFAULTTONEAREST
                );

            MONITORINFO monitorInfo{};
            monitorInfo.cbSize =
                sizeof(monitorInfo);

            if (GetMonitorInfoW(
                    monitor,
                    &monitorInfo))
            {
                const RECT work =
                    monitorInfo.rcWork;

                const RECT screen =
                    monitorInfo.rcMonitor;

                info->ptMaxPosition.x =
                    work.left -
                    screen.left;

                info->ptMaxPosition.y =
                    work.top -
                    screen.top;

                info->ptMaxSize.x =
                    work.right -
                    work.left;

                info->ptMaxSize.y =
                    work.bottom -
                    work.top;
            }

            info->ptMinTrackSize.x = 900;
            info->ptMinTrackSize.y = 600;

            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
        {
            destroyResources();

            CoUninitialize();

            PostQuitMessage(0);

            return 0;
        }
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}

void MainWindow::createFonts()
{
    fontNormal_ =
        CreateFontW(
            -16,0,0,0,
            FW_NORMAL,
            FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

    fontSmall_ =
        CreateFontW(
            -13,0,0,0,
            FW_NORMAL,
            FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

    fontMeta_ =
        CreateFontW(
            -12,0,0,0,
            FW_SEMIBOLD,
            FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

    fontTitle_ =
        CreateFontW(
            -30,0,0,0,
            FW_SEMIBOLD,
            FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

    fontBrand_ =
        CreateFontW(
            -24,0,0,0,
            FW_BOLD,
            FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

    fontHero_ =
        CreateFontW(
            -40,0,0,0,
            FW_BOLD,
            FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );
}

void MainWindow::destroyResources()
{
    if (fontNormal_) DeleteObject(fontNormal_);
    if (fontSmall_) DeleteObject(fontSmall_);
    if (fontMeta_) DeleteObject(fontMeta_);
    if (fontTitle_) DeleteObject(fontTitle_);
    if (fontBrand_) DeleteObject(fontBrand_);
    if (fontHero_) DeleteObject(fontHero_);

    for (auto& [_, bitmap] : imageCache_)
        if (bitmap)
            DeleteObject(bitmap);

    imageCache_.clear();
}

void MainWindow::paint(HDC dc)
{
    RECT fullClient{};
    GetClientRect(
        hwnd_,
        &fullClient
    );

    HDC memory =
        CreateCompatibleDC(dc);

    HBITMAP bitmap =
        CreateCompatibleBitmap(
            dc,
            fullClient.right,
            fullClient.bottom
        );

    HBITMAP old =
        static_cast<HBITMAP>(
            SelectObject(
                memory,
                bitmap
            )
        );

    fillRectColor(
        memory,
        fullClient,
        BG
    );

    paintTitleBar(
        memory,
        fullClient
    );

    const int saved =
        SaveDC(memory);

    SetViewportOrgEx(
        memory,
        0,
        TITLEBAR_HEIGHT,
        nullptr
    );

    RECT content{
        0,
        0,
        fullClient.right,
        fullClient.bottom - TITLEBAR_HEIGHT
    };

    paintSidebar(
        memory,
        content
    );

    switch (page_)
    {
        case Page::Home:
            paintHome(
                memory,
                content
            );
            break;

        case Page::Modpacks:
            paintModpacks(
                memory,
                content
            );
            break;

        case Page::Downloads:
            paintDownloads(
                memory,
                content
            );
            break;

        case Page::Settings:
            paintSettings(
                memory,
                content
            );
            break;
    }

    RestoreDC(
        memory,
        saved
    );

    BitBlt(
        dc,
        0,
        0,
        fullClient.right,
        fullClient.bottom,
        memory,
        0,
        0,
        SRCCOPY
    );

    SelectObject(
        memory,
        old
    );

    DeleteObject(bitmap);
    DeleteDC(memory);
}

void MainWindow::paintSidebar(
    HDC dc,
    const RECT& client)
{
    RECT sidebar{
        0,
        0,
        220,
        client.bottom
    };

    fillRectColor(
        dc,
        sidebar,
        PANEL
    );

    fillRectColor(
        dc,
        RECT{
            219,
            0,
            220,
            client.bottom
        },
        BORDER
    );

    drawTextSimple(
        dc,
        L"NEWTTECH",
        RECT{
            16,
            22,
            200,
            50
        },
        fontBrand_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"L A U N C H E R",
        RECT{
            16,
            51,
            205,
            72
        },
        fontSmall_,
        ACCENT,
        DT_LEFT |
        DT_SINGLELINE
    );

    const wchar_t* nav[] = {
        L"Home",
        L"Modpacks",
        L"Downloads",
        L"Settings"
    };

    for (int i = 0; i < 4; ++i)
    {
        RECT row{
            7,
            105 + i * 52,
            195,
            149 + i * 52
        };

        if (
            static_cast<int>(page_) ==
            i
        )
        {
            fillRectColor(
                dc,
                row,
                CARD
            );
        }

        RECT textRect = row;
        textRect.left += 16;

        drawTextSimple(
            dc,
            nav[i],
            textRect,
            fontNormal_,
            static_cast<int>(page_) == i
                ? TEXT
                : MUTED,
            DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE
        );
    }

    RECT status{
        7,
        client.bottom - 90,
        195,
        client.bottom - 20
    };

    fillRectColor(
        dc,
        status,
        CARD
    );

    drawTextSimple(
        dc,
        L"SERVER STATUS",
        RECT{
            19,
            client.bottom - 80,
            185,
            client.bottom - 61
        },
        fontMeta_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        statusText_,
        RECT{
            19,
            client.bottom - 57,
            185,
            client.bottom - 26
        },
        fontSmall_,
        TEXT,
        DT_LEFT |
        DT_WORDBREAK
    );
}

void MainWindow::paintHome(
    HDC dc,
    const RECT& client)
{
    drawTextSimple(
        dc,
        L"Home",
        RECT{
            241,
            24,
            500,
            64
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"Your NewtTech modpacks in one place.",
        RECT{
            241,
            64,
            720,
            92
        },
        fontNormal_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    RECT hero{
        241,
        118,
        client.right - 30,
        290
    };

    fillRectColor(
        dc,
        hero,
        CARD
    );

    std::wstring headline =
        packs_.empty()
            ? L"No modpacks available"
            : L"Ready to play?";

    std::wstring description =
        packs_.empty()
            ? L"The launcher is connected, but the server is not publishing any enabled packs."
            : std::to_wstring(packs_.size()) +
              (packs_.size() == 1
                  ? L" modpack is available."
                  : L" modpacks are available.");

    drawTextSimple(
        dc,
        headline,
        RECT{
            270,
            150,
            client.right - 60,
            205
        },
        fontHero_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        description,
        RECT{
            271,
            215,
            client.right - 60,
            255
        },
        fontNormal_,
        MUTED,
        DT_LEFT |
        DT_WORDBREAK
    );

    RECT openPacks{
        250,
        310,
        420,
        356
    };

    fillRectColor(
        dc,
        openPacks,
        ACCENT
    );

    drawTextSimple(
        dc,
        L"Browse Modpacks",
        openPacks,
        fontNormal_,
        TEXT_DARK,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    RECT card1{
        241,
        388,
        520,
        530
    };

    RECT card2{
        542,
        388,
        821,
        530
    };

    fillRectColor(dc,card1,PANEL);
    fillRectColor(dc,card2,PANEL);

    drawTextSimple(
        dc,
        L"INSTALL LOCATION",
        RECT{262,408,490,430},
        fontMeta_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        basenameForDisplay(
            settings_.installRoot
        ),
        RECT{262,440,492,482},
        fontNormal_,
        TEXT,
        DT_LEFT |
        DT_WORDBREAK |
        DT_END_ELLIPSIS
    );

    InstallProgress progressCopy;
    {
        std::lock_guard<std::mutex> lock(
            progressMutex_
        );
        progressCopy =
            installProgress_;
    }

    drawTextSimple(
        dc,
        L"DOWNLOAD STATUS",
        RECT{563,408,792,430},
        fontMeta_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        progressCopy.active
            ? progressCopy.title
            : L"No active downloads",
        RECT{563,440,792,482},
        fontNormal_,
        progressCopy.active
            ? CYAN
            : TEXT,
        DT_LEFT |
        DT_WORDBREAK
    );
}

void MainWindow::paintModpacks(
    HDC dc,
    const RECT& client)
{
    drawTextSimple(
        dc,
        L"Modpacks",
        RECT{
            241,
            24,
            500,
            62
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"Select an available pack to install, repair, or launch.",
        RECT{
            241,
            63,
            730,
            90
        },
        fontNormal_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    RECT refresh =
        refreshRect(client);

    fillRectColor(
        dc,
        refresh,
        CARD
    );

    drawTextSimple(
        dc,
        L"Refresh",
        refresh,
        fontNormal_,
        TEXT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    paintPackList(
        dc,
        client
    );

    paintPackDetails(
        dc,
        client
    );
}

void MainWindow::paintDownloads(
    HDC dc,
    const RECT& client)
{
    drawTextSimple(
        dc,
        L"Downloads",
        RECT{
            241,
            24,
            600,
            64
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"Installation and repair activity.",
        RECT{
            241,
            64,
            730,
            92
        },
        fontNormal_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    InstallProgress progress;
    {
        std::lock_guard<std::mutex> lock(
            progressMutex_
        );

        progress =
            installProgress_;
    }

    RECT card{
        241,
        118,
        client.right - 30,
        390
    };

    fillRectColor(
        dc,
        card,
        PANEL
    );

    std::wstring title =
        progress.title.empty()
            ? L"No active download"
            : progress.title;

    std::wstring detail =
        progress.detail.empty()
            ? L"Install a modpack or run Verify & Repair to see activity here."
            : progress.detail;

    drawTextSimple(
        dc,
        title,
        RECT{
            270,
            150,
            client.right - 60,
            190
        },
        fontTitle_,
        progress.failed
            ? ERROR_TEXT
            : (
                progress.complete
                    ? SUCCESS
                    : TEXT
            ),
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    drawTextSimple(
        dc,
        detail,
        RECT{
            270,
            200,
            client.right - 60,
            248
        },
        fontNormal_,
        MUTED,
        DT_LEFT |
        DT_WORDBREAK
    );

    RECT progressBg{
        270,
        274,
        client.right - 60,
        290
    };

    fillRectColor(
        dc,
        progressBg,
        CARD
    );

    RECT progressFill =
        progressBg;

    progressFill.right =
        progressFill.left +
        (
            (progressBg.right -
             progressBg.left) *
            std::clamp(
                progress.percent,
                0,
                100
            ) /
            100
        );

    fillRectColor(
        dc,
        progressFill,
        progress.failed
            ? ERROR_TEXT
            : (
                progress.complete
                    ? SUCCESS
                    : CYAN
            )
    );

    const std::wstring stats =
        std::to_wstring(
            std::clamp(
                progress.percent,
                0,
                100
            )
        ) +
        L"%   •   " +
        std::to_wstring(
            progress.currentFile
        ) +
        L" / " +
        std::to_wstring(
            progress.totalFiles
        ) +
        L" files";

    drawTextSimple(
        dc,
        stats,
        RECT{
            270,
            306,
            client.right - 60,
            335
        },
        fontSmall_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    RECT openFolder{
        250,
        430,
        445,
        476
    };

    fillRectColor(
        dc,
        openFolder,
        CARD
    );

    drawTextSimple(
        dc,
        L"Open Install Folder",
        openFolder,
        fontNormal_,
        TEXT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );
}

void MainWindow::paintSettings(
    HDC dc,
    const RECT& client)
{
    drawTextSimple(
        dc,
        L"Settings",
        RECT{
            241,
            24,
            600,
            64
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"Launcher storage and runtime preferences.",
        RECT{
            241,
            64,
            760,
            92
        },
        fontNormal_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    RECT location{
        241,
        118,
        client.right - 30,
        280
    };

    fillRectColor(
        dc,
        location,
        PANEL
    );

    drawTextSimple(
        dc,
        L"INSTALL LOCATION",
        RECT{
            270,
            142,
            client.right - 60,
            164
        },
        fontMeta_,
        CYAN,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        settings_.installRoot,
        RECT{
            270,
            177,
            client.right - 60,
            215
        },
        fontNormal_,
        TEXT,
        DT_LEFT |
        DT_WORDBREAK |
        DT_END_ELLIPSIS
    );

    RECT open =
        openFolderRect(client);

    RECT reset =
        resetFolderRect(client);

    fillRectColor(dc,open,CARD);
    fillRectColor(dc,reset,CARD);

    drawTextSimple(
        dc,
        L"Open Folder",
        open,
        fontNormal_,
        TEXT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"Reset Default",
        reset,
        fontNormal_,
        TEXT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    RECT memory{
        241,
        306,
        client.right - 30,
        466
    };

    fillRectColor(
        dc,
        memory,
        PANEL
    );

    drawTextSimple(
        dc,
        L"RECOMMENDED MEMORY",
        RECT{
            270,
            330,
            client.right - 60,
            352
        },
        fontMeta_,
        CYAN,
        DT_LEFT |
        DT_SINGLELINE
    );

    const std::wstring memoryText =
        std::to_wstring(
            settings_.memoryMb / 1024
        ) +
        L" GB";

    drawTextSimple(
        dc,
        memoryText,
        RECT{
            270,
            370,
            430,
            420
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    RECT minus =
        memoryMinusRect(client);

    RECT plus =
        memoryPlusRect(client);

    fillRectColor(dc,minus,CARD);
    fillRectColor(dc,plus,ACCENT);

    drawTextSimple(
        dc,
        L"− 1 GB",
        minus,
        fontNormal_,
        TEXT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"+ 1 GB",
        plus,
        fontNormal_,
        TEXT_DARK,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"Memory is saved and applied to the generated Minecraft Java installation profile.",
        RECT{
            270,
            425,
            client.right - 60,
            452
        },
        fontSmall_,
        MUTED,
        DT_LEFT |
        DT_WORDBREAK
    );
}

void MainWindow::drawBitmapCover(
    HDC dc,
    HBITMAP bitmap,
    const RECT& target)
{
    if (!bitmap)
        return;

    BITMAP bm{};

    GetObject(
        bitmap,
        sizeof(bm),
        &bm
    );

    HDC source =
        CreateCompatibleDC(dc);

    HBITMAP old =
        static_cast<HBITMAP>(
            SelectObject(
                source,
                bitmap
            )
        );

    SetStretchBltMode(
        dc,
        HALFTONE
    );

    StretchBlt(
        dc,
        target.left,
        target.top,
        target.right - target.left,
        target.bottom - target.top,
        source,
        0,
        0,
        bm.bmWidth,
        bm.bmHeight,
        SRCCOPY
    );

    SelectObject(
        source,
        old
    );

    DeleteDC(source);
}

void MainWindow::paintPackList(
    HDC dc,
    const RECT& client)
{
    RECT panel{
        241,
        107,
        556,
        client.bottom - 22
    };

    fillRectColor(
        dc,
        panel,
        PANEL
    );

    drawTextSimple(
        dc,
        L"AVAILABLE PACKS",
        RECT{
            257,
            124,
            535,
            144
        },
        fontMeta_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    int y = 155;

    for (
        size_t i = 0;
        i < packs_.size();
        ++i
    )
    {
        RECT card{
            251,
            y,
            546,
            y + 76
        };

        fillRectColor(
            dc,
            card,
            static_cast<int>(i) ==
                selectedPack_
                ? CARD_SELECTED
                : CARD
        );

        int textLeft =
            card.left + 14;

        if (!packs_[i].iconUrl.empty())
        {
            auto it =
                imageCache_.find(
                    L"icon:" +
                    packs_[i].iconUrl
                );

            if (
                it != imageCache_.end() &&
                it->second
            )
            {
                RECT icon{
                    card.left + 12,
                    card.top + 12,
                    card.left + 64,
                    card.top + 64
                };

                drawBitmapCover(
                    dc,
                    it->second,
                    icon
                );

                textLeft =
                    card.left + 76;
            }
        }

        drawTextSimple(
            dc,
            packs_[i].name,
            RECT{
                textLeft,
                card.top + 10,
                card.right - 10,
                card.top + 33
            },
            fontNormal_,
            TEXT,
            DT_LEFT |
            DT_SINGLELINE |
            DT_END_ELLIPSIS
        );

        drawTextSimple(
            dc,
            packs_[i].description,
            RECT{
                textLeft,
                card.top + 36,
                card.right - 10,
                card.bottom - 8
            },
            fontSmall_,
            MUTED,
            DT_LEFT |
            DT_WORDBREAK |
            DT_END_ELLIPSIS
        );

        y += 84;
    }
}

void MainWindow::paintPackDetails(
    HDC dc,
    const RECT& client)
{
    RECT panel{
        578,
        107,
        client.right - 30,
        client.bottom - 22
    };

    fillRectColor(
        dc,
        panel,
        PANEL
    );

    RECT hero{
        panel.left,
        panel.top,
        panel.right,
        panel.top + 205
    };

    fillRectColor(
        dc,
        hero,
        CARD
    );

    if (
        selectedPack_ >= 0 &&
        selectedPack_ <
            static_cast<int>(
                packs_.size()
            )
    )
    {
        const Modpack& pack =
            packs_[selectedPack_];

        auto it =
            imageCache_.find(
                L"banner:" +
                pack.bannerUrl
            );

        if (
            it != imageCache_.end() &&
            it->second
        )
        {
            drawBitmapCover(
                dc,
                it->second,
                hero
            );
        }
    }

    RECT overlay{
        hero.left,
        hero.bottom - 82,
        hero.right,
        hero.bottom
    };

    fillRectColor(
        dc,
        overlay,
        BG
    );

    const std::wstring name =
        selectedPack_ >= 0
            ? packs_[selectedPack_].name
            : L"Select a modpack";

    const std::wstring description =
        selectedPack_ >= 0
            ? packs_[selectedPack_].description
            : L"Available packs are loaded from your server.";

    drawTextSimple(
        dc,
        name,
        RECT{
            hero.left + 28,
            hero.bottom - 72,
            hero.right - 28,
            hero.bottom - 35
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    drawTextSimple(
        dc,
        description,
        RECT{
            hero.left + 28,
            hero.bottom - 35,
            hero.right - 28,
            hero.bottom - 8
        },
        fontSmall_,
        MUTED,
        DT_LEFT |
        DT_WORDBREAK |
        DT_END_ELLIPSIS
    );

    const int x0 =
        panel.left + 28;

    const int x1 =
        x0 + 118;

    const int x2 =
        x1 + 165;

    const int x3 =
        x2 + 145;

    const int xr =
        panel.right - 22;

    const int top =
        hero.bottom + 28;

    const std::wstring version =
        currentManifest_.version.empty()
            ? L"—"
            : L"Pack " +
              currentManifest_.version;

    std::wstring loader = L"—";

    if (
        !currentManifest_
            .minecraft
            .loader
            .empty()
    )
    {
        loader =
            currentManifest_
                .minecraft
                .loader;

        if (
            !currentManifest_
                .minecraft
                .loaderVersion
                .empty()
        )
        {
            loader +=
                L" " +
                currentManifest_
                    .minecraft
                    .loaderVersion;
        }

        if (
            !currentManifest_
                .minecraft
                .version
                .empty()
        )
        {
            loader +=
                L"\nMinecraft " +
                currentManifest_
                    .minecraft
                    .version;
        }
    }

    const std::wstring server =
        currentManifest_
            .server
            .address
            .empty()
            ? L"—"
            : currentManifest_
                .server
                .address;

    const std::wstring managed =
        std::to_wstring(
            currentManifest_
                .files
                .size()
        ) +
        L" managed files";

    struct Cell
    {
        const wchar_t* label;
        std::wstring value;
        int left;
        int right;
    };

    Cell cells[] = {
        {
            L"VERSION",
            version,
            x0,
            x1 - 10
        },
        {
            L"LOADER",
            loader,
            x1,
            x2 - 10
        },
        {
            L"SERVER",
            server,
            x2,
            x3 - 10
        },
        {
            L"MANAGED CONTENT",
            managed,
            x3,
            xr
        }
    };

    for (const Cell& cell : cells)
    {
        drawTextSimple(
            dc,
            cell.label,
            RECT{
                cell.left,
                top,
                cell.right,
                top + 22
            },
            fontMeta_,
            MUTED,
            DT_LEFT |
            DT_SINGLELINE
        );

        drawTextSimple(
            dc,
            cell.value,
            RECT{
                cell.left,
                top + 27,
                cell.right,
                top + 83
            },
            fontNormal_,
            TEXT,
            DT_LEFT |
            DT_WORDBREAK |
            DT_END_ELLIPSIS
        );
    }

    if (!errorText_.empty())
    {
        RECT box{
            panel.left + 28,
            top + 94,
            panel.right - 28,
            top + 166
        };

        fillRectColor(
            dc,
            box,
            ERROR_BG
        );

        RECT inside = box;
        inside.left += 12;
        inside.right -= 12;
        inside.top += 10;
        inside.bottom -= 10;

        drawTextSimple(
            dc,
            errorText_,
            inside,
            fontSmall_,
            ERROR_TEXT,
            DT_LEFT |
            DT_WORDBREAK
        );
    }

    const RECT repair =
        repairRect(client);

    const RECT install =
        installRect(client);

    const bool active =
        selectedPack_ >= 0 &&
        !installWorkerRunning_;

    fillRectColor(
        dc,
        repair,
        CARD
    );

    fillRectColor(
        dc,
        install,
        active
            ? ACCENT
            : CARD
    );

    drawTextSimple(
        dc,
        L"Verify & Repair",
        repair,
        fontNormal_,
        active
            ? TEXT
            : MUTED,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    std::wstring installText =
        L"Install";

    if (installWorkerRunning_)
        installText =
            L"Installing...";
    else if (currentPackInstalled())
        installText =
            L"Play";

    drawTextSimple(
        dc,
        installText,
        install,
        fontNormal_,
        active
            ? (
                installText == L"Play" ||
                installText == L"Install"
                    ? TEXT_DARK
                    : MUTED
            )
            : MUTED,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );
}

void MainWindow::ensureArtwork(
    const Modpack& pack)
{
    if (!pack.iconUrl.empty())
    {
        const std::wstring key =
            L"icon:" +
            pack.iconUrl;

        if (!imageCache_.contains(key))
        {
            imageCache_[key] =
                ImageLoader::loadFromUrl(
                    pack.iconUrl,
                    52,
                    52
                );
        }
    }

    if (!pack.bannerUrl.empty())
    {
        const std::wstring key =
            L"banner:" +
            pack.bannerUrl;

        if (!imageCache_.contains(key))
        {
            imageCache_[key] =
                ImageLoader::loadFromUrl(
                    pack.bannerUrl,
                    700,
                    205
                );
        }
    }
}

void MainWindow::refreshPacks()
{
    setStatus(
        L"Connecting to pack server..."
    );

    clearError();

    packs_.clear();
    currentManifest_ = {};
    selectedPack_ = -1;

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );

    UpdateWindow(hwnd_);

    try
    {
        parseIndex(
            HttpClient::getUtf8(
                AppConfig::INDEX_URL
            )
        );

        for (
            const Modpack& pack :
            packs_
        )
        {
            ensureArtwork(pack);
        }

        setStatus(
            L"Online • " +
            std::to_wstring(
                packs_.size()
            ) +
            (
                packs_.size() == 1
                    ? L" pack available"
                    : L" packs available"
            )
        );

        if (!packs_.empty())
        {
            int featured = 0;

            for (
                size_t i = 0;
                i < packs_.size();
                ++i
            )
            {
                if (packs_[i].featured)
                {
                    featured =
                        static_cast<int>(i);
                    break;
                }
            }

            selectPack(featured);
        }
    }
    catch (const std::exception& error)
    {
        setError(
            L"Unable to load the remote pack list.\n\n" +
            utf8ToWide(
                error.what()
            )
        );

        setStatus(
            L"Pack server unavailable"
        );
    }

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );
}

void MainWindow::selectPack(int index)
{
    if (
        index < 0 ||
        index >=
            static_cast<int>(
                packs_.size()
            )
    )
        return;

    selectedPack_ = index;
    currentManifest_ = {};

    clearError();

    ensureArtwork(
        packs_[index]
    );

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );

    UpdateWindow(hwnd_);

    loadManifest(
        packs_[index]
    );
}

void MainWindow::loadManifest(
    const Modpack& pack)
{
    setStatus(
        L"Loading " +
        pack.name +
        L"..."
    );

    try
    {
        currentManifest_ =
            parseManifest(
                HttpClient::getUtf8(
                    pack.manifestUrl
                )
            );

        setStatus(
            L"Connected • manifest loaded"
        );
    }
    catch (const std::exception& error)
    {
        setError(
            L"Could not load " +
            pack.name +
            L".\n\n" +
            utf8ToWide(
                error.what()
            )
        );

        setStatus(
            L"Manifest unavailable"
        );
    }

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );
}

void MainWindow::startInstallOrRepair()
{
    if (
        installWorkerRunning_ ||
        currentManifest_.id.empty()
    )
        return;

    installWorkerRunning_ = true;

    page_ =
        Page::Downloads;

    {
        std::lock_guard<std::mutex> lock(
            progressMutex_
        );

        installProgress_ = {};
        installProgress_.active = true;
        installProgress_.title =
            L"Starting installation...";
        installProgress_.detail =
            currentManifest_.name;
    }

    const PackManifest manifest =
        currentManifest_;

    const std::wstring root =
        settings_.installRoot;

    HWND hwnd =
        hwnd_;

    std::thread(
        [this, manifest, root, hwnd]()
        {
            try
            {
                InstallEngine::installOrRepair(
                    manifest,
                    root,
                    [this, hwnd](
                        const InstallProgress& progress)
                    {
                        {
                            std::lock_guard<std::mutex> lock(
                                progressMutex_
                            );

                            installProgress_ =
                                progress;
                        }

                        PostMessageW(
                            hwnd,
                            WM_INSTALL_PROGRESS,
                            0,
                            0
                        );
                    }
                );
            }
            catch (const std::exception& error)
            {
                std::lock_guard<std::mutex> lock(
                    progressMutex_
                );

                installProgress_.active = false;
                installProgress_.failed = true;
                installProgress_.complete = false;
                installProgress_.title =
                    L"Installation failed";
                installProgress_.detail =
                    utf8ToWide(
                        error.what()
                    );
            }

            PostMessageW(
                hwnd,
                WM_INSTALL_DONE,
                0,
                0
            );
        }
    ).detach();

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );
}

bool MainWindow::currentPackInstalled() const
{
    if (
        currentManifest_.id.empty()
    )
        return false;

    try
    {
        return InstallEngine::isInstalled(
            currentManifest_,
            settings_.installRoot
        );
    }
    catch (...)
    {
        return false;
    }
}

void MainWindow::openInstalledInstance()
{
    if (
        currentManifest_.id.empty()
    )
        return;

    const std::wstring path =
        InstallEngine::packInstanceRoot(
            settings_.installRoot,
            currentManifest_.id
        );

    std::filesystem::create_directories(
        path
    );

    ShellExecuteW(
        hwnd_,
        L"open",
        path.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );
}

void MainWindow::launchOfficialMinecraftLauncher()
{
    // Opens the registered Minecraft Launcher protocol when available.
    // This intentionally does not pretend to authenticate/launch Forge itself.
    HINSTANCE result =
        ShellExecuteW(
            hwnd_,
            L"open",
            L"minecraft://",
            nullptr,
            nullptr,
            SW_SHOWNORMAL
        );

    if (
        reinterpret_cast<INT_PTR>(result) <= 32
    )
    {
        setError(
            L"The pack is installed, but Windows could not open the Minecraft Launcher. "
            L"Install the official Minecraft Launcher or open it manually."
        );
    }
}

void MainWindow::openInstallRoot()
{
    std::filesystem::create_directories(
        settings_.installRoot
    );

    ShellExecuteW(
        hwnd_,
        L"open",
        settings_.installRoot.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );
}

void MainWindow::resetInstallRoot()
{
    settings_.installRoot =
        LauncherSettings::defaultInstallRoot();

    settings_.save();

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );
}

void MainWindow::adjustMemory(
    int deltaMb)
{
    settings_.memoryMb =
        std::clamp(
            settings_.memoryMb + deltaMb,
            2048,
            32768
        );

    settings_.save();

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );
}

void MainWindow::paintTitleBar(
    HDC dc,
    const RECT& fullClient)
{
    const COLORREF TITLE_BG =
        RGB(6, 12, 29);

    const COLORREF TITLE_BORDER =
        RGB(19, 38, 70);

    fillRectColor(
        dc,
        RECT{
            0,
            0,
            fullClient.right,
            TITLEBAR_HEIGHT
        },
        TITLE_BG
    );

    fillRectColor(
        dc,
        RECT{
            0,
            TITLEBAR_HEIGHT - 1,
            fullClient.right,
            TITLEBAR_HEIGHT
        },
        TITLE_BORDER
    );

    // Cyan indicator.
    fillRectColor(
        dc,
        RECT{
            14,
            15,
            20,
            21
        },
        CYAN
    );

    drawTextSimple(
        dc,
        L"NewtTech Launcher",
        RECT{
            29,
            0,
            250,
            TITLEBAR_HEIGHT
        },
        fontSmall_,
        TEXT,
        DT_LEFT |
        DT_VCENTER |
        DT_SINGLELINE
    );

    const RECT minRect =
        titleMinimizeRect(
            fullClient
        );

    const RECT maxRect =
        titleMaximizeRect(
            fullClient
        );

    const RECT closeRect =
        titleCloseRect(
            fullClient
        );

    drawTextSimple(
        dc,
        L"—",
        minRect,
        fontNormal_,
        MUTED,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        IsZoomed(hwnd_) ? L"❐" : L"□",
        maxRect,
        fontNormal_,
        TEXT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    fillRectColor(
        dc,
        closeRect,
        ACCENT
    );

    drawTextSimple(
        dc,
        L"×",
        closeRect,
        fontNormal_,
        TEXT_DARK,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );
}

RECT MainWindow::titleCloseRect(
    const RECT& fullClient) const
{
    return RECT{
        fullClient.right - 46,
        0,
        fullClient.right,
        TITLEBAR_HEIGHT
    };
}

RECT MainWindow::titleMaximizeRect(
    const RECT& fullClient) const
{
    return RECT{
        fullClient.right - 92,
        0,
        fullClient.right - 46,
        TITLEBAR_HEIGHT
    };
}

RECT MainWindow::titleMinimizeRect(
    const RECT& fullClient) const
{
    return RECT{
        fullClient.right - 138,
        0,
        fullClient.right - 92,
        TITLEBAR_HEIGHT
    };
}

LRESULT MainWindow::hitTestNonClient(
    int screenX,
    int screenY) const
{
    POINT point{
        screenX,
        screenY
    };

    ScreenToClient(
        hwnd_,
        &point
    );

    RECT client{};
    GetClientRect(
        hwnd_,
        &client
    );

    constexpr int border = 7;

    const bool left =
        point.x < border;

    const bool right =
        point.x >=
        client.right - border;

    const bool top =
        point.y < border;

    const bool bottom =
        point.y >=
        client.bottom - border;

    if (top && left)
        return HTTOPLEFT;

    if (top && right)
        return HTTOPRIGHT;

    if (bottom && left)
        return HTBOTTOMLEFT;

    if (bottom && right)
        return HTBOTTOMRIGHT;

    if (left)
        return HTLEFT;

    if (right)
        return HTRIGHT;

    if (top)
        return HTTOP;

    if (bottom)
        return HTBOTTOM;

    if (point.y < TITLEBAR_HEIGHT)
    {
        if (
            pointInRect(
                point.x,
                point.y,
                titleCloseRect(client)
            ) ||
            pointInRect(
                point.x,
                point.y,
                titleMaximizeRect(client)
            ) ||
            pointInRect(
                point.x,
                point.y,
                titleMinimizeRect(client)
            )
        )
        {
            return HTCLIENT;
        }

        return HTCAPTION;
    }

    return HTCLIENT;
}

void MainWindow::applyModernWindowStyle()
{
    DWM_WINDOW_CORNER_PREFERENCE preference =
        DWMWCP_DONOTROUND;

    DwmSetWindowAttribute(
        hwnd_,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &preference,
        sizeof(preference)
    );
}

void MainWindow::parseIndex(
    const std::string& json)
{
    JsonValue root =
        JsonLite::parse(json);

    const auto& array =
        root.get("packs")
            .asArray();

    for (
        const JsonValue& value :
        array
    )
    {
        if (!value.isObject())
            continue;

        Modpack pack;

        pack.id =
            utf8ToWide(
                value.get("id")
                    .asString()
            );

        pack.name =
            utf8ToWide(
                value.get("name")
                    .asString()
            );

        pack.description =
            utf8ToWide(
                value.get("description")
                    .asString()
            );

        pack.iconUrl =
            utf8ToWide(
                value.get("icon")
                    .asString()
            );

        pack.bannerUrl =
            utf8ToWide(
                value.get("banner")
                    .asString()
            );

        pack.manifestUrl =
            utf8ToWide(
                value.get("manifest")
                    .asString()
            );

        pack.enabled =
            value.get("enabled")
                .asBool(true);

        pack.featured =
            value.get("featured")
                .asBool(false);

        pack.accent =
            utf8ToWide(
                value.get("accent")
                    .asString("#ff23b4")
            );

        if (pack.enabled)
            packs_.push_back(
                std::move(pack)
            );
    }
}

PackManifest MainWindow::parseManifest(
    const std::string& json)
{
    JsonValue root =
        JsonLite::parse(json);

    PackManifest manifest;

    manifest.id =
        utf8ToWide(
            root.get("id")
                .asString()
        );

    manifest.name =
        utf8ToWide(
            root.get("name")
                .asString()
        );

    manifest.version =
        utf8ToWide(
            root.get("version")
                .asString()
        );

    const JsonValue& minecraft =
        root.get("minecraft");

    manifest.minecraft.version =
        utf8ToWide(
            minecraft.get("version")
                .asString()
        );

    manifest.minecraft.loader =
        utf8ToWide(
            minecraft.get("loader")
                .asString()
        );

    manifest.minecraft.loaderVersion =
        utf8ToWide(
            minecraft.get("loaderVersion")
                .asString()
        );

    const JsonValue& java =
        root.get("java");

    manifest.java.minimumVersion =
        static_cast<int>(
            java.get("minimumVersion")
                .asNumber(17)
        );

    manifest.java.recommendedMemory =
        static_cast<int>(
            java.get("recommendedMemory")
                .asNumber(8192)
        );

    manifest.server.address =
        utf8ToWide(
            root.get("server")
                .get("address")
                .asString()
        );

    const JsonValue& files =
        root.get("files");

    if (files.isArray())
    {
        for (
            const JsonValue& value :
            files.asArray()
        )
        {
            PackFile file;

            file.path =
                utf8ToWide(
                    value.get("path")
                        .asString()
                );

            file.url =
                utf8ToWide(
                    value.get("url")
                        .asString()
                );

            file.size =
                static_cast<long long>(
                    value.get("size")
                        .asNumber(0)
                );

            file.sha256 =
                utf8ToWide(
                    value.get("sha256")
                        .asString()
                );

            file.policy =
                utf8ToWide(
                    value.get("policy")
                        .asString("replace")
                );

            manifest.files.push_back(
                std::move(file)
            );
        }
    }

    return manifest;
}

std::wstring MainWindow::utf8ToWide(
    const std::string& value)
{
    if (value.empty())
        return {};

    const int count =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(
                value.size()
            ),
            nullptr,
            0
        );

    std::wstring result(
        count,
        L'\0'
    );

    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(
            value.size()
        ),
        result.data(),
        count
    );

    return result;
}

void MainWindow::setStatus(
    const std::wstring& text)
{
    statusText_ = text;

    if (hwnd_)
        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );
}

void MainWindow::setError(
    const std::wstring& text)
{
    errorText_ = text;

    if (hwnd_)
        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );
}

void MainWindow::clearError()
{
    errorText_.clear();
}

int MainWindow::hitTestSidebar(
    int x,
    int y) const
{
    if (
        x < 7 ||
        x > 195
    )
        return -1;

    for (
        int i = 0;
        i < 4;
        ++i
    )
    {
        RECT row{
            7,
            105 + i * 52,
            195,
            149 + i * 52
        };

        if (
            pointInRect(
                x,
                y,
                row
            )
        )
            return i;
    }

    return -1;
}

int MainWindow::hitTestPackList(
    int x,
    int y) const
{
    if (
        x < 251 ||
        x > 546 ||
        y < 155
    )
        return -1;

    const int relative =
        y - 155;

    const int index =
        relative / 84;

    if (relative % 84 > 76)
        return -1;

    if (
        index < 0 ||
        index >=
            static_cast<int>(
                packs_.size()
            )
    )
        return -1;

    return index;
}

bool MainWindow::pointInRect(
    int x,
    int y,
    const RECT& rect) const
{
    POINT point{
        x,
        y
    };

    return PtInRect(
        &rect,
        point
    ) != 0;
}

RECT MainWindow::refreshRect(
    const RECT& client) const
{
    return RECT{
        client.right - 145,
        27,
        client.right - 30,
        69
    };
}

RECT MainWindow::repairRect(
    const RECT& client) const
{
    return RECT{
        client.right - 348,
        client.bottom - 88,
        client.right - 200,
        client.bottom - 44
    };
}

RECT MainWindow::installRect(
    const RECT& client) const
{
    return RECT{
        client.right - 188,
        client.bottom - 88,
        client.right - 58,
        client.bottom - 44
    };
}

RECT MainWindow::openFolderRect(
    const RECT& client) const
{
    return RECT{
        client.right - 340,
        225,
        client.right - 205,
        267
    };
}

RECT MainWindow::resetFolderRect(
    const RECT& client) const
{
    return RECT{
        client.right - 193,
        225,
        client.right - 58,
        267
    };
}

RECT MainWindow::memoryMinusRect(
    const RECT& client) const
{
    return RECT{
        client.right - 340,
        360,
        client.right - 225,
        404
    };
}

RECT MainWindow::memoryPlusRect(
    const RECT& client) const
{
    return RECT{
        client.right - 210,
        360,
        client.right - 95,
        404
    };
}
