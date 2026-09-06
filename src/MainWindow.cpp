#include "MainWindow.h"
#include "ServersDat.h"

#include "AppConfig.h"
#include "HttpClient.h"
#include "ImageLoader.h"
#include "JsonLite.h"
#include "VersionManager.h"
#include "MinecraftProfile.h"
#include "NewsManager.h"

#include <objbase.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>


namespace
{
constexpr UINT WM_ACCOUNT_AVATAR_READY = WM_APP + 43;

struct AccountAvatarReady
{
    std::wstring username;
    std::wstring uuid;
    HBITMAP bitmap = nullptr;
};

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

std::wstring formatNewsDate(
    const std::wstring& iso)
{
    if (iso.length() < 16)
        return iso;

    try
    {
        const int year = std::stoi(iso.substr(0, 4));
        const int month = std::stoi(iso.substr(5, 2));
        const int day = std::stoi(iso.substr(8, 2));
        int hour = std::stoi(iso.substr(11, 2));
        const int minute = std::stoi(iso.substr(14, 2));

        const bool pm = hour >= 12;
        int displayHour = hour % 12;

        if (displayHour == 0)
            displayHour = 12;

        wchar_t buffer[64]{};

        swprintf(
            buffer,
            64,
            L"%02d:%02d %ls %02d/%02d/%04d",
            displayHour,
            minute,
            pm ? L"PM" : L"AM",
            month,
            day,
            year
        );

        return buffer;
    }
    catch (...)
    {
        return iso;
    }
}

}

constexpr wchar_t LAUNCHER_VERSION[] = L"0.9.0";

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
    wc.style =
        CS_HREDRAW |
        CS_VREDRAW;
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
    createLoginControls();

    // Restore the cached account BEFORE exposing the login wall.
    // This makes CLion/debug and installed builds follow the same path because
    // AuthManager stores the session under %LOCALAPPDATA%, not beside the EXE.
    authenticated_ = false;
    authUser_ = AuthUser{};
    authError_.clear();

    if (AuthManager::hasToken())
    {
        authenticated_ =
            AuthManager::restore(
                authUser_
            );
    }

    showLoginControls(
        !authenticated_
    );

    ShowWindow(
        hwnd_,
        showCommand
    );

    UpdateWindow(hwnd_);

    if (authenticated_)
    {
        completeAuthenticatedStartup();
    }
    else
    {
        SetFocus(loginUsername_);
    }

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
        // Enter from either edit field submits the form.  Doing this in
        // the message loop catches the key while focus belongs to the child
        // EDIT control (the parent does not receive that WM_KEYDOWN).
        if (
            !authenticated_ &&
            msg.message == WM_KEYDOWN &&
            msg.wParam == VK_RETURN &&
            (
                msg.hwnd == loginUsername_ ||
                msg.hwnd == loginPassword_ ||
                msg.hwnd == loginButton_
            )
        )
        {
            if (!authBusy_)
                attemptLogin();

            continue;
        }

        // Give the unauthenticated login wall normal Windows form keyboard
        // navigation.  WS_TABSTOP already defines the desired order:
        // Username -> Password -> Sign In -> Register now.
        if (
            !authenticated_ &&
            IsDialogMessageW(
                hwnd_,
                &msg
            )
        )
        {
            continue;
        }

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

        case WM_KEYDOWN:
        {
            if (
                wParam == VK_ESCAPE &&
                mediaOpenInstance_ >= 0
            )
            {
                mediaOpenInstance_ = -1;
                mediaOpenScreenshot_ = -1;

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (
                wParam == VK_ESCAPE &&
                lowMemoryWarningOpen_
            )
            {
                lowMemoryWarningOpen_ = false;

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (
                wParam == VK_ESCAPE &&
                openNewsIndex_ >= 0
            )
            {
                openNewsIndex_ = -1;
                newsModalScrollY_ = 0;
                newsModalMaxScroll_ = 0;
                newsModalLinks_.clear();

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            break;
        }

        case WM_CAPTURECHANGED:
        {
            homeScrollbarDragging_ =
                false;

            newsModalScrollbarDragging_ =
                false;

            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            if (
                authenticated_ &&
                page_ == Page::Settings
            )
            {
                RECT client{};
                GetClientRect(
                    hwnd_,
                    &client
                );

                const int delta =
                    GET_WHEEL_DELTA_WPARAM(
                        wParam
                    );

                settingsScrollY_ -=
                    (delta / WHEEL_DELTA) *
                    72;

                clampSettingsScroll(
                    client
                );

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (
                page_ == Page::Media &&
                mediaOpenInstance_ < 0
            )
            {
                RECT client{};
                GetClientRect(
                    hwnd_,
                    &client
                );
                client.bottom -= TITLEBAR_HEIGHT;

                const int notches =
                    GET_WHEEL_DELTA_WPARAM(wParam) /
                    WHEEL_DELTA;

                setMediaScroll(
                    mediaScrollY_ -
                    notches * 90,
                    client
                );

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (
                page_ == Page::Home &&
                openNewsIndex_ >= 0
            )
            {
                const int delta =
                    GET_WHEEL_DELTA_WPARAM(
                        wParam
                    );

                const int notches =
                    delta / WHEEL_DELTA;

                setNewsModalScroll(
                    newsModalScrollY_ -
                    notches * 72
                );

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (page_ == Page::Home)
            {
                RECT client{};
                GetClientRect(
                    hwnd_,
                    &client
                );

                client.bottom -=
                    TITLEBAR_HEIGHT;

                const int delta =
                    GET_WHEEL_DELTA_WPARAM(
                        wParam
                    );

                const int notches =
                    delta / WHEEL_DELTA;

                setHomeScroll(
                    homeScrollY_ -
                    notches * 72,
                    client
                );

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            break;
        }

        case WM_LBUTTONDOWN:
        {
            if (
                page_ == Page::Home &&
                openNewsIndex_ >= 0
            )
            {
                const int x =
                    GET_X_LPARAM(lParam);

                const int contentY =
                    GET_Y_LPARAM(lParam) -
                    TITLEBAR_HEIGHT;

                RECT client{};
                GetClientRect(
                    hwnd_,
                    &client
                );

                client.bottom -=
                    TITLEBAR_HEIGHT;

                const RECT track =
                    newsModalScrollbarTrackRect(
                        client
                    );

                const RECT thumb =
                    newsModalScrollbarThumbRect(
                        client
                    );

                if (
                    newsModalMaxScroll_ > 0 &&
                    pointInRect(
                        x,
                        contentY,
                        thumb
                    )
                )
                {
                    newsModalScrollbarDragging_ =
                        true;

                    newsModalScrollbarDragOffset_ =
                        contentY -
                        thumb.top;

                    SetCapture(hwnd_);
                    return 0;
                }

                if (
                    newsModalMaxScroll_ > 0 &&
                    pointInRect(
                        x,
                        contentY,
                        track
                    )
                )
                {
                    const RECT modal =
                        newsModalRect(client);

                    const int pageAmount =
                        std::max(
                            120,
                            static_cast<int>(
                                modal.bottom -
                                modal.top -
                                100
                            )
                        );

                    if (contentY < thumb.top)
                    {
                        setNewsModalScroll(
                            newsModalScrollY_ -
                            pageAmount
                        );
                    }
                    else if (
                        contentY >
                        thumb.bottom
                    )
                    {
                        setNewsModalScroll(
                            newsModalScrollY_ +
                            pageAmount
                        );
                    }

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }

                return 0;
            }

            if (page_ == Page::Home)
            {
                const int x =
                    GET_X_LPARAM(lParam);

                const int contentY =
                    GET_Y_LPARAM(lParam) -
                    TITLEBAR_HEIGHT;

                RECT client{};
                GetClientRect(
                    hwnd_,
                    &client
                );

                client.bottom -=
                    TITLEBAR_HEIGHT;

                const RECT track =
                    homeScrollbarTrackRect(
                        client
                    );

                const RECT thumb =
                    homeScrollbarThumbRect(
                        client
                    );

                if (
                    homeMaxScroll(client) > 0 &&
                    pointInRect(
                        x,
                        contentY,
                        thumb
                    )
                )
                {
                    homeScrollbarDragging_ =
                        true;

                    homeScrollbarDragOffset_ =
                        contentY -
                        thumb.top;

                    SetCapture(
                        hwnd_
                    );

                    return 0;
                }

                if (
                    homeMaxScroll(client) > 0 &&
                    pointInRect(
                        x,
                        contentY,
                        track
                    )
                )
                {
                    const int pageAmount =
                        static_cast<int>(
                            std::max<LONG>(
                                120L,
                                client.bottom - 100
                            )
                        );

                    if (contentY < thumb.top)
                    {
                        setHomeScroll(
                            homeScrollY_ -
                            pageAmount,
                            client
                        );
                    }
                    else if (
                        contentY >
                        thumb.bottom
                    )
                    {
                        setHomeScroll(
                            homeScrollY_ +
                            pageAmount,
                            client
                        );
                    }

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }
            }

            break;
        }

        case WM_MOUSEMOVE:
        {
            if (
                settingsScrollDragging_ &&
                page_ == Page::Settings
            )
            {
                RECT client{};
                GetClientRect(
                    hwnd_,
                    &client
                );

                RECT contentClient{
                    217,
                    30,
                    client.right,
                    client.bottom
                };

                RECT track =
                    settingsScrollbarTrackRect(
                        contentClient
                    );

                RECT thumb =
                    settingsScrollbarThumbRect(
                        contentClient
                    );

                const int thumbHeight =
                    thumb.bottom -
                    thumb.top;

                const int travel =
                    std::max(
                        1,
                        static_cast<int>(
                            track.bottom -
                            track.top
                        ) -
                        thumbHeight
                    );

                const int mouseY =
                    GET_Y_LPARAM(lParam) -
                    30;

                const int desired =
                    std::clamp<int>(
                        mouseY -
                        static_cast<int>(track.top) -
                        settingsScrollDragOffset_,
                        0,
                        travel
                    );

                settingsScrollY_ =
                    static_cast<int>(
                        (static_cast<long long>(
                            desired
                        ) *
                         settingsMaxScroll(
                             contentClient
                         )) /
                        travel
                    );

                clampSettingsScroll(
                    contentClient
                );

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }


            if (
                page_ == Page::Home &&
                openNewsIndex_ >= 0 &&
                newsModalScrollbarDragging_
            )
            {
                RECT client{};
                GetClientRect(
                    hwnd_,
                    &client
                );

                client.bottom -=
                    TITLEBAR_HEIGHT;

                const RECT track =
                    newsModalScrollbarTrackRect(
                        client
                    );

                const RECT thumb =
                    newsModalScrollbarThumbRect(
                        client
                    );

                const int thumbHeight =
                    static_cast<int>(
                        thumb.bottom -
                        thumb.top
                    );

                const int usableTrack =
                    static_cast<int>(
                        std::max<LONG>(
                            1L,
                            (
                                track.bottom -
                                track.top
                            ) -
                            thumbHeight
                        )
                    );

                const int y =
                    GET_Y_LPARAM(lParam) -
                    TITLEBAR_HEIGHT;

                int thumbTop =
                    y -
                    newsModalScrollbarDragOffset_;

                thumbTop =
                    std::clamp(
                        thumbTop,
                        static_cast<int>(
                            track.top
                        ),
                        static_cast<int>(
                            track.bottom -
                            thumbHeight
                        )
                    );

                const int newScroll =
                    newsModalMaxScroll_ > 0
                        ? (
                            (
                                thumbTop -
                                track.top
                            ) *
                            newsModalMaxScroll_
                          ) /
                          usableTrack
                        : 0;

                setNewsModalScroll(
                    newScroll
                );

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (
                page_ == Page::Home &&
                homeScrollbarDragging_
            )
            {
                RECT client{};
                GetClientRect(
                    hwnd_,
                    &client
                );

                client.bottom -=
                    TITLEBAR_HEIGHT;

                const RECT track =
                    homeScrollbarTrackRect(
                        client
                    );

                const RECT thumb =
                    homeScrollbarThumbRect(
                        client
                    );

                const int thumbHeight =
                    thumb.bottom -
                    thumb.top;

                const int usableTrack =
                    static_cast<int>(
                        std::max<LONG>(
                            1L,
                            (track.bottom -
                             track.top) -
                            thumbHeight
                        )
                    );

                const int y =
                    GET_Y_LPARAM(lParam) -
                    TITLEBAR_HEIGHT;

                int thumbTop =
                    y -
                    homeScrollbarDragOffset_;

                thumbTop =
                    std::clamp(
                        thumbTop,
                        static_cast<int>(
                            track.top
                        ),
                        static_cast<int>(
                            track.bottom -
                            thumbHeight
                        )
                    );

                const int maxScroll =
                    homeMaxScroll(
                        client
                    );

                const int newScroll =
                    maxScroll > 0
                        ? (
                            (thumbTop -
                             track.top) *
                            maxScroll
                          ) /
                          usableTrack
                        : 0;

                setHomeScroll(
                    newScroll,
                    client
                );

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            break;
        }

        case WM_LBUTTONUP:
        {
            if (settingsScrollDragging_)
            {
                settingsScrollDragging_ = false;
                ReleaseCapture();
                return 0;
            }


            if (newsModalScrollbarDragging_)
            {
                newsModalScrollbarDragging_ =
                    false;

                if (
                    GetCapture() ==
                    hwnd_
                )
                {
                    ReleaseCapture();
                }

                return 0;
            }

            if (homeScrollbarDragging_)
            {
                homeScrollbarDragging_ =
                    false;

                if (
                    GetCapture() ==
                    hwnd_
                )
                {
                    ReleaseCapture();
                }

                return 0;
            }

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

            /*
                The low-memory warning is application-modal.  Handle it before
                sidebar navigation so clicks cannot leak through to the page
                behind the warning.
            */
            if (lowMemoryWarningOpen_)
            {
                RECT modalClient{};
                GetClientRect(
                    hwnd_,
                    &modalClient
                );

                modalClient.bottom -=
                    TITLEBAR_HEIGHT;

                if (
                    pointInRect(
                        x,
                        contentY,
                        lowMemoryProceedRect(
                            modalClient
                        )
                    )
                )
                {
                    lowMemoryWarningOpen_ = false;

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );

                    // Explicit user override applies to this launch only.
                    launchCurrentPack();
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        contentY,
                        lowMemoryChangeRect(
                            modalClient
                        )
                    )
                )
                {
                    lowMemoryWarningOpen_ = false;
                    page_ = Page::Settings;

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }

                // Ignore all other clicks while the warning is open.
                return 0;
            }

            if (
                pointInRect(
                    x,
                    contentY,
                    sidebarAccountRect()
                )
            )
            {
                page_ = Page::Settings;
                InvalidateRect(hwnd_,nullptr,FALSE);
                return 0;
            }

            const int nav =
                hitTestSidebar(
                    x,
                    contentY
                );

            if (nav >= 0)
            {
                const Page nextPage =
                    static_cast<Page>(nav);

                if (
                    nextPage == Page::Media &&
                    page_ != Page::Media
                )
                {
                    refreshMedia();
                }

                page_ = nextPage;

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

            RECT contentClient = client;
            contentClient.bottom -= TITLEBAR_HEIGHT;

            if (
                page_ == Page::Home &&
                openNewsIndex_ >= 0
            )
            {
                for (
                    const NewsMarkdownLink& link :
                    newsModalLinks_
                )
                {
                    if (
                        pointInRect(
                            x,
                            contentY,
                            link.rect
                        )
                    )
                    {
                        ShellExecuteW(
                            hwnd_,
                            L"open",
                            link.url.c_str(),
                            nullptr,
                            nullptr,
                            SW_SHOWNORMAL
                        );

                        return 0;
                    }
                }

                if (
                    pointInRect(
                        x,
                        contentY,
                        newsModalCloseRect(
                            contentClient
                        )
                    )
                )
                {
                    openNewsIndex_ = -1;
                    newsModalScrollY_ = 0;
                    newsModalMaxScroll_ = 0;
                    newsModalLinks_.clear();

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );
                }

                // Modal is exclusive: prevent clicks from reaching Home/nav.
                return 0;
            }

            if (
                page_ == Page::Modpacks &&
                pointInRect(x, contentY, refreshRect(contentClient))
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
                        repairRect(contentClient)
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
                        installRect(contentClient)
                    )
                )
                {
                    if (currentPackInstalled())
                    {
                        if (shouldWarnAboutMemory())
                        {
                            lowMemoryWarningOpen_ = true;

                            InvalidateRect(
                                hwnd_,
                                nullptr,
                                FALSE
                            );

                            return 0;
                        }

                        launchCurrentPack();
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
                const int virtualY =
                    contentY +
                    homeScrollY_;

                const int featured =
                    homeFeaturedPack();

                // Featured pack: Play Now
                if (
                    featured >= 0 &&
                    pointInRect(
                        x,
                        virtualY,
                        homeHeroPlayRect(
                            contentClient
                        )
                    )
                )
                {
                    // Make sure the manifest belongs to the pack shown
                    // in the Home hero before launching it.
                    if (selectedPack_ != featured)
                        selectPack(featured);

                    if (currentPackInstalled())
                    {
                        if (shouldWarnAboutMemory())
                        {
                            lowMemoryWarningOpen_ = true;
                            InvalidateRect(hwnd_,nullptr,FALSE);
                            return 0;
                        }

                        launchCurrentPack();
                    }
                    else
                    {
                        startInstallOrRepair();
                    }

                    return 0;
                }

                // Featured pack: open the full Modpacks view.
                if (
                    featured >= 0 &&
                    pointInRect(
                        x,
                        virtualY,
                        homeHeroViewRect(
                            contentClient
                        )
                    )
                )
                {
                    if (selectedPack_ != featured)
                        selectPack(featured);

                    page_ = Page::Modpacks;
                    InvalidateRect(hwnd_,nullptr,FALSE);
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        virtualY,
                        homeViewAllPacksRect(
                            contentClient
                        )
                    )
                )
                {
                    page_ = Page::Modpacks;
                    InvalidateRect(hwnd_,nullptr,FALSE);
                    return 0;
                }

                const std::vector<int> installed =
                    homeInstalledPacks();

                const int maxOffset =
                    std::max(
                        0,
                        static_cast<int>(
                            installed.size()
                        ) - 3
                    );

                if (
                    homeCarouselOffset_ > 0 &&
                    pointInRect(
                        x,
                        virtualY,
                        homeCarouselPrevRect(
                            contentClient
                        )
                    )
                )
                {
                    --homeCarouselOffset_;
                    InvalidateRect(hwnd_,nullptr,FALSE);
                    return 0;
                }

                if (
                    homeCarouselOffset_ < maxOffset &&
                    pointInRect(
                        x,
                        virtualY,
                        homeCarouselNextRect(
                            contentClient
                        )
                    )
                )
                {
                    ++homeCarouselOffset_;
                    InvalidateRect(hwnd_,nullptr,FALSE);
                    return 0;
                }

                // Clicking an installed-pack card makes it the Home hero.
                for (int slot = 0; slot < 3; ++slot)
                {
                    const int position =
                        homeCarouselOffset_ + slot;

                    if (
                        position >=
                        static_cast<int>(
                            installed.size()
                        )
                    )
                        break;

                    if (
                        pointInRect(
                            x,
                            virtualY,
                            homeCarouselCardRect(
                                contentClient,
                                slot
                            )
                        )
                    )
                    {
                        selectPack(
                            installed[position]
                        );

                        InvalidateRect(hwnd_,nullptr,FALSE);
                        return 0;
                    }
                }

                if (
                    pointInRect(
                        x,
                        virtualY,
                        newsRefreshRect(
                            contentClient
                        )
                    )
                )
                {
                    refreshNews();
                    return 0;
                }

                const int count =
                    static_cast<int>(
                        std::min<size_t>(
                            news_.size(),
                            3
                        )
                    );

                for (int i = 0; i < count; ++i)
                {
                    if (
                        pointInRect(
                            x,
                            virtualY,
                            newsCardRect(
                                contentClient,
                                i
                            )
                        )
                    )
                    {
                        openNewsIndex_ = i;
                        newsModalScrollY_ = 0;
                        newsModalMaxScroll_ = 0;
                        newsModalLinks_.clear();

                        InvalidateRect(
                            hwnd_,
                            nullptr,
                            FALSE
                        );

                        return 0;
                    }
                }
            }

            if (page_ == Page::Media)
            {
                if (
                    mediaOpenInstance_ >= 0 &&
                    mediaOpenScreenshot_ >= 0
                )
                {
                    if (
                        pointInRect(
                            x,
                            contentY,
                            mediaModalCloseRect(
                                contentClient
                            )
                        )
                    )
                    {
                        mediaOpenInstance_ = -1;
                        mediaOpenScreenshot_ = -1;

                        InvalidateRect(
                            hwnd_,
                            nullptr,
                            FALSE
                        );
                    }

                    // Screenshot viewer is modal.
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        contentY,
                        mediaRefreshRect(
                            contentClient
                        )
                    )
                )
                {
                    refreshMedia();

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }

                const int virtualY =
                    contentY +
                    mediaScrollY_;

                for (
                    int instanceIndex = 0;
                    instanceIndex <
                        static_cast<int>(
                            mediaInstances_.size()
                        );
                    ++instanceIndex
                )
                {
                    MediaInstance& instance =
                        mediaInstances_[instanceIndex];

                    if (
                        pointInRect(
                            x,
                            virtualY,
                            instance.headerRect
                        )
                    )
                    {
                        instance.expanded =
                            !instance.expanded;

                        InvalidateRect(
                            hwnd_,
                            nullptr,
                            FALSE
                        );

                        return 0;
                    }

                    if (!instance.expanded)
                        continue;

                    for (
                        int shotIndex = 0;
                        shotIndex <
                            static_cast<int>(
                                instance.screenshots.size()
                            );
                        ++shotIndex
                    )
                    {
                        if (
                            pointInRect(
                                x,
                                virtualY,
                                instance
                                    .screenshots[shotIndex]
                                    .cardRect
                            )
                        )
                        {
                            mediaOpenInstance_ =
                                instanceIndex;

                            mediaOpenScreenshot_ =
                                shotIndex;

                            InvalidateRect(
                                hwnd_,
                                nullptr,
                                FALSE
                            );

                            return 0;
                        }
                    }
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
                const RECT settingsTrack =
                    settingsScrollbarTrackRect(
                        contentClient
                    );

                const RECT settingsThumb =
                    settingsScrollbarThumbRect(
                        contentClient
                    );

                if (
                    settingsMaxScroll(
                        contentClient
                    ) > 0 &&
                    pointInRect(
                        x,
                        contentY,
                        settingsThumb
                    )
                )
                {
                    settingsScrollDragging_ = true;
                    settingsScrollDragOffset_ =
                        contentY -
                        settingsThumb.top;
                    SetCapture(hwnd_);
                    return 0;
                }

                if (
                    settingsMaxScroll(
                        contentClient
                    ) > 0 &&
                    pointInRect(
                        x,
                        contentY,
                        settingsTrack
                    )
                )
                {
                    const int trackHeight =
                        settingsTrack.bottom -
                        settingsTrack.top;

                    const int thumbHeight =
                        settingsThumb.bottom -
                        settingsThumb.top;

                    const int travel =
                        std::max(
                            1,
                            trackHeight -
                            thumbHeight
                        );

                    const int desired =
                        std::clamp<int>(
                            static_cast<int>(contentY) -
                            static_cast<int>(settingsTrack.top) -
                            thumbHeight / 2,
                            0,
                            travel
                        );

                    settingsScrollY_ =
                        static_cast<int>(
                            (static_cast<long long>(
                                desired
                            ) *
                             settingsMaxScroll(
                                 contentClient
                             )) /
                            travel
                        );

                    clampSettingsScroll(
                        contentClient
                    );

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }

                const int settingsY = static_cast<int>(contentY) + settingsScrollY_;
                if (
                    pointInRect(
                        x,
                        settingsY,
                        openFolderRect(contentClient)
                    )
                )
                {
                    openInstallRoot();
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        settingsY,
                        resetFolderRect(contentClient)
                    )
                )
                {
                    resetInstallRoot();
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        settingsY,
                        memoryMinusRect(contentClient)
                    )
                )
                {
                    adjustMemory(-1024);
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        settingsY,
                        memoryPlusRect(contentClient)
                    )
                )
                {
                    adjustMemory(1024);
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        settingsY,
                        updateCheckRect(contentClient)
                    )
                )
                {
                    checkForUpdates(true);
                    return 0;
                }

                if (
                    updateAvailable_ &&
                    pointInRect(
                        x,
                        settingsY,
                        updateNowRect(contentClient)
                    )
                )
                {
                    launchUpdater();
                    return 0;
                }

                if (
                    pointInRect(
                        x,
                        settingsY,
                        accountLogoutRect(contentClient)
                    )
                )
                {
                    // Sign out is explicit: invalidate the server session
                    // where possible, then always erase the local DPAPI cache.
                    AuthManager::logout();

                    authenticated_ = false;
                    authUser_ = AuthUser{};
                    authError_.clear();
                    authBusy_ = false;
                    settingsScrollY_ = 0;
                    page_ = Page::Home;

                    SetWindowTextW(loginUsername_, L"");
                    SetWindowTextW(loginPassword_, L"");

                    KillTimer(
                        hwnd_,
                        AUTH_HEARTBEAT_TIMER
                    );

                    showLoginControls(true);

                    InvalidateRect(
                        hwnd_,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }
            }

            return 0;
        }

        case WM_APP + 20:
        {
            EnableWindow(loginButton_,TRUE);
            if (wParam == 1)
                completeAuthenticatedStartup();
            else
                InvalidateRect(hwnd_,nullptr,FALSE);
            return 0;
        }

        case WM_CTLCOLOREDIT:
        {
            HDC editDc = reinterpret_cast<HDC>(wParam);
            HWND edit = reinterpret_cast<HWND>(lParam);

            if (
                edit == loginUsername_ ||
                edit == loginPassword_
            )
            {
                SetTextColor(editDc, TEXT);
                SetBkColor(editDc, CARD);
                static HBRUSH loginEditBrush =
                    CreateSolidBrush(CARD);
                return reinterpret_cast<LRESULT>(
                    loginEditBrush
                );
            }

            break;
        }

        case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* item =
                reinterpret_cast<DRAWITEMSTRUCT*>(
                    lParam
                );

            if (
                item &&
                (
                    item->CtlID == ID_LOGIN_BUTTON ||
                    item->CtlID == ID_REGISTER_BUTTON
                )
            )
            {
                const bool signIn =
                    item->CtlID == ID_LOGIN_BUTTON;

                const bool pressed =
                    (item->itemState & ODS_SELECTED) != 0;

                RECT r = item->rcItem;

                const COLORREF background =
                    signIn
                        ? (pressed ? RGB(220,20,155) : ACCENT)
                        : CARD;

                fillRectColor(
                    item->hDC,
                    r,
                    background
                );

                if (!signIn)
                {
                    HPEN pen = CreatePen(
                        PS_SOLID,
                        1,
                        RGB(42,83,120)
                    );
                    HGDIOBJ oldPen =
                        SelectObject(
                            item->hDC,
                            pen
                        );
                    HGDIOBJ oldBrush =
                        SelectObject(
                            item->hDC,
                            GetStockObject(
                                NULL_BRUSH
                            )
                        );
                    Rectangle(
                        item->hDC,
                        r.left,
                        r.top,
                        r.right,
                        r.bottom
                    );
                    SelectObject(
                        item->hDC,
                        oldBrush
                    );
                    SelectObject(
                        item->hDC,
                        oldPen
                    );
                    DeleteObject(pen);
                }

                const wchar_t* text =
                    signIn
                        ? L"Sign In"
                        : L"Register now";

                drawTextSimple(
                    item->hDC,
                    text,
                    r,
                    fontNormal_,
                    signIn ? RGB(5,12,24) : CYAN,
                    DT_CENTER |
                    DT_VCENTER |
                    DT_SINGLELINE
                );

                if (
                    item->itemState &
                    ODS_FOCUS
                )
                {
                    RECT focus = r;
                    InflateRect(
                        &focus,
                        -4,
                        -4
                    );
                    DrawFocusRect(
                        item->hDC,
                        &focus
                    );
                }

                return TRUE;
            }

            break;
        }

        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);

            if (!authenticated_)
            {
                if (id == ID_LOGIN_BUTTON)
                {
                    attemptLogin();
                    return 0;
                }

                if (id == ID_REGISTER_BUTTON)
                {
                    ShellExecuteW(
                        hwnd_,
                        L"open",
                        L"https://launcher.newttech.net/account/user/",
                        nullptr,
                        nullptr,
                        SW_SHOWNORMAL
                    );
                    return 0;
                }
            }

            break;
        }

        case WM_ACCOUNT_AVATAR_READY:
        {
            auto* result =
                reinterpret_cast<AccountAvatarReady*>(
                    lParam
                );

            if (!result)
                return 0;

            const std::wstring currentUser =
                authUser_.minecraftUsername;

            if (
                authenticated_ &&
                currentUser == result->username
            )
            {
                const std::wstring key =
                    L"account-head:" +
                    result->username;

                auto existing =
                    imageCache_.find(key);

                if (
                    existing != imageCache_.end() &&
                    existing->second
                )
                {
                    DeleteObject(existing->second);
                }

                imageCache_[key] =
                    result->bitmap;

                result->bitmap = nullptr;

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );
            }

            if (result->bitmap)
                DeleteObject(result->bitmap);

            delete result;
            return 0;
        }

        case WM_TIMER:
        {
            if (
                wParam == AUTH_HEARTBEAT_TIMER &&
                authenticated_
            )
            {
                std::thread([](){ AuthManager::heartbeat(); }).detach();
                return 0;
            }

            if (
                wParam == UPDATE_PULSE_TIMER &&
                updateAvailable_
            )
            {
                updatePulseOn_ =
                    !updatePulseOn_;

                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            break;
        }

        case WM_UPDATE_CHECK_DONE:
        {
            updateCheckRunning_ = false;

            if (updateAvailable_)
            {
                SetTimer(
                    hwnd_,
                    UPDATE_PULSE_TIMER,
                    650,
                    nullptr
                );
            }
            else
            {
                KillTimer(
                    hwnd_,
                    UPDATE_PULSE_TIMER
                );

                updatePulseOn_ = false;
            }

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

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

        case WM_ENTERSIZEMOVE:
        {
            /*
                Interactive resize is about to begin. Do not let Windows
                preserve/stretch the old client pixels while the pointer moves.
            */
            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

            return 0;
        }

        case WM_SIZING:
        {
            /*
                WM_SIZE alone is not enough for a custom-drawn popup/frame:
                Windows may wait until the sizing operation ends before
                dispatching a useful WM_PAINT. Paint synchronously here so the
                launcher layout follows the mouse continuously.
            */
            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

            UpdateWindow(hwnd_);

            return TRUE;
        }

        case WM_SIZE:
        {
            if (!authenticated_ && loginUsername_)
                showLoginControls(true);

            if (wParam != SIZE_MINIMIZED)
            {
                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                UpdateWindow(hwnd_);
            }

            return 0;
        }

        case WM_WINDOWPOSCHANGED:
        {
            /*
                Covers programmatic/maximize/restore sizing as well as normal
                interactive resizing. Let DefWindowProc perform its normal
                bookkeeping, then repaint using the new client dimensions.
            */
            const LRESULT result =
                DefWindowProcW(
                    hwnd,
                    message,
                    wParam,
                    lParam
                );

            if (!IsIconic(hwnd_))
            {
                InvalidateRect(
                    hwnd_,
                    nullptr,
                    FALSE
                );

                UpdateWindow(hwnd_);
            }

            return result;
        }

        case WM_EXITSIZEMOVE:
        {
            /*
                Final full repaint after the user releases the resize edge.
            */
            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

            UpdateWindow(hwnd_);

            return 0;
        }

        case WM_ACTIVATE:
        {
            /*
                DWM can reconsider border appearance when activation changes.
                Re-assert the no-border attributes, then repaint ONLY the
                client area. Do not request RDW_FRAME/WM_NCPAINT.
            */
            applyModernWindowStyle();

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

            UpdateWindow(hwnd_);

            return 0;
        }

        case WM_NCACTIVATE:
        {
            /*
                We own the complete frame. Prevent DefWindowProc from drawing
                the normal active/inactive non-client border.
            */
            applyModernWindowStyle();
            return TRUE;
        }

        case WM_NCPAINT:
        {
            /*
                Critical for the gray-focus-border bug: WS_THICKFRAME normally
                causes Windows/DWM to paint a non-client outline here. The
                launcher paints every visible pixel itself.
            */
            return 0;
        }

        case WM_DWMCOMPOSITIONCHANGED:
        case WM_THEMECHANGED:
        {
            applyModernWindowStyle();

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE
            );

            UpdateWindow(hwnd_);

            return 0;
        }

        case WM_NCCALCSIZE:
        {
            /*
                Make the complete outer window our client area. WS_THICKFRAME
                remains only so Windows honors HTLEFT/HTRIGHT/etc. resizing;
                none of its visual frame is allowed to paint.
            */
            return 0;
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
            KillTimer(
                hwnd_,
                UPDATE_PULSE_TIMER
            );
            KillTimer(
                hwnd_,
                AUTH_HEARTBEAT_TIMER
            );

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
    if (!authenticated_)
    {
        RECT loginClient{};
        GetClientRect(hwnd_, &loginClient);

        // Login wall still uses the normal custom window chrome.  Previously
        // this early return skipped paintTitleBar(), leaving the title-bar
        // buttons clickable but invisible.
        paintLoginWall(dc, loginClient);
        paintTitleBar(dc, loginClient);
        return;
    }

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
        {
            /*
                Keep the sidebar fixed while only the Home content scrolls.
                HOME_CONTENT_HEIGHT is a virtual canvas height; the visible
                client remains clipped to the current window size.
            */
            RECT homeClient = content;

            if (homeClient.bottom < HOME_CONTENT_HEIGHT)
                homeClient.bottom = HOME_CONTENT_HEIGHT;

            setHomeScroll(
                homeScrollY_,
                content
            );

            const int homeSaved =
                SaveDC(memory);

            IntersectClipRect(
                memory,
                220,
                0,
                content.right,
                content.bottom
            );

            OffsetViewportOrgEx(
                memory,
                0,
                -homeScrollY_,
                nullptr
            );

            paintHome(
                memory,
                homeClient
            );

            RestoreDC(
                memory,
                homeSaved
            );

            paintHomeScrollbar(
                memory,
                content
            );

            if (openNewsIndex_ >= 0)
            {
                paintNewsModal(
                    memory,
                    content
                );
            }

            break;
        }

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

        case Page::Media:
            paintMedia(
                memory,
                content
            );

            if (
                mediaOpenInstance_ >= 0 &&
                mediaOpenScreenshot_ >= 0
            )
            {
                paintMediaModal(
                    memory,
                    content
                );
            }
            break;

        case Page::Settings:
            paintSettings(
                memory,
                content
            );
            break;
    }

    if (lowMemoryWarningOpen_)
    {
        paintLowMemoryWarning(
            memory,
            content
        );
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


void MainWindow::createLoginControls()
{
    loginUsername_ = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        0,0,0,0, hwnd_, (HMENU)ID_LOGIN_USERNAME, instance_, nullptr);

    loginPassword_ = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
        0,0,0,0, hwnd_, (HMENU)ID_LOGIN_PASSWORD, instance_, nullptr);

    loginButton_ = CreateWindowExW(
        0, L"BUTTON", L"Sign In",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
        0,0,0,0, hwnd_, (HMENU)ID_LOGIN_BUTTON, instance_, nullptr);

    registerButton_ = CreateWindowExW(
        0, L"BUTTON", L"Register now",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        0,0,0,0, hwnd_, (HMENU)ID_REGISTER_BUTTON, instance_, nullptr);

    for (HWND control : {loginUsername_,loginPassword_,loginButton_,registerButton_})
        SendMessageW(control, WM_SETFONT, (WPARAM)fontNormal_, TRUE);

    SendMessageW(loginUsername_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(12,12));
    SendMessageW(loginPassword_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(12,12));
    SendMessageW(loginPassword_, EM_SETPASSWORDCHAR, static_cast<WPARAM>(L'\x2022'), 0);

}

void MainWindow::showLoginControls(bool show)
{
    const int cmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(loginUsername_,cmd);
    ShowWindow(loginPassword_,cmd);
    ShowWindow(loginButton_,cmd);
    ShowWindow(registerButton_,cmd);

    if(show) {
        RECT c{}; GetClientRect(hwnd_,&c);
        const int width=420;
        const int left=(c.right-width)/2;
        const int top=270;
        // Keep the native EDIT controls single-line so ES_PASSWORD works.
        // The 44px themed field is painted behind them; the actual 28px edit
        // is vertically centered inside it so Win32's text baseline looks correct.
        MoveWindow(loginUsername_,left+8,top+8,width-16,28,TRUE);
        MoveWindow(loginPassword_,left+8,top+90,width-16,28,TRUE);
        MoveWindow(loginButton_,left,top+156,width,46,TRUE);
        MoveWindow(registerButton_,left+244,top+222,176,34,TRUE);
        SetFocus(loginUsername_);
    }
}

void MainWindow::attemptLogin()
{
    if(authBusy_) return;
    wchar_t username[256]{}, password[512]{};
    GetWindowTextW(loginUsername_,username,256);
    GetWindowTextW(loginPassword_,password,512);

    if(username[0]==0 || password[0]==0) {
        authError_=L"Enter your username and password.";
        InvalidateRect(hwnd_,nullptr,FALSE);
        return;
    }

    authBusy_=true;
    authError_=L"Signing in...";
    EnableWindow(loginButton_,FALSE);
    InvalidateRect(hwnd_,nullptr,FALSE);

    std::wstring u=username, p=password;
    std::thread([this,u,p](){
        AuthUser user; std::wstring error;
        bool ok=AuthManager::login(u,p,user,error);
        if(ok) {
            authUser_=user;
            authenticated_=true;
            authError_.clear();
        } else authError_=error.empty()?L"Invalid username or password.":error;
        authBusy_=false;
        PostMessageW(hwnd_,WM_APP+20,ok?1:0,0);
    }).detach();
}

void MainWindow::completeAuthenticatedStartup()
{
    showLoginControls(false);
    SetTimer(hwnd_,AUTH_HEARTBEAT_TIMER,60000,nullptr);
    std::thread([](){ AuthManager::heartbeat(); }).detach();
    ensureAccountArtwork();
    refreshPacks();
    checkForUpdates(false);
    InvalidateRect(hwnd_,nullptr,FALSE);
}

void MainWindow::paintLoginWall(HDC dc, const RECT& client)
{
    fillRectColor(dc,client,BG);

    const int width = 420;
    const int left = (client.right - width) / 2;

    drawTextSimple(
        dc,L"NEWTTECH",
        RECT{0,72,client.right,122},
        fontHero_,TEXT,
        DT_CENTER|DT_SINGLELINE|DT_VCENTER
    );

    drawTextSimple(
        dc,L"L A U N C H E R",
        RECT{0,124,client.right,154},
        fontNormal_,ACCENT,
        DT_CENTER|DT_SINGLELINE|DT_VCENTER
    );

    drawTextSimple(
        dc,L"Sign in to continue",
        RECT{0,178,client.right,224},
        fontTitle_,TEXT,
        DT_CENTER|DT_SINGLELINE|DT_VCENTER
    );

    drawTextSimple(
        dc,L"Username",
        RECT{left,244,left+width,266},
        fontSmall_,MUTED,
        DT_LEFT|DT_SINGLELINE|DT_VCENTER
    );

    drawTextSimple(
        dc,L"Password",
        RECT{left,326,left+width,348},
        fontSmall_,MUTED,
        DT_LEFT|DT_SINGLELINE|DT_VCENTER
    );

    // Full-size themed input surfaces.  The native EDIT windows are inset
    // vertically so their single-line text is visually centered.
    RECT usernameField{
        left,
        270,
        left + width,
        314
    };
    RECT passwordField{
        left,
        352,
        left + width,
        396
    };

    fillRectColor(
        dc,
        usernameField,
        CARD
    );
    fillRectColor(
        dc,
        passwordField,
        CARD
    );

    drawTextSimple(
        dc,L"Don't have an account?",
        RECT{left,486,left+240,520},
        fontSmall_,MUTED,
        DT_LEFT|DT_VCENTER|DT_SINGLELINE
    );

    if(!authError_.empty())
    {
        drawTextSimple(
            dc,authError_,
            RECT{left,548,left+width,604},
            fontSmall_,
            authBusy_ ? MUTED : ACCENT,
            DT_CENTER|DT_WORDBREAK
        );
    }
}


void MainWindow::ensureAccountArtwork()
{
    if (authUser_.minecraftUsername.empty())
        return;

    const std::wstring username =
        authUser_.minecraftUsername;

    const std::wstring displayKey =
        L"account-head:" + username;

    if (imageCache_.contains(displayKey))
        return;

    const HWND notifyWindow = hwnd_;

    std::thread(
        [notifyWindow, username]()
        {
            const HRESULT comResult =
                CoInitializeEx(
                    nullptr,
                    COINIT_APARTMENTTHREADED
                );

            const bool ownsCom =
                SUCCEEDED(comResult);

            HBITMAP bitmap = nullptr;

            try
            {
                // 1. Resolve the Java username through Mojang.
                const std::wstring profileUrl =
                    L"https://api.mojang.com/users/profiles/minecraft/" +
                    username;

                const std::string profileJson =
                    HttpClient::getUtf8(profileUrl);

                const JsonValue profile =
                    JsonLite::parse(profileJson);

                const std::string uuidUtf8 =
                    profile.get("id").asString();

                if (!uuidUtf8.empty())
                {
                    const int chars =
                        MultiByteToWideChar(
                            CP_UTF8,
                            0,
                            uuidUtf8.c_str(),
                            static_cast<int>(
                                uuidUtf8.size()
                            ),
                            nullptr,
                            0
                        );

                    std::wstring uuid(
                        static_cast<size_t>(chars),
                        L'\0'
                    );

                    if (chars > 0)
                    {
                        MultiByteToWideChar(
                            CP_UTF8,
                            0,
                            uuidUtf8.c_str(),
                            static_cast<int>(
                                uuidUtf8.size()
                            ),
                            uuid.data(),
                            chars
                        );
                    }

                    // MCHeads' documented sized-avatar route is:
                    // /avatar/<UUID>/<size>
                    const std::wstring avatarUrl =
                        L"https://mc-heads.net/avatar/" +
                        uuid +
                        L"/96";

                    bitmap =
                        ImageLoader::loadFromUrlPreserveAspect(
                            avatarUrl,
                            96,
                            96
                        );

                    if (bitmap)
                    {
                        auto* result =
                            new AccountAvatarReady{
                                username,
                                uuid,
                                bitmap
                            };

                        bitmap = nullptr;

                        if (
                            !PostMessageW(
                                notifyWindow,
                                WM_ACCOUNT_AVATAR_READY,
                                0,
                                reinterpret_cast<LPARAM>(
                                    result
                                )
                            )
                        )
                        {
                            if (result->bitmap)
                                DeleteObject(result->bitmap);

                            delete result;
                        }
                    }
                }
            }
            catch (...)
            {
                // Cosmetic feature: launcher/login must continue normally.
            }

            if (bitmap)
                DeleteObject(bitmap);

            if (ownsCom)
                CoUninitialize();
        }
    ).detach();
}

RECT MainWindow::sidebarAccountRect() const
{
    return RECT{7, 88, 205, 176};
}

bool MainWindow::homePackInstalled(int index) const
{
    if (
        index < 0 ||
        index >= static_cast<int>(packs_.size())
    )
        return false;

    try
    {
        const std::filesystem::path root(
            InstallEngine::packInstanceRoot(
                settings_.installRoot,
                packs_[index].id
            )
        );

        return std::filesystem::exists(root);
    }
    catch (...)
    {
        return false;
    }
}

std::vector<int> MainWindow::homeInstalledPacks() const
{
    std::vector<int> result;

    for (int i = 0; i < static_cast<int>(packs_.size()); ++i)
    {
        if (homePackInstalled(i))
            result.push_back(i);
    }

    return result;
}

int MainWindow::homeFeaturedPack() const
{
    const std::vector<int> installed =
        homeInstalledPacks();

    if (installed.empty())
        return -1;

    if (
        selectedPack_ >= 0 &&
        homePackInstalled(selectedPack_)
    )
        return selectedPack_;

    for (const int index : installed)
    {
        if (packs_[index].featured)
            return index;
    }

    return installed.front();
}

RECT MainWindow::homeHeroPlayRect(
    const RECT&) const
{
    return RECT{270, 340, 430, 386};
}

RECT MainWindow::homeHeroViewRect(
    const RECT&) const
{
    return RECT{442, 340, 602, 386};
}

RECT MainWindow::homeViewAllPacksRect(
    const RECT& client) const
{
    return RECT{
        client.right - 214,
        451,
        client.right - 78,
        493
    };
}

RECT MainWindow::homeCarouselPrevRect(
    const RECT&) const
{
    return RECT{241, 455, 277, 493};
}

RECT MainWindow::homeCarouselNextRect(
    const RECT& client) const
{
    return RECT{
        client.right - 66,
        455,
        client.right - 30,
        493
    };
}

RECT MainWindow::homeCarouselCardRect(
    const RECT& client,
    int slot) const
{
    const int left = 286;
    const int right = static_cast<int>(client.right) - 78;
    const int gap = 14;
    const int width =
        std::max(
            150,
            (right - left - gap * 2) / 3
        );

    const int x =
        left +
        slot * (width + gap);

    return RECT{
        x,
        505,
        x + width,
        690
    };
}

void MainWindow::paintSidebar(
    HDC dc,
    const RECT& client)
{
    RECT sidebar{0,0,220,client.bottom};
    fillRectColor(dc,sidebar,PANEL);
    fillRectColor(dc,RECT{219,0,220,client.bottom},BORDER);

    // Logged-in Minecraft account card.
    const RECT account = sidebarAccountRect();
    fillRectColor(dc,account,CARD);
    fillRectColor(
        dc,
        RECT{
            account.left,
            account.top,
            account.left + 3,
            account.bottom
        },
        ACCENT
    );

    const std::wstring headKey =
        L"account-head:" +
        authUser_.minecraftUsername;

    auto headIt =
        imageCache_.find(headKey);

    if (
        headIt != imageCache_.end() &&
        headIt->second
    )
    {
        drawBitmapFit(
            dc,
            headIt->second,
            RECT{
                account.left + 12,
                account.top + 14,
                account.left + 68,
                account.top + 70
            }
        );
    }
    else
    {
        fillRectColor(
            dc,
            RECT{
                account.left + 12,
                account.top + 14,
                account.left + 68,
                account.top + 70
            },
            CARD_SELECTED
        );
    }

    drawTextSimple(
        dc,
        L"Logged in as",
        RECT{
            account.left + 80,
            account.top + 12,
            account.right - 8,
            account.top + 31
        },
        fontSmall_,
        MUTED,
        DT_LEFT | DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        authUser_.minecraftUsername.empty()
            ? authUser_.username
            : authUser_.minecraftUsername,
        RECT{
            account.left + 80,
            account.top + 32,
            account.right - 8,
            account.top + 55
        },
        fontNormal_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    drawTextSimple(
        dc,
        L"View Account  >",
        RECT{
            account.left + 80,
            account.top + 59,
            account.right - 8,
            account.bottom - 8
        },
        fontSmall_,
        CYAN,
        DT_LEFT | DT_SINGLELINE
    );

    const wchar_t* nav[] = {
        L"Home",
        L"Modpacks",
        L"Downloads",
        L"Media",
        L"Settings"
    };

    for (int i = 0; i < 5; ++i)
    {
        RECT row{
            7,
            195 + i * 52,
            205,
            239 + i * 52
        };

        if (static_cast<int>(page_) == i)
            fillRectColor(dc,row,CARD);

        RECT textRect = row;
        textRect.left += 16;

        drawTextSimple(
            dc,
            nav[i],
            textRect,
            fontNormal_,
            static_cast<int>(page_) == i ? TEXT : MUTED,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE
        );

        if (
            i == 4 &&
            updateAvailable_ &&
            updatePulseOn_
        )
        {
            HBRUSH dotBrush = CreateSolidBrush(ACCENT);
            HBRUSH oldBrush =
                static_cast<HBRUSH>(SelectObject(dc,dotBrush));
            HPEN dotPen = CreatePen(PS_SOLID,1,ACCENT);
            HPEN oldPen =
                static_cast<HPEN>(SelectObject(dc,dotPen));

            Ellipse(
                dc,
                row.right - 20,
                row.top + 16,
                row.right - 10,
                row.top + 26
            );

            SelectObject(dc,oldPen);
            SelectObject(dc,oldBrush);
            DeleteObject(dotPen);
            DeleteObject(dotBrush);
        }
    }

    // Brand mark moves below navigation, matching the v0.9 account-first layout.
    drawTextSimple(
        dc,
        L"NEWTTECH",
        RECT{
            18,
            client.bottom - 160,
            200,
            client.bottom - 132
        },
        fontBrand_,
        TEXT,
        DT_LEFT | DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"L A U N C H E R",
        RECT{
            18,
            client.bottom - 130,
            205,
            client.bottom - 108
        },
        fontSmall_,
        ACCENT,
        DT_LEFT | DT_SINGLELINE
    );

    RECT status{
        7,
        client.bottom - 90,
        205,
        client.bottom - 20
    };

    fillRectColor(dc,status,CARD);

    drawTextSimple(
        dc,
        L"SERVER STATUS",
        RECT{
            19,
            client.bottom - 80,
            195,
            client.bottom - 61
        },
        fontMeta_,
        MUTED,
        DT_LEFT | DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        statusText_,
        RECT{
            19,
            client.bottom - 57,
            195,
            client.bottom - 26
        },
        fontSmall_,
        TEXT,
        DT_LEFT | DT_WORDBREAK
    );
}

int MainWindow::homeMaxScroll(
    const RECT& client) const
{
    const int viewportHeight =
        std::max(
            0L,
            client.bottom -
            client.top
        );

    return
        std::max(
            0,
            HOME_CONTENT_HEIGHT -
            viewportHeight
        );
}

RECT MainWindow::homeScrollbarTrackRect(
    const RECT& client) const
{
    return RECT{
        client.right - 13,
        12,
        client.right - 5,
        client.bottom - 12
    };
}

RECT MainWindow::homeScrollbarThumbRect(
    const RECT& client) const
{
    const RECT track =
        homeScrollbarTrackRect(
            client
        );

    const int trackHeight =
        std::max(
            1L,
            track.bottom -
            track.top
        );

    const int viewportHeight =
        std::max(
            1L,
            client.bottom -
            client.top
        );

    const int contentHeight =
        std::max(
            HOME_CONTENT_HEIGHT,
            viewportHeight
        );

    int thumbHeight =
        (
            trackHeight *
            viewportHeight
        ) /
        contentHeight;

    thumbHeight =
        std::clamp(
            thumbHeight,
            42,
            trackHeight
        );

    const int maxScroll =
        homeMaxScroll(
            client
        );

    const int usableTrack =
        std::max(
            0,
            trackHeight -
            thumbHeight
        );

    int thumbTop =
        track.top;

    if (
        maxScroll > 0 &&
        usableTrack > 0
    )
    {
        thumbTop +=
            (
                homeScrollY_ *
                usableTrack
            ) /
            maxScroll;
    }

    return RECT{
        track.left,
        thumbTop,
        track.right,
        thumbTop +
        thumbHeight
    };
}

void MainWindow::setHomeScroll(
    int value,
    const RECT& client)
{
    homeScrollY_ =
        std::clamp(
            value,
            0,
            homeMaxScroll(
                client
            )
        );
}

void MainWindow::paintHomeScrollbar(
    HDC dc,
    const RECT& client)
{
    if (
        homeMaxScroll(
            client
        ) <= 0
    )
    {
        return;
    }

    const RECT track =
        homeScrollbarTrackRect(
            client
        );

    const RECT thumb =
        homeScrollbarThumbRect(
            client
        );

    fillRectColor(
        dc,
        track,
        BORDER
    );

    fillRectColor(
        dc,
        thumb,
        homeScrollbarDragging_
            ? CYAN
            : MUTED
    );
}

RECT MainWindow::newsRefreshRect(
    const RECT& client) const
{
    const LONG top = 741;

    return RECT{
        client.right - 142,
        top,
        client.right - 46,
        top + 38
    };
}

RECT MainWindow::newsCardRect(
    const RECT& client,
    int index) const
{
    const LONG left = 241;
    const LONG right =
        client.right - 30;
    const LONG gap = 12;

    const int count =
        static_cast<int>(
            std::min<size_t>(
                news_.size(),
                3
            )
        );

    if (
        count <= 0 ||
        index < 0 ||
        index >= count
    )
    {
        return RECT{};
    }

    const LONG available =
        right -
        left -
        gap * (count - 1);

    const LONG width =
        available / count;

    const LONG top = 806;
    const LONG bottom = 1010;

    const LONG x =
        left +
        index * (width + gap);

    return RECT{
        x,
        top,
        x + width,
        bottom
    };
}

RECT MainWindow::newsModalRect(
    const RECT& client) const
{
    const LONG width =
        std::min<LONG>(
            760L,
            std::max<LONG>(
                520L,
                client.right - 300
            )
        );

    const LONG height =
        std::min<LONG>(
            590L,
            std::max<LONG>(
                430L,
                client.bottom - 70
            )
        );

    const LONG left =
        220 +
        (
            (client.right - 220) -
            width
        ) / 2;

    const LONG top =
        (
            client.bottom -
            height
        ) / 2;

    return RECT{
        left,
        top,
        left + width,
        top + height
    };
}

RECT MainWindow::newsModalCloseRect(
    const RECT& client) const
{
    const RECT modal =
        newsModalRect(
            client
        );

    return RECT{
        modal.right - 48,
        modal.top + 10,
        modal.right - 12,
        modal.top + 42
    };
}


RECT MainWindow::newsModalScrollbarTrackRect(
    const RECT& client) const
{
    const RECT modal =
        newsModalRect(client);

    return RECT{
        modal.right - 15,
        modal.top + 54,
        modal.right - 7,
        modal.bottom - 16
    };
}

RECT MainWindow::newsModalScrollbarThumbRect(
    const RECT& client) const
{
    const RECT track =
        newsModalScrollbarTrackRect(client);

    const LONG trackHeight =
        std::max<LONG>(
            1L,
            track.bottom - track.top
        );

    if (newsModalMaxScroll_ <= 0)
    {
        return RECT{
            track.left,
            track.top,
            track.right,
            track.bottom
        };
    }

    const RECT modal =
        newsModalRect(client);

    const LONG viewportHeight =
        std::max<LONG>(
            1L,
            modal.bottom -
            modal.top -
            72
        );

    const LONG virtualHeight =
        viewportHeight +
        newsModalMaxScroll_;

    LONG thumbHeight =
        (
            trackHeight *
            viewportHeight
        ) /
        std::max<LONG>(
            1L,
            virtualHeight
        );

    thumbHeight =
        std::clamp<LONG>(
            thumbHeight,
            44L,
            trackHeight
        );

    const LONG usableTrack =
        std::max<LONG>(
            0L,
            trackHeight -
            thumbHeight
        );

    LONG thumbTop =
        track.top;

    if (
        usableTrack > 0 &&
        newsModalMaxScroll_ > 0
    )
    {
        thumbTop +=
            (
                static_cast<LONG>(
                    newsModalScrollY_
                ) *
                usableTrack
            ) /
            newsModalMaxScroll_;
    }

    return RECT{
        track.left,
        thumbTop,
        track.right,
        thumbTop +
        thumbHeight
    };
}

void MainWindow::setNewsModalScroll(
    int value)
{
    newsModalScrollY_ =
        std::clamp(
            value,
            0,
            std::max(
                0,
                newsModalMaxScroll_
            )
        );
}



LONG MainWindow::renderNewsMarkdown(
    HDC dc,
    const std::wstring& markdown,
    const RECT& bounds,
    bool draw)
{
    struct InlineToken
    {
        std::wstring text;
        bool link = false;
        std::wstring url;
    };

    auto tokenizeInline =
        [](const std::wstring& text)
        {
            std::vector<InlineToken> tokens;

            auto addPlain =
                [&tokens](const std::wstring& plain)
                {
                    std::wstring word;

                    auto flushWord =
                        [&]()
                        {
                            if (!word.empty())
                            {
                                tokens.push_back(
                                    InlineToken{
                                        word,
                                        false,
                                        L""
                                    }
                                );

                                word.clear();
                            }
                        };

                    for (wchar_t c : plain)
                    {
                        if (
                            c == L' ' ||
                            c == L'\t'
                        )
                        {
                            flushWord();

                            tokens.push_back(
                                InlineToken{
                                    L" ",
                                    false,
                                    L""
                                }
                            );
                        }
                        else
                        {
                            word += c;
                        }
                    }

                    flushWord();
                };

            size_t cursor = 0;

            while (cursor < text.size())
            {
                const size_t open =
                    text.find(
                        L'[',
                        cursor
                    );

                if (open == std::wstring::npos)
                {
                    addPlain(
                        text.substr(cursor)
                    );
                    break;
                }

                const size_t close =
                    text.find(
                        L']',
                        open + 1
                    );

                if (
                    close == std::wstring::npos ||
                    close + 1 >= text.size() ||
                    text[close + 1] != L'('
                )
                {
                    addPlain(
                        text.substr(cursor)
                    );
                    break;
                }

                const size_t urlClose =
                    text.find(
                        L')',
                        close + 2
                    );

                if (urlClose == std::wstring::npos)
                {
                    addPlain(
                        text.substr(cursor)
                    );
                    break;
                }

                addPlain(
                    text.substr(
                        cursor,
                        open - cursor
                    )
                );

                const std::wstring label =
                    text.substr(
                        open + 1,
                        close - open - 1
                    );

                const std::wstring url =
                    text.substr(
                        close + 2,
                        urlClose - close - 2
                    );

                if (
                    !label.empty() &&
                    (
                        url.rfind(
                            L"https://",
                            0
                        ) == 0 ||
                        url.rfind(
                            L"http://",
                            0
                        ) == 0
                    )
                )
                {
                    tokens.push_back(
                        InlineToken{
                            label,
                            true,
                            url
                        }
                    );
                }
                else
                {
                    addPlain(
                        text.substr(
                            open,
                            urlClose - open + 1
                        )
                    );
                }

                cursor =
                    urlClose + 1;
            }

            return tokens;
        };

    auto renderInline =
        [&](const std::wstring& text,
            HFONT font,
            COLORREF color,
            LONG left,
            LONG right,
            LONG top,
            bool actuallyDraw) -> LONG
        {
            const std::vector<InlineToken> tokens =
                tokenizeInline(text);

            HFONT oldFont =
                static_cast<HFONT>(
                    SelectObject(
                        dc,
                        font
                    )
                );

            TEXTMETRICW metrics{};
            GetTextMetricsW(
                dc,
                &metrics
            );

            const LONG lineHeight =
                std::max<LONG>(
                    18L,
                    metrics.tmHeight + 6
                );

            LONG x = left;
            LONG y = top;

            for (const InlineToken& token : tokens)
            {
                if (token.text.empty())
                    continue;

                SIZE extent{};

                GetTextExtentPoint32W(
                    dc,
                    token.text.c_str(),
                    static_cast<int>(
                        token.text.size()
                    ),
                    &extent
                );

                if (
                    token.text == L" " &&
                    x == left
                )
                {
                    continue;
                }

                if (
                    x != left &&
                    x + extent.cx > right
                )
                {
                    x = left;
                    y += lineHeight;

                    if (token.text == L" ")
                        continue;
                }

                if (actuallyDraw)
                {
                    SetBkMode(
                        dc,
                        TRANSPARENT
                    );

                    SetTextColor(
                        dc,
                        token.link
                            ? CYAN
                            : color
                    );

                    TextOutW(
                        dc,
                        x,
                        y,
                        token.text.c_str(),
                        static_cast<int>(
                            token.text.size()
                        )
                    );

                    if (token.link)
                    {
                        const LONG underlineY =
                            y +
                            metrics.tmAscent +
                            2;

                        HPEN pen =
                            CreatePen(
                                PS_SOLID,
                                1,
                                CYAN
                            );

                        HPEN oldPen =
                            static_cast<HPEN>(
                                SelectObject(
                                    dc,
                                    pen
                                )
                            );

                        MoveToEx(
                            dc,
                            x,
                            underlineY,
                            nullptr
                        );

                        LineTo(
                            dc,
                            x + extent.cx,
                            underlineY
                        );

                        SelectObject(
                            dc,
                            oldPen
                        );

                        DeleteObject(pen);

                        RECT hit{
                            x,
                            y -
                            newsModalScrollY_,
                            x + extent.cx,
                            y +
                            lineHeight -
                            newsModalScrollY_
                        };

                        newsModalLinks_.push_back(
                            NewsMarkdownLink{
                                hit,
                                token.url
                            }
                        );
                    }
                }

                x += extent.cx;
            }

            SelectObject(
                dc,
                oldFont
            );

            return
                (y - top) +
                lineHeight;
        };

    auto splitLines =
        [](const std::wstring& input)
        {
            std::vector<std::wstring> lines;
            std::wstring current;

            for (wchar_t c : input)
            {
                if (c == L'\r')
                    continue;

                if (c == L'\n')
                {
                    lines.push_back(
                        current
                    );

                    current.clear();
                }
                else
                {
                    current += c;
                }
            }

            lines.push_back(
                current
            );

            return lines;
        };

    const std::vector<std::wstring> lines =
        splitLines(markdown);

    const LONG left =
        bounds.left;

    const LONG right =
        bounds.right;

    LONG y =
        bounds.top;

    bool inCode = false;
    std::wstring codeBuffer;

    auto renderCode =
        [&](const std::wstring& code)
        {
            if (code.empty())
                return;

            HFONT codeFont =
                static_cast<HFONT>(
                    GetStockObject(
                        ANSI_FIXED_FONT
                    )
                );

            RECT measure{
                left + 14,
                y + 12,
                right - 14,
                y + 12
            };

            HFONT oldFont =
                static_cast<HFONT>(
                    SelectObject(
                        dc,
                        codeFont
                    )
                );

            DrawTextW(
                dc,
                code.c_str(),
                -1,
                &measure,
                DT_LEFT |
                DT_WORDBREAK |
                DT_CALCRECT |
                DT_NOPREFIX
            );

            SelectObject(
                dc,
                oldFont
            );

            const LONG height =
                std::max<LONG>(
                    44L,
                    measure.bottom -
                    measure.top +
                    24
                );

            if (draw)
            {
                RECT block{
                    left,
                    y,
                    right,
                    y + height
                };

                fillRectColor(
                    dc,
                    block,
                    RGB(3, 8, 20)
                );

                fillRectColor(
                    dc,
                    RECT{
                        block.left,
                        block.top,
                        block.left + 3,
                        block.bottom
                    },
                    MUTED
                );

                drawTextSimple(
                    dc,
                    code,
                    RECT{
                        block.left + 14,
                        block.top + 12,
                        block.right - 14,
                        block.bottom - 12
                    },
                    codeFont,
                    SUCCESS,
                    DT_LEFT |
                    DT_WORDBREAK |
                    DT_NOPREFIX
                );
            }

            y +=
                height + 12;
        };

    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::wstring& raw =
            lines[i];

        /*
            Custom grouped-list syntax:

                -- Hardware
                - CPU
                - GPU

                -- Software
                Minecraft
                Forge

            A line beginning with "-- " is the group heading.
            Every following nonblank line becomes an item until either:
              1. a blank line, or
              2. another "-- " heading.

            Item lines may optionally begin with "- ".
        */
        if (
            !inCode &&
            raw.rfind(
                L"-- ",
                0
            ) == 0
        )
        {
            const std::wstring groupTitle =
                raw.substr(3);

            // Group heading
            const LONG headingHeight =
                renderInline(
                    groupTitle,
                    fontBrand_,
                    CYAN,
                    left,
                    right,
                    y,
                    draw
                );

            y +=
                headingHeight + 4;

            // Thin accent divider under the group title.
            if (draw)
            {
                fillRectColor(
                    dc,
                    RECT{
                        left,
                        y,
                        right,
                        y + 2
                    },
                    CYAN
                );
            }

            y += 10;

            // Consume following item lines.
            size_t itemIndex =
                i + 1;

            for (
                ;
                itemIndex < lines.size();
                ++itemIndex
            )
            {
                const std::wstring& itemRaw =
                    lines[itemIndex];

                if (itemRaw.empty())
                    break;

                if (
                    itemRaw.rfind(
                        L"-- ",
                        0
                    ) == 0
                )
                {
                    break;
                }

                std::wstring itemText =
                    itemRaw;

                if (
                    itemText.rfind(
                        L"- ",
                        0
                    ) == 0
                )
                {
                    itemText =
                        itemText.substr(2);
                }

                // Treat a bare "-" or "- " as an empty spacer item.
                if (
                    itemText == L"-" ||
                    itemText == L" "
                )
                {
                    itemText.clear();
                }

                if (!itemText.empty())
                {
                    HFONT bulletFont =
                        fontNormal_;

                    HFONT oldFont =
                        static_cast<HFONT>(
                            SelectObject(
                                dc,
                                bulletFont
                            )
                        );

                    TEXTMETRICW metrics{};
                    GetTextMetricsW(
                        dc,
                        &metrics
                    );

                    SelectObject(
                        dc,
                        oldFont
                    );

                    const LONG lineHeight =
                        std::max<LONG>(
                            18L,
                            metrics.tmHeight + 6
                        );

                    const LONG bulletX =
                        left + 10;

                    const LONG textLeft =
                        left + 32;

                    const LONG itemHeight =
                        renderInline(
                            itemText,
                            bulletFont,
                            TEXT,
                            textLeft,
                            right,
                            y,
                            draw
                        );

                    if (draw)
                    {
                        drawTextSimple(
                            dc,
                            L"•",
                            RECT{
                                bulletX,
                                y,
                                textLeft - 6,
                                y + lineHeight
                            },
                            bulletFont,
                            CYAN,
                            DT_LEFT |
                            DT_SINGLELINE |
                            DT_VCENTER
                        );
                    }

                    y +=
                        std::max<LONG>(
                            lineHeight,
                            itemHeight
                        ) +
                        5;
                }
                else
                {
                    y += 8;
                }
            }

            /*
                If the loop stopped because it found another "-- " heading,
                leave i pointing at the item before it so the outer loop will
                process the next heading normally.
            */
            if (
                itemIndex > i + 1
            )
            {
                i =
                    itemIndex - 1;
            }

            y += 6;
            continue;
        }

        if (
            raw.rfind(
                L"```",
                0
            ) == 0
        )
        {
            if (inCode)
            {
                renderCode(
                    codeBuffer
                );

                codeBuffer.clear();
                inCode = false;
            }
            else
            {
                inCode = true;
            }

            continue;
        }

        if (inCode)
        {
            if (!codeBuffer.empty())
                codeBuffer += L"\n";

            codeBuffer += raw;
            continue;
        }

        if (raw.empty())
        {
            y += 12;
            continue;
        }

        int headingLevel = 0;
        std::wstring content = raw;

        if (
            raw.rfind(
                L"### ",
                0
            ) == 0
        )
        {
            headingLevel = 3;
            content = raw.substr(4);
        }
        else if (
            raw.rfind(
                L"## ",
                0
            ) == 0
        )
        {
            headingLevel = 2;
            content = raw.substr(3);
        }
        else if (
            raw.rfind(
                L"# ",
                0
            ) == 0
        )
        {
            headingLevel = 1;
            content = raw.substr(2);
        }

        if (headingLevel > 0)
        {
            HFONT headingFont =
                headingLevel == 1
                    ? fontHero_
                    : (
                        headingLevel == 2
                            ? fontTitle_
                            : fontBrand_
                    );

            const LONG height =
                renderInline(
                    content,
                    headingFont,
                    TEXT,
                    left,
                    right,
                    y,
                    draw
                );

            y +=
                height + 8;

            continue;
        }

        if (
            raw.rfind(
                L"- ",
                0
            ) == 0
        )
        {
            const std::wstring listText =
                raw.substr(2);

            HFONT bulletFont =
                fontNormal_;

            HFONT oldFont =
                static_cast<HFONT>(
                    SelectObject(
                        dc,
                        bulletFont
                    )
                );

            TEXTMETRICW metrics{};
            GetTextMetricsW(
                dc,
                &metrics
            );

            SelectObject(
                dc,
                oldFont
            );

            const LONG lineHeight =
                std::max<LONG>(
                    18L,
                    metrics.tmHeight + 6
                );

            const LONG bulletX =
                left + 2;

            const LONG textLeft =
                left + 22;

            const LONG textHeight =
                renderInline(
                    listText,
                    bulletFont,
                    TEXT,
                    textLeft,
                    right,
                    y,
                    draw
                );

            if (draw)
            {
                /*
                    Discord-style unordered list marker.
                    Use a real Unicode bullet instead of rendering the source '-'.
                */
                drawTextSimple(
                    dc,
                    L"•",
                    RECT{
                        bulletX,
                        y,
                        textLeft - 4,
                        y + lineHeight
                    },
                    bulletFont,
                    CYAN,
                    DT_LEFT |
                    DT_SINGLELINE |
                    DT_VCENTER
                );
            }

            y +=
                std::max<LONG>(
                    lineHeight,
                    textHeight
                ) +
                6;

            continue;
        }

        if (
            raw.rfind(
                L"> ",
                0
            ) == 0 ||
            raw == L">"
        )
        {
            const std::wstring quoteText =
                raw.size() > 2
                    ? raw.substr(2)
                    : L"";

            const LONG textHeight =
                renderInline(
                    quoteText,
                    fontNormal_,
                    MUTED,
                    left + 18,
                    right - 12,
                    y + 10,
                    false
                );

            const LONG blockHeight =
                std::max<LONG>(
                    40L,
                    textHeight + 20
                );

            if (draw)
            {
                fillRectColor(
                    dc,
                    RECT{
                        left,
                        y,
                        right,
                        y + blockHeight
                    },
                    PANEL
                );

                fillRectColor(
                    dc,
                    RECT{
                        left,
                        y,
                        left + 4,
                        y + blockHeight
                    },
                    CYAN
                );

                renderInline(
                    quoteText,
                    fontNormal_,
                    MUTED,
                    left + 18,
                    right - 12,
                    y + 10,
                    true
                );
            }

            y +=
                blockHeight + 10;

            continue;
        }

        const LONG paragraphHeight =
            renderInline(
                raw,
                fontNormal_,
                TEXT,
                left,
                right,
                y,
                draw
            );

        y +=
            paragraphHeight + 8;
    }

    if (inCode)
    {
        renderCode(
            codeBuffer
        );
    }

    return
        std::max<LONG>(
            0L,
            y - bounds.top
        );
}


void MainWindow::paintNewsModal(
    HDC dc,
    const RECT& client)
{
    if (
        openNewsIndex_ < 0 ||
        openNewsIndex_ >=
            static_cast<int>(
                news_.size()
            )
    )
    {
        return;
    }

    const NewsItem& item =
        news_[openNewsIndex_];

    RECT overlay{
        220,
        0,
        client.right,
        client.bottom
    };

    fillRectColor(
        dc,
        overlay,
        RGB(3, 8, 20)
    );

    const RECT modal =
        newsModalRect(client);

    fillRectColor(
        dc,
        modal,
        CARD
    );

    fillRectColor(
        dc,
        RECT{
            modal.left,
            modal.top,
            modal.left + 5,
            modal.bottom
        },
        item.pinned
            ? ACCENT
            : CYAN
    );

    RECT viewport{
        modal.left + 8,
        modal.top + 10,
        modal.right - 22,
        modal.bottom - 12
    };

    const std::wstring body =
        item.body.empty()
            ? item.summary
            : item.body;

    LONG contentTop =
        modal.top + 22;

    bool hasArtwork = false;

    if (!item.imageUrl.empty())
    {
        const std::wstring imageKey =
            L"news:" +
            item.imageUrl;

        auto it =
            imageCache_.find(imageKey);

        hasArtwork =
            it != imageCache_.end() &&
            it->second;
    }

    if (hasArtwork)
        contentTop = modal.top + 300;

    const LONG bodyTop =
        contentTop + 112;

    RECT markdownBounds{
        modal.left + 24,
        bodyTop,
        modal.right - 42,
        bodyTop + 100000
    };

    const LONG markdownHeight =
        renderNewsMarkdown(
            dc,
            body,
            markdownBounds,
            false
        );

    const LONG virtualBottom =
        bodyTop +
        markdownHeight +
        34;

    newsModalMaxScroll_ =
        std::max(
            0,
            static_cast<int>(
                virtualBottom -
                viewport.bottom
            )
        );

    setNewsModalScroll(
        newsModalScrollY_
    );

    const int saved =
        SaveDC(dc);

    IntersectClipRect(
        dc,
        viewport.left,
        viewport.top,
        viewport.right,
        viewport.bottom
    );

    OffsetViewportOrgEx(
        dc,
        0,
        -newsModalScrollY_,
        nullptr
    );

    contentTop =
        modal.top + 22;

    if (hasArtwork)
    {
        const std::wstring imageKey =
            L"news:" +
            item.imageUrl;

        auto it =
            imageCache_.find(imageKey);

        RECT hero{
            modal.left + 48,
            modal.top + 22,
            modal.right - 48,
            modal.top + 282
        };

        drawBitmapFit(
            dc,
            it->second,
            hero
        );

        contentTop =
            hero.bottom + 18;
    }

    drawTextSimple(
        dc,
        item.pinned
            ? L"PINNED • " + item.category
            : item.category,
        RECT{
            modal.left + 24,
            contentTop,
            modal.right - 42,
            contentTop + 23
        },
        fontMeta_,
        item.pinned
            ? ACCENT
            : CYAN,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    drawTextSimple(
        dc,
        item.title,
        RECT{
            modal.left + 24,
            contentTop + 29,
            modal.right - 42,
            contentTop + 72
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    drawTextSimple(
        dc,
        formatNewsDate(
            item.published
        ),
        RECT{
            modal.left + 24,
            contentTop + 76,
            modal.right - 42,
            contentTop + 98
        },
        fontSmall_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    newsModalLinks_.clear();

    renderNewsMarkdown(
        dc,
        body,
        RECT{
            modal.left + 24,
            contentTop + 112,
            modal.right - 42,
            virtualBottom
        },
        true
    );

    RestoreDC(
        dc,
        saved
    );

    if (newsModalMaxScroll_ > 0)
    {
        const RECT track =
            newsModalScrollbarTrackRect(
                client
            );

        const RECT thumb =
            newsModalScrollbarThumbRect(
                client
            );

        fillRectColor(
            dc,
            track,
            BORDER
        );

        fillRectColor(
            dc,
            thumb,
            newsModalScrollbarDragging_
                ? CYAN
                : MUTED
        );
    }

    const RECT close =
        newsModalCloseRect(client);

    fillRectColor(
        dc,
        close,
        ACCENT
    );

    drawTextSimple(
        dc,
        L"×",
        close,
        fontNormal_,
        TEXT_DARK,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );
}


void MainWindow::paintHome(
    HDC dc,
    const RECT& client)
{
    const std::wstring player =
        authUser_.minecraftUsername.empty()
            ? authUser_.username
            : authUser_.minecraftUsername;

    // v0.9 polish: keep the greeting white but highlight the signed-in
    // Minecraft username with the launcher's magenta accent.
    const std::wstring welcomePrefix =
        L"Welcome back, ";

    RECT welcomeMeasure{0,0,0,0};

    DrawTextW(
        dc,
        welcomePrefix.c_str(),
        -1,
        &welcomeMeasure,
        DT_CALCRECT | DT_SINGLELINE
    );

    // DrawTextW above uses the currently selected font, so select fontTitle_
    // while measuring to ensure the username begins exactly after the prefix.
    HFONT oldWelcomeFont =
        static_cast<HFONT>(
            SelectObject(
                dc,
                fontTitle_
            )
        );

    welcomeMeasure = RECT{0,0,0,0};

    DrawTextW(
        dc,
        welcomePrefix.c_str(),
        -1,
        &welcomeMeasure,
        DT_CALCRECT | DT_SINGLELINE
    );

    SelectObject(
        dc,
        oldWelcomeFont
    );

    const LONG welcomeNameX =
        241 +
        (welcomeMeasure.right -
         welcomeMeasure.left);

    drawTextSimple(
        dc,
        welcomePrefix,
        RECT{
            241,
            24,
            welcomeNameX + 4,
            65
        },
        fontTitle_,
        TEXT,
        DT_LEFT | DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        player + L" !",
        RECT{
            welcomeNameX,
            24,
            client.right - 30,
            65
        },
        fontTitle_,
        ACCENT,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    drawTextSimple(
        dc,
        L"Manage your modpacks, keep up with updates, and jump into your next adventure.",
        RECT{241,66,client.right - 30,94},
        fontNormal_,
        MUTED,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    const int featured =
        homeFeaturedPack();

    RECT hero{
        241,
        112,
        client.right - 30,
        410
    };

    fillRectColor(dc,hero,CARD);

    if (featured >= 0)
    {
        const Modpack& pack =
            packs_[featured];

        const std::wstring bannerKey =
            L"banner:" + pack.bannerUrl;

        auto bannerIt =
            imageCache_.find(bannerKey);

        if (
            bannerIt != imageCache_.end() &&
            bannerIt->second
        )
        {
            drawBitmapCover(
                dc,
                bannerIt->second,
                hero
            );
        }

        /*
            v0.9 featured-banner polish:
            Keep the banner visible across the ENTIRE hero and fade the
            launcher's panel color from opaque on the text side to fully
            transparent toward the artwork.

            We build a one-row premultiplied BGRA DIB and stretch it over the
            hero. AlphaBlend is already used elsewhere by the launcher.
        */
        RECT info{
            hero.left,
            hero.top,
            std::min<LONG>(
                hero.right,
                hero.left + 610
            ),
            hero.bottom
        };

        const int fadeWidth =
            std::max(
                1,
                static_cast<int>(
                    info.right - info.left
                )
            );

        BITMAPINFO fadeInfo{};
        fadeInfo.bmiHeader.biSize =
            sizeof(BITMAPINFOHEADER);
        fadeInfo.bmiHeader.biWidth =
            fadeWidth;
        fadeInfo.bmiHeader.biHeight =
            -1;
        fadeInfo.bmiHeader.biPlanes = 1;
        fadeInfo.bmiHeader.biBitCount = 32;
        fadeInfo.bmiHeader.biCompression =
            BI_RGB;

        void* fadePixels = nullptr;

        HBITMAP fadeBitmap =
            CreateDIBSection(
                dc,
                &fadeInfo,
                DIB_RGB_COLORS,
                &fadePixels,
                nullptr,
                0
            );

        if (fadeBitmap && fadePixels)
        {
            auto* pixels =
                static_cast<unsigned char*>(
                    fadePixels
                );

            const unsigned char panelR =
                GetRValue(PANEL);
            const unsigned char panelG =
                GetGValue(PANEL);
            const unsigned char panelB =
                GetBValue(PANEL);

            for (int px = 0; px < fadeWidth; ++px)
            {
                const double t =
                    fadeWidth <= 1
                        ? 1.0
                        : static_cast<double>(px) /
                          static_cast<double>(
                              fadeWidth - 1
                          );

                /*
                    Hold the left side nearly opaque so the pack text remains
                    easy to read, then smoothly reveal the banner. The eased
                    falloff avoids a visible vertical seam.
                */
                double alphaFactor = 0.0;

                if (t < 0.34)
                {
                    alphaFactor = 0.96;
                }
                else
                {
                    const double fadeT =
                        (t - 0.34) / 0.66;

                    const double eased =
                        fadeT * fadeT *
                        (3.0 - 2.0 * fadeT);

                    alphaFactor =
                        0.96 * (1.0 - eased);
                }

                const unsigned char alpha =
                    static_cast<unsigned char>(
                        255.0 * alphaFactor
                    );

                // AlphaBlend expects premultiplied BGRA source pixels.
                pixels[px * 4 + 0] =
                    static_cast<unsigned char>(
                        panelB * alpha / 255
                    );
                pixels[px * 4 + 1] =
                    static_cast<unsigned char>(
                        panelG * alpha / 255
                    );
                pixels[px * 4 + 2] =
                    static_cast<unsigned char>(
                        panelR * alpha / 255
                    );
                pixels[px * 4 + 3] =
                    alpha;
            }

            HDC fadeDc =
                CreateCompatibleDC(dc);

            if (fadeDc)
            {
                HBITMAP oldFade =
                    static_cast<HBITMAP>(
                        SelectObject(
                            fadeDc,
                            fadeBitmap
                        )
                    );

                BLENDFUNCTION fadeBlend{};
                fadeBlend.BlendOp =
                    AC_SRC_OVER;
                fadeBlend.BlendFlags = 0;
                fadeBlend.SourceConstantAlpha =
                    255;
                fadeBlend.AlphaFormat =
                    AC_SRC_ALPHA;

                AlphaBlend(
                    dc,
                    info.left,
                    info.top,
                    fadeWidth,
                    info.bottom - info.top,
                    fadeDc,
                    0,
                    0,
                    fadeWidth,
                    1,
                    fadeBlend
                );

                SelectObject(
                    fadeDc,
                    oldFade
                );

                DeleteDC(fadeDc);
            }

            DeleteObject(fadeBitmap);
        }

        drawTextSimple(
            dc,
            L"FEATURED MODPACK",
            RECT{
                info.left + 28,
                info.top + 24,
                info.right - 20,
                info.top + 47
            },
            fontMeta_,
            CYAN,
            DT_LEFT | DT_SINGLELINE
        );

        drawTextSimple(
            dc,
            pack.name,
            RECT{
                info.left + 28,
                info.top + 58,
                info.right - 20,
                info.top + 105
            },
            fontHero_,
            TEXT,
            DT_LEFT |
            DT_SINGLELINE |
            DT_END_ELLIPSIS
        );

        // The description can extend into the transparent portion of the
        // hero gradient. Use the launcher's cyan and a stronger font weight
        // so it keeps contrast against bright or detailed banner artwork.
        drawTextSimple(
            dc,
            pack.description,
            RECT{
                info.left + 28,
                info.top + 118,
                info.right - 24,
                info.top + 202
            },
            fontNormal_,
            CYAN,
            DT_LEFT |
            DT_WORDBREAK |
            DT_END_ELLIPSIS
        );

        const RECT play =
            homeHeroPlayRect(client);

        const RECT view =
            homeHeroViewRect(client);

        fillRectColor(dc,play,ACCENT);
        fillRectColor(dc,view,CARD_SELECTED);

        drawTextSimple(
            dc,
            L"Play Now",
            play,
            fontNormal_,
            TEXT_DARK,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );

        drawTextSimple(
            dc,
            L"View Modpack",
            view,
            fontNormal_,
            TEXT,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );
    }
    else
    {
        drawTextSimple(
            dc,
            L"No installed modpacks yet",
            RECT{
                hero.left + 28,
                hero.top + 60,
                hero.right - 28,
                hero.top + 105
            },
            fontHero_,
            TEXT,
            DT_LEFT | DT_SINGLELINE
        );

        drawTextSimple(
            dc,
            L"Install a modpack from the Modpacks tab and it will appear here.",
            RECT{
                hero.left + 28,
                hero.top + 125,
                hero.right - 28,
                hero.top + 175
            },
            fontNormal_,
            MUTED,
            DT_LEFT | DT_WORDBREAK
        );
    }

    // Installed-pack carousel.
    drawTextSimple(
        dc,
        L"YOUR MODPACKS",
        RECT{286,455,client.right - 245,490},
        fontTitle_,
        TEXT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE
    );

    const RECT viewAll =
        homeViewAllPacksRect(client);

    fillRectColor(dc,viewAll,CARD_SELECTED);

    drawTextSimple(
        dc,
        L"View All Modpacks  >",
        viewAll,
        fontSmall_,
        TEXT,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );

    const std::vector<int> installed =
        homeInstalledPacks();

    const int maxOffset =
        std::max(
            0,
            static_cast<int>(installed.size()) - 3
        );

    homeCarouselOffset_ =
        std::clamp(
            homeCarouselOffset_,
            0,
            maxOffset
        );

    const RECT prev =
        homeCarouselPrevRect(client);

    const RECT next =
        homeCarouselNextRect(client);

    fillRectColor(
        dc,
        prev,
        homeCarouselOffset_ > 0
            ? CARD_SELECTED
            : PANEL
    );

    fillRectColor(
        dc,
        next,
        homeCarouselOffset_ < maxOffset
            ? CARD_SELECTED
            : PANEL
    );

    drawTextSimple(
        dc,
        L"<",
        prev,
        fontTitle_,
        homeCarouselOffset_ > 0 ? CYAN : MUTED,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L">",
        next,
        fontTitle_,
        homeCarouselOffset_ < maxOffset ? CYAN : MUTED,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );

    if (installed.empty())
    {
        drawTextSimple(
            dc,
            L"Your installed modpacks will appear here.",
            RECT{286,520,client.right - 78,570},
            fontNormal_,
            MUTED,
            DT_LEFT | DT_SINGLELINE
        );
    }
    else
    {
        for (int slot = 0; slot < 3; ++slot)
        {
            const int position =
                homeCarouselOffset_ + slot;

            if (
                position >=
                static_cast<int>(installed.size())
            )
                break;

            const int index =
                installed[position];

            const Modpack& pack =
                packs_[index];

            const RECT card =
                homeCarouselCardRect(
                    client,
                    slot
                );

            fillRectColor(
                dc,
                card,
                index == featured
                    ? CARD_SELECTED
                    : PANEL
            );

            fillRectColor(
                dc,
                RECT{
                    card.left,
                    card.top,
                    card.right,
                    card.top + 3
                },
                index == featured
                    ? ACCENT
                    : CYAN
            );

            const RECT banner{
                card.left,
                card.top + 3,
                card.right,
                card.top + 98
            };

            const std::wstring bannerKey =
                L"banner:" +
                pack.bannerUrl;

            auto bannerIt =
                imageCache_.find(
                    bannerKey
                );

            if (
                bannerIt != imageCache_.end() &&
                bannerIt->second
            )
            {
                drawBitmapCover(
                    dc,
                    bannerIt->second,
                    banner
                );
            }
            else
            {
                fillRectColor(
                    dc,
                    banner,
                    CARD
                );
            }

            drawTextSimple(
                dc,
                pack.name,
                RECT{
                    card.left + 12,
                    card.top + 110,
                    card.right - 12,
                    card.top + 137
                },
                fontNormal_,
                TEXT,
                DT_LEFT |
                DT_SINGLELINE |
                DT_END_ELLIPSIS
            );

            drawTextSimple(
                dc,
                L"Installed",
                RECT{
                    card.left + 12,
                    card.top + 140,
                    card.right - 12,
                    card.top + 162
                },
                fontSmall_,
                MUTED,
                DT_LEFT |
                DT_SINGLELINE |
                DT_END_ELLIPSIS
            );

            drawTextSimple(
                dc,
                index == featured
                    ? L"Selected"
                    : L"Select",
                RECT{
                    card.left + 12,
                    card.bottom - 24,
                    card.right - 12,
                    card.bottom - 7
                },
                fontSmall_,
                index == featured
                    ? ACCENT
                    : CYAN,
                DT_RIGHT | DT_SINGLELINE
            );
        }
    }

    /*
        News & Updates now begins below the v0.9 installed-pack carousel.
    */
    const LONG newsTop = 730;

    RECT newsHeader{
        241,
        newsTop,
        client.right - 30,
        newsTop + 64
    };

    fillRectColor(dc,newsHeader,CARD);
    fillRectColor(
        dc,
        RECT{
            newsHeader.left,
            newsHeader.top,
            newsHeader.left + 4,
            newsHeader.bottom
        },
        CYAN
    );

    drawTextSimple(
        dc,
        L"NEWS & UPDATES",
        RECT{
            newsHeader.left + 18,
            newsHeader.top + 10,
            newsHeader.right - 150,
            newsHeader.top + 31
        },
        fontMeta_,
        CYAN,
        DT_LEFT | DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        newsStatus_,
        RECT{
            newsHeader.left + 18,
            newsHeader.top + 33,
            newsHeader.right - 150,
            newsHeader.bottom - 8
        },
        fontSmall_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    const RECT refresh =
        newsRefreshRect(client);

    fillRectColor(dc,refresh,PANEL);

    drawTextSimple(
        dc,
        L"Refresh News",
        refresh,
        fontSmall_,
        TEXT,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );

    if (news_.empty())
    {
        RECT emptyCard{
            241,
            newsHeader.bottom + 12,
            client.right - 30,
            newsHeader.bottom + 198
        };

        fillRectColor(dc,emptyCard,PANEL);

        drawTextSimple(
            dc,
            L"No news to display",
            RECT{
                emptyCard.left + 24,
                emptyCard.top + 32,
                emptyCard.right - 24,
                emptyCard.top + 62
            },
            fontNormal_,
            TEXT,
            DT_LEFT | DT_SINGLELINE
        );

        drawTextSimple(
            dc,
            newsStatus_,
            RECT{
                emptyCard.left + 24,
                emptyCard.top + 70,
                emptyCard.right - 24,
                emptyCard.bottom - 24
            },
            fontSmall_,
            MUTED,
            DT_LEFT | DT_WORDBREAK
        );
    }
    else
    {
        const int count =
            static_cast<int>(
                std::min<size_t>(
                    news_.size(),
                    3
                )
            );

        for (int i = 0; i < count; ++i)
        {
            const NewsItem& item =
                news_[i];

            const RECT card =
                newsCardRect(
                    client,
                    i
                );

            fillRectColor(dc,card,PANEL);
            fillRectColor(
                dc,
                RECT{
                    card.left,
                    card.top,
                    card.right,
                    card.top + 4
                },
                item.pinned ? ACCENT : CYAN
            );

            LONG textLeft =
                card.left + 16;

            if (!item.imageUrl.empty())
            {
                const std::wstring imageKey =
                    L"news:" + item.imageUrl;

                auto it =
                    imageCache_.find(imageKey);

                if (
                    it != imageCache_.end() &&
                    it->second
                )
                {
                    RECT thumb{
                        card.left + 16,
                        card.top + 18,
                        card.left + 104,
                        card.top + 106
                    };

                    drawBitmapFit(dc,it->second,thumb);
                    textLeft = thumb.right + 14;
                }
            }

            drawTextSimple(
                dc,
                item.pinned
                    ? L"PINNED • " + item.category
                    : item.category,
                RECT{
                    textLeft,
                    card.top + 16,
                    card.right - 16,
                    card.top + 37
                },
                fontMeta_,
                item.pinned ? ACCENT : CYAN,
                DT_LEFT |
                DT_SINGLELINE |
                DT_END_ELLIPSIS
            );

            drawTextSimple(
                dc,
                item.title,
                RECT{
                    textLeft,
                    card.top + 43,
                    card.right - 16,
                    card.top + 72
                },
                fontNormal_,
                TEXT,
                DT_LEFT |
                DT_SINGLELINE |
                DT_END_ELLIPSIS
            );

            drawTextSimple(
                dc,
                item.summary,
                RECT{
                    textLeft,
                    card.top + 80,
                    card.right - 16,
                    card.bottom - 42
                },
                fontSmall_,
                MUTED,
                DT_LEFT |
                DT_WORDBREAK |
                DT_END_ELLIPSIS
            );

            drawTextSimple(
                dc,
                formatNewsDate(item.published),
                RECT{
                    card.left + 16,
                    card.bottom - 31,
                    card.right - 105,
                    card.bottom - 10
                },
                fontSmall_,
                MUTED,
                DT_LEFT |
                DT_SINGLELINE |
                DT_END_ELLIPSIS
            );

            drawTextSimple(
                dc,
                L"Read Article  >",
                RECT{
                    card.right - 105,
                    card.bottom - 31,
                    card.right - 16,
                    card.bottom - 10
                },
                fontSmall_,
                CYAN,
                DT_RIGHT | DT_SINGLELINE
            );
        }
    }
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


void MainWindow::clearMedia()
{
    for (MediaInstance& instance : mediaInstances_)
    {
        for (MediaScreenshot& shot : instance.screenshots)
        {
            if (shot.bitmap)
            {
                DeleteObject(shot.bitmap);
                shot.bitmap = nullptr;
            }
        }
    }

    mediaInstances_.clear();
    mediaOpenInstance_ = -1;
    mediaOpenScreenshot_ = -1;
    mediaScrollY_ = 0;
    mediaContentHeight_ = 0;
}

std::wstring MainWindow::formatMediaTimestamp(
    const std::filesystem::file_time_type& time)
{
    try
    {
        const auto systemTime =
            std::chrono::time_point_cast<
                std::chrono::system_clock::duration
            >(
                time -
                std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now()
            );

        const std::time_t raw =
            std::chrono::system_clock::to_time_t(
                systemTime
            );

        std::tm local{};

        localtime_s(
            &local,
            &raw
        );

        std::wostringstream stream;
        stream <<
            std::put_time(
                &local,
                L"%I:%M %p %m/%d/%Y"
            );

        std::wstring result =
            stream.str();

        if (
            result.size() > 1 &&
            result[0] == L'0'
        )
        {
            result.erase(
                result.begin()
            );
        }

        return result;
    }
    catch (...)
    {
        return L"Unknown date";
    }
}

void MainWindow::refreshMedia()
{
    // Preserve collapsed/expanded choices across a refresh.
    std::unordered_map<std::wstring, bool>
        expanded;

    for (const MediaInstance& item : mediaInstances_)
        expanded[item.id] = item.expanded;

    clearMedia();

    for (const Modpack& pack : packs_)
    {
        const std::filesystem::path instanceRoot(
            InstallEngine::packInstanceRoot(
                settings_.installRoot,
                pack.id
            )
        );

        const std::filesystem::path screenshots =
            instanceRoot /
            L"screenshots";

        std::error_code error;

        if (
            !std::filesystem::exists(
                screenshots,
                error
            ) ||
            !std::filesystem::is_directory(
                screenshots,
                error
            )
        )
        {
            continue;
        }

        MediaInstance instance;
        instance.id = pack.id;
        instance.name =
            pack.name.empty()
                ? pack.id
                : pack.name;

        auto expandedIt =
            expanded.find(
                instance.id
            );

        instance.expanded =
            expandedIt != expanded.end()
                ? expandedIt->second
                : false;

        for (
            const auto& entry :
            std::filesystem::directory_iterator(
                screenshots,
                error
            )
        )
        {
            if (
                error ||
                !entry.is_regular_file()
            )
            {
                continue;
            }

            std::wstring extension =
                entry.path()
                    .extension()
                    .wstring();

            std::transform(
                extension.begin(),
                extension.end(),
                extension.begin(),
                [](wchar_t c)
                {
                    return
                        static_cast<wchar_t>(
                            towlower(c)
                        );
                }
            );

            if (
                extension != L".png" &&
                extension != L".jpg" &&
                extension != L".jpeg"
            )
            {
                continue;
            }

            MediaScreenshot shot;
            shot.path = entry.path();
            shot.modified =
                entry.last_write_time(
                    error
                );

            if (error)
            {
                error.clear();
                continue;
            }

            shot.timestamp =
                formatMediaTimestamp(
                    shot.modified
                );

            instance.screenshots.push_back(
                std::move(shot)
            );
        }

        if (instance.screenshots.empty())
            continue;

        std::sort(
            instance.screenshots.begin(),
            instance.screenshots.end(),
            [](
                const MediaScreenshot& a,
                const MediaScreenshot& b)
            {
                return
                    a.modified >
                    b.modified;
            }
        );

        mediaInstances_.push_back(
            std::move(instance)
        );
    }

    // Packs with the newest screenshot appear first.
    std::sort(
        mediaInstances_.begin(),
        mediaInstances_.end(),
        [](
            const MediaInstance& a,
            const MediaInstance& b)
        {
            return
                a.screenshots.front().modified >
                b.screenshots.front().modified;
        }
    );
}

RECT MainWindow::mediaRefreshRect(
    const RECT& client) const
{
    return RECT{
        client.right - 154,
        34,
        client.right - 34,
        74
    };
}

int MainWindow::mediaMaxScroll(
    const RECT& client) const
{
    return
        std::max(
            0,
            mediaContentHeight_ -
            static_cast<int>(
                client.bottom - 92
            )
        );
}

void MainWindow::setMediaScroll(
    int value,
    const RECT& client)
{
    mediaScrollY_ =
        std::clamp(
            value,
            0,
            mediaMaxScroll(client)
        );
}

RECT MainWindow::mediaModalRect(
    const RECT& client) const
{
    const LONG width =
        std::max<LONG>(
            520L,
            std::min<LONG>(
                1050L,
                client.right - 300
            )
        );

    const LONG height =
        std::max<LONG>(
            420L,
            std::min<LONG>(
                760L,
                client.bottom - 80
            )
        );

    const LONG left =
        220 +
        std::max<LONG>(
            24L,
            (
                client.right -
                220 -
                width
            ) / 2
        );

    const LONG top =
        std::max<LONG>(
            24L,
            (
                client.bottom -
                height
            ) / 2
        );

    return RECT{
        left,
        top,
        left + width,
        top + height
    };
}

RECT MainWindow::mediaModalCloseRect(
    const RECT& client) const
{
    const RECT modal =
        mediaModalRect(client);

    return RECT{
        modal.right - 112,
        modal.top + 16,
        modal.right - 24,
        modal.top + 50
    };
}

void MainWindow::paintMedia(
    HDC dc,
    const RECT& client)
{
    drawTextSimple(
        dc,
        L"MEDIA",
        RECT{
            250,
            30,
            client.right - 180,
            65
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        L"Browse screenshots captured in your modpack instances.",
        RECT{
            250,
            65,
            client.right - 190,
            88
        },
        fontSmall_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    const RECT refresh =
        mediaRefreshRect(client);

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
        CYAN,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    const int saved =
        SaveDC(dc);

    IntersectClipRect(
        dc,
        220,
        96,
        client.right,
        client.bottom
    );

    OffsetViewportOrgEx(
        dc,
        0,
        -mediaScrollY_,
        nullptr
    );

    LONG y = 110;

    if (mediaInstances_.empty())
    {
        drawTextSimple(
            dc,
            L"No screenshots found yet.",
            RECT{
                250,
                y + 20,
                client.right - 40,
                y + 52
            },
            fontNormal_,
            MUTED,
            DT_LEFT |
            DT_SINGLELINE
        );

        drawTextSimple(
            dc,
            L"Minecraft screenshots will appear here after they are saved in an installed pack's screenshots folder.",
            RECT{
                250,
                y + 58,
                client.right - 60,
                y + 110
            },
            fontSmall_,
            MUTED,
            DT_LEFT |
            DT_WORDBREAK
        );

        mediaContentHeight_ = 210;
    }
    else
    {
        const LONG left = 250;
        const LONG right =
            client.right - 38;

        const LONG gap = 16;
        const LONG minimumCardWidth = 220;

        const LONG available =
            std::max<LONG>(
                minimumCardWidth,
                right - left
            );

        int columns =
            static_cast<int>(
                (
                    available + gap
                ) /
                (
                    minimumCardWidth + gap
                )
            );

        columns =
            std::clamp(
                columns,
                1,
                4
            );

        const LONG cardWidth =
            (
                available -
                gap * (columns - 1)
            ) /
            columns;

        const LONG imageHeight =
            std::max<LONG>(
                125L,
                cardWidth * 9 / 16
            );

        const LONG cardHeight =
            imageHeight + 38;

        for (MediaInstance& instance : mediaInstances_)
        {
            instance.headerRect =
                RECT{
                    left,
                    y,
                    right,
                    y + 52
                };

            fillRectColor(
                dc,
                instance.headerRect,
                CARD
            );

            fillRectColor(
                dc,
                RECT{
                    left,
                    y,
                    left + 4,
                    y + 52
                },
                instance.expanded
                    ? ACCENT
                    : BORDER
            );

            drawTextSimple(
                dc,
                instance.expanded
                    ? L"▼"
                    : L"▶",
                RECT{
                    left + 16,
                    y,
                    left + 42,
                    y + 52
                },
                fontNormal_,
                CYAN,
                DT_LEFT |
                DT_VCENTER |
                DT_SINGLELINE
            );

            drawTextSimple(
                dc,
                instance.name,
                RECT{
                    left + 46,
                    y,
                    right - 180,
                    y + 52
                },
                fontNormal_,
                TEXT,
                DT_LEFT |
                DT_VCENTER |
                DT_SINGLELINE |
                DT_END_ELLIPSIS
            );

            const std::wstring count =
                std::to_wstring(
                    instance.screenshots.size()
                ) +
                (
                    instance.screenshots.size() == 1
                        ? L" screenshot"
                        : L" screenshots"
                );

            drawTextSimple(
                dc,
                count,
                RECT{
                    right - 170,
                    y,
                    right - 18,
                    y + 52
                },
                fontSmall_,
                MUTED,
                DT_RIGHT |
                DT_VCENTER |
                DT_SINGLELINE
            );

            y += 66;

            if (!instance.expanded)
                continue;

            for (
                size_t i = 0;
                i < instance.screenshots.size();
                ++i
            )
            {
                MediaScreenshot& shot =
                    instance.screenshots[i];

                const int column =
                    static_cast<int>(
                        i % columns
                    );

                const int row =
                    static_cast<int>(
                        i / columns
                    );

                const LONG x =
                    left +
                    column *
                    (
                        cardWidth + gap
                    );

                const LONG cardTop =
                    y +
                    row *
                    (
                        cardHeight + gap
                    );

                shot.cardRect =
                    RECT{
                        x,
                        cardTop,
                        x + cardWidth,
                        cardTop + cardHeight
                    };

                fillRectColor(
                    dc,
                    shot.cardRect,
                    CARD
                );

                const RECT imageRect{
                    x + 6,
                    cardTop + 6,
                    x + cardWidth - 6,
                    cardTop + imageHeight - 2
                };

                fillRectColor(
                    dc,
                    imageRect,
                    BG
                );

                if (!shot.bitmap)
                {
                    shot.bitmap =
                        ImageLoader::
                            loadFromFilePreserveAspect(
                                shot.path.wstring(),
                                640,
                                360
                            );
                }

                if (shot.bitmap)
                {
                    drawBitmapFit(
                        dc,
                        shot.bitmap,
                        imageRect
                    );
                }

                drawTextSimple(
                    dc,
                    shot.timestamp,
                    RECT{
                        x + 10,
                        cardTop + imageHeight + 2,
                        x + cardWidth - 10,
                        cardTop + cardHeight - 4
                    },
                    fontMeta_,
                    MUTED,
                    DT_RIGHT |
                    DT_VCENTER |
                    DT_SINGLELINE
                );
            }

            const size_t rows =
                (
                    instance.screenshots.size() +
                    static_cast<size_t>(
                        columns
                    ) -
                    1
                ) /
                static_cast<size_t>(
                    columns
                );

            y +=
                static_cast<LONG>(
                    rows
                ) *
                (
                    cardHeight + gap
                ) +
                8;
        }

        mediaContentHeight_ =
            static_cast<int>(
                y + 30
            );
    }

    RestoreDC(
        dc,
        saved
    );

    setMediaScroll(
        mediaScrollY_,
        client
    );

    if (mediaMaxScroll(client) > 0)
    {
        const LONG trackTop = 106;
        const LONG trackBottom =
            client.bottom - 18;

        fillRectColor(
            dc,
            RECT{
                client.right - 14,
                trackTop,
                client.right - 8,
                trackBottom
            },
            BORDER
        );

        const LONG trackHeight =
            trackBottom - trackTop;

        const LONG visibleHeight =
            std::max<LONG>(
                1L,
                client.bottom - 106
            );

        LONG thumbHeight =
            std::max<LONG>(
                40L,
                trackHeight *
                visibleHeight /
                std::max<LONG>(
                    visibleHeight,
                    mediaContentHeight_
                )
            );

        const LONG usable =
            std::max<LONG>(
                1L,
                trackHeight -
                thumbHeight
            );

        const LONG thumbTop =
            trackTop +
            usable *
            mediaScrollY_ /
            std::max(
                1,
                mediaMaxScroll(client)
            );

        fillRectColor(
            dc,
            RECT{
                client.right - 15,
                thumbTop,
                client.right - 7,
                thumbTop + thumbHeight
            },
            CYAN
        );
    }
}

void MainWindow::paintMediaModal(
    HDC dc,
    const RECT& client)
{
    if (
        mediaOpenInstance_ < 0 ||
        mediaOpenInstance_ >=
            static_cast<int>(
                mediaInstances_.size()
            )
    )
    {
        return;
    }

    MediaInstance& instance =
        mediaInstances_[
            mediaOpenInstance_
        ];

    if (
        mediaOpenScreenshot_ < 0 ||
        mediaOpenScreenshot_ >=
            static_cast<int>(
                instance.screenshots.size()
            )
    )
    {
        return;
    }

    MediaScreenshot& shot =
        instance.screenshots[
            mediaOpenScreenshot_
        ];

    // Opaque dark overlay consistent with the launcher's other custom modals.
    fillRectColor(
        dc,
        RECT{
            220,
            0,
            client.right,
            client.bottom
        },
        RGB(3, 7, 18)
    );

    const RECT modal =
        mediaModalRect(client);

    fillRectColor(
        dc,
        modal,
        CARD
    );

    fillRectColor(
        dc,
        RECT{
            modal.left,
            modal.top,
            modal.left + 5,
            modal.bottom
        },
        ACCENT
    );

    drawTextSimple(
        dc,
        instance.name,
        RECT{
            modal.left + 28,
            modal.top + 16,
            modal.right - 130,
            modal.top + 50
        },
        fontTitle_,
        TEXT,
        DT_LEFT |
        DT_VCENTER |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    const RECT close =
        mediaModalCloseRect(client);

    fillRectColor(
        dc,
        close,
        PANEL
    );

    drawTextSimple(
        dc,
        L"Close",
        close,
        fontNormal_,
        CYAN,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    const RECT imageArea{
        modal.left + 28,
        modal.top + 70,
        modal.right - 28,
        modal.bottom - 58
    };

    fillRectColor(
        dc,
        imageArea,
        BG
    );

    if (!shot.bitmap)
    {
        shot.bitmap =
            ImageLoader::
                loadFromFilePreserveAspect(
                    shot.path.wstring(),
                    1600,
                    1000
                );
    }

    if (shot.bitmap)
    {
        drawBitmapFit(
            dc,
            shot.bitmap,
            imageArea
        );
    }

    drawTextSimple(
        dc,
        shot.timestamp,
        RECT{
            modal.left + 28,
            modal.bottom - 48,
            modal.right - 28,
            modal.bottom - 18
        },
        fontSmall_,
        MUTED,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );
}



int MainWindow::settingsViewportHeight(
    const RECT& client) const
{
    return std::max<int>(
        120,
        static_cast<int>(client.bottom) - 8
    );
}

int MainWindow::settingsMaxScroll(
    const RECT& client) const
{
    return std::max(
        0,
        settingsContentHeight_ -
        settingsViewportHeight(client)
    );
}

RECT MainWindow::settingsScrollbarTrackRect(
    const RECT& client) const
{
    return RECT{
        client.right - 10,
        104,
        client.right - 3,
        client.bottom - 8
    };
}

RECT MainWindow::settingsScrollbarThumbRect(
    const RECT& client) const
{
    RECT track =
        settingsScrollbarTrackRect(client);

    const int trackHeight =
        static_cast<int>(
            track.bottom - track.top
        );

    if (
        trackHeight <= 0 ||
        settingsMaxScroll(client) <= 0
    )
    {
        return track;
    }

    const int viewport =
        settingsViewportHeight(client);

    const int thumbHeight =
        std::max(
            42,
            static_cast<int>(
                (static_cast<long long>(trackHeight) *
                 viewport) /
                settingsContentHeight_
            )
        );

    const int travel =
        std::max(
            1,
            trackHeight - thumbHeight
        );

    const int top =
        track.top +
        static_cast<int>(
            (static_cast<long long>(travel) *
             settingsScrollY_) /
            settingsMaxScroll(client)
        );

    return RECT{
        track.left,
        top,
        track.right,
        top + thumbHeight
    };
}

void MainWindow::clampSettingsScroll(
    const RECT& client)
{
    settingsScrollY_ =
        std::clamp(
            settingsScrollY_,
            0,
            settingsMaxScroll(client)
        );
}

RECT MainWindow::accountLogoutRect(
    const RECT& client) const
{
    return RECT{
        client.right - 170,
        746,
        client.right - 48,
        788
    };
}

RECT MainWindow::updateCheckRect(
    const RECT& client) const
{
    return RECT{
        client.right - 310,
        536,
        client.right - 165,
        578
    };
}

RECT MainWindow::updateNowRect(
    const RECT& client) const
{
    return RECT{
        client.right - 150,
        536,
        client.right - 48,
        578
    };
}

bool MainWindow::isVersionNewer(
    const std::wstring& candidate,
    const std::wstring& current)
{
    auto parse =
        [](std::wstring value)
        {
            if (
                !value.empty() &&
                (
                    value[0] == L'v' ||
                    value[0] == L'V'
                )
            )
            {
                value.erase(
                    value.begin()
                );
            }

            std::vector<int> parts;
            std::wstring number;

            for (wchar_t c : value)
            {
                if (
                    c >= L'0' &&
                    c <= L'9'
                )
                {
                    number += c;
                }
                else if (c == L'.')
                {
                    parts.push_back(
                        number.empty()
                            ? 0
                            : std::stoi(number)
                    );
                    number.clear();
                }
                else
                {
                    // Ignore suffixes such as "-beta" for this simple release
                    // comparison after the numeric version has begun.
                    break;
                }
            }

            parts.push_back(
                number.empty()
                    ? 0
                    : std::stoi(number)
            );

            return parts;
        };

    try
    {
        std::vector<int> a =
            parse(candidate);

        std::vector<int> b =
            parse(current);

        const size_t count =
            std::max(
                a.size(),
                b.size()
            );

        a.resize(count, 0);
        b.resize(count, 0);

        for (size_t i = 0; i < count; ++i)
        {
            if (a[i] > b[i])
                return true;

            if (a[i] < b[i])
                return false;
        }
    }
    catch (...)
    {
        return false;
    }

    return false;
}

void MainWindow::checkForUpdates(
    bool manual)
{
    if (updateCheckRunning_)
        return;

    updateCheckRunning_ = true;

    if (manual)
    {
        updateStatus_ =
            L"Checking for launcher updates...";

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );
    }

    std::thread(
        [this, manual]()
        {
            try
            {
                /*
                    Keep this endpoint deliberately simple. The PHP file can
                    later generate the same JSON dynamically without requiring
                    any launcher-side changes.
                */
                const std::wstring endpoint =
                    L"https://launcher.newttech.net/app/update.json";

                const std::string json =
                    HttpClient::getUtf8(
                        endpoint
                    );

                const JsonValue root =
                    JsonLite::parse(json);

                const std::wstring latest =
                    utf8ToWide(
                        root
                            .get("latestVersion")
                            .asString()
                    );

                const std::wstring title =
                    utf8ToWide(
                        root
                            .get("title")
                            .asString()
                    );

                const std::wstring notes =
                    utf8ToWide(
                        root
                            .get("notes")
                            .asString()
                    );

                if (latest.empty())
                    throw std::runtime_error(
                        "Update manifest is missing latestVersion."
                    );

                updateLatestVersion_ = latest;
                updateTitle_ = title;
                updateNotes_ = notes;
                updateCheckCompleted_ = true;

                updateAvailable_ =
                    isVersionNewer(
                        latest,
                        LAUNCHER_VERSION
                    );

                if (updateAvailable_)
                {
                    updateStatus_ =
                        !title.empty()
                            ? title
                            : L"NewtTech Launcher v" +
                              latest +
                              L" is available.";

                    if (!notes.empty())
                    {
                        updateStatus_ +=
                            L"\n" +
                            notes;
                    }
                }
                else
                {
                    updateStatus_ =
                        L"You're up to date.";
                }
            }
            catch (const std::exception& error)
            {
                updateCheckCompleted_ = true;
                updateAvailable_ = false;
                updateLatestVersion_.clear();

                /*
                    Startup checks fail quietly from the user's perspective.
                    Manual checks expose a useful error message.
                */
                if (manual)
                {
                    updateStatus_ =
                        L"Unable to check for updates: " +
                        utf8ToWide(
                            error.what()
                        );
                }
                else
                {
                    updateStatus_ =
                        L"Updates could not be checked automatically.";
                }
            }

            if (hwnd_)
            {
                PostMessageW(
                    hwnd_,
                    WM_UPDATE_CHECK_DONE,
                    0,
                    0
                );
            }
        }
    ).detach();
}

void MainWindow::launchUpdater()
{
    wchar_t localAppData[MAX_PATH]{};

    const DWORD length =
        GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            localAppData,
            MAX_PATH
        );

    if (
        length == 0 ||
        length >= MAX_PATH
    )
    {
        updateStatus_ =
            L"Unable to locate your Local AppData folder.";

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );

        return;
    }

    const std::filesystem::path installerPath =
        std::filesystem::path(
            localAppData
        ) /
        L"Programs" /
        L"NewtTech Launcher" /
        L"NewtTechInstaller.exe";

    std::error_code error;

    if (
        !std::filesystem::exists(
            installerPath,
            error
        ) ||
        !std::filesystem::is_regular_file(
            installerPath,
            error
        )
    )
    {
        updateStatus_ =
            L"NewtTechInstaller.exe is not installed. "
            L"Run the latest NewtTech installer once to add the local updater.";

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );

        return;
    }

    /*
        The installer is deliberately stored beside NewtTechLauncher.exe.
        Update Now launches that local executable directly; no browser or
        download URL is involved.
    */
    const HINSTANCE result =
        ShellExecuteW(
            hwnd_,
            L"open",
            installerPath.c_str(),
            nullptr,
            installerPath.parent_path().c_str(),
            SW_SHOWNORMAL
        );

    if (
        reinterpret_cast<INT_PTR>(
            result
        ) <= 32
    )
    {
        updateStatus_ =
            L"Unable to start NewtTechInstaller.exe.";

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE
        );

        return;
    }

    // Only close after Windows successfully accepts the installer launch.
    SendMessageW(
        hwnd_,
        WM_CLOSE,
        0,
        0
    );
}

void MainWindow::paintSettings(
    HDC dc,
    const RECT& client)
{
    // Keep the Settings heading fixed. Only the card/content region below it
    // scrolls, and clip that region so scrolled cards can never paint over
    // the heading/subtitle.
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

    const int savedDc = SaveDC(dc);

    IntersectClipRect(
        dc,
        220,
        100,
        client.right,
        client.bottom
    );

    OffsetViewportOrgEx(
        dc,
        0,
        -settingsScrollY_,
        nullptr
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


    RECT updates{
        241,
        492,
        client.right - 30,
        686
    };

    fillRectColor(
        dc,
        updates,
        PANEL
    );

    drawTextSimple(
        dc,
        L"LAUNCHER UPDATES",
        RECT{
            270,
            514,
            client.right - 60,
            536
        },
        fontMeta_,
        ACCENT,
        DT_LEFT |
        DT_SINGLELINE
    );

    const std::wstring currentText =
        L"Current version: v" +
        std::wstring(
            LAUNCHER_VERSION
        );

    drawTextSimple(
        dc,
        currentText,
        RECT{
            270,
            548,
            client.right - 340,
            572
        },
        fontNormal_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE
    );

    std::wstring latestText =
        L"Latest version: ";

    latestText +=
        updateLatestVersion_.empty()
            ? L"—"
            : L"v" + updateLatestVersion_;

    drawTextSimple(
        dc,
        latestText,
        RECT{
            270,
            574,
            client.right - 340,
            598
        },
        fontSmall_,
        MUTED,
        DT_LEFT |
        DT_SINGLELINE
    );

    drawTextSimple(
        dc,
        updateStatus_,
        RECT{
            270,
            606,
            client.right - 330,
            666
        },
        fontSmall_,
        updateAvailable_
            ? SUCCESS
            : MUTED,
        DT_LEFT |
        DT_WORDBREAK |
        DT_END_ELLIPSIS
    );

    const RECT check =
        updateCheckRect(client);

    fillRectColor(
        dc,
        check,
        CARD
    );

    drawTextSimple(
        dc,
        updateCheckRunning_
            ? L"Checking..."
            : L"Check for Updates",
        check,
        fontNormal_,
        updateCheckRunning_
            ? MUTED
            : TEXT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    if (updateAvailable_)
    {
        const RECT update =
            updateNowRect(client);

        fillRectColor(
            dc,
            update,
            ACCENT
        );

        drawTextSimple(
            dc,
            L"Update Now",
            update,
            fontNormal_,
            TEXT_DARK,
            DT_CENTER |
            DT_VCENTER |
            DT_SINGLELINE
        );
    }


    RECT account{
        241,
        712,
        client.right - 30,
        824
    };

    fillRectColor(
        dc,
        account,
        PANEL
    );

    drawTextSimple(
        dc,
        L"ACCOUNT",
        RECT{
            270,
            734,
            client.right - 210,
            756
        },
        fontMeta_,
        CYAN,
        DT_LEFT |
        DT_SINGLELINE
    );

    const std::wstring accountName =
        authUser_.name.empty()
            ? authUser_.username
            : authUser_.name;

    drawTextSimple(
        dc,
        accountName,
        RECT{
            270,
            763,
            client.right - 220,
            787
        },
        fontNormal_,
        TEXT,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    const std::wstring accountDetails =
        L"@" +
        authUser_.username +
        L"   •   Minecraft: " +
        authUser_.minecraftUsername +
        L"   •   Signed in";

    drawTextSimple(
        dc,
        accountDetails,
        RECT{
            270,
            790,
            client.right - 220,
            812
        },
        fontSmall_,
        SUCCESS,
        DT_LEFT |
        DT_SINGLELINE |
        DT_END_ELLIPSIS
    );

    const RECT logout =
        accountLogoutRect(client);

    fillRectColor(
        dc,
        logout,
        CARD
    );

    drawTextSimple(
        dc,
        L"Sign Out",
        logout,
        fontNormal_,
        ACCENT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    RestoreDC(
        dc,
        savedDc
    );

    if (settingsMaxScroll(client) > 0)
    {
        const RECT track =
            settingsScrollbarTrackRect(client);

        const RECT thumb =
            settingsScrollbarThumbRect(client);

        fillRectColor(
            dc,
            track,
            RGB(10, 28, 49)
        );

        fillRectColor(
            dc,
            thumb,
            RGB(35, 76, 112)
        );
    }
}

void MainWindow::drawBitmapCover(
    HDC dc,
    HBITMAP bitmap,
    const RECT& target)
{
    if (!bitmap)
        return;

    BITMAP bm{};

    if (
        GetObject(
            bitmap,
            sizeof(bm),
            &bm
        ) == 0
    )
    {
        return;
    }

    HDC source =
        CreateCompatibleDC(dc);

    if (!source)
        return;

    HBITMAP old =
        static_cast<HBITMAP>(
            SelectObject(
                source,
                bitmap
            )
        );

    /*
        ImageLoader stores artwork as 32bpp premultiplied BGRA.
        AlphaBlend composites transparent PNG pixels over whatever is
        already painted in the launcher instead of copying the RGB
        values of transparent pixels as an opaque black rectangle.
    */
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    SetStretchBltMode(
        dc,
        HALFTONE
    );

    AlphaBlend(
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
        blend
    );

    SelectObject(
        source,
        old
    );

    DeleteDC(source);
}

void MainWindow::drawBitmapFit(
    HDC dc,
    HBITMAP bitmap,
    const RECT& target)
{
    if (!bitmap)
        return;

    BITMAP bm{};

    if (
        GetObject(
            bitmap,
            sizeof(bm),
            &bm
        ) == 0 ||
        bm.bmWidth <= 0 ||
        bm.bmHeight <= 0
    )
    {
        return;
    }

    const LONG boxWidth =
        target.right -
        target.left;

    const LONG boxHeight =
        target.bottom -
        target.top;

    if (
        boxWidth <= 0 ||
        boxHeight <= 0
    )
    {
        return;
    }

    /*
        TRUE ASPECT-CONTAIN.

        The HBITMAP already retains the source image's real proportions.
        We calculate one uniform scale factor and apply it to BOTH axes.

        No cropping.
        No forced width.
        No forced height.
        No aspect-ratio changes.
    */
    const double scaleX =
        static_cast<double>(boxWidth) /
        static_cast<double>(bm.bmWidth);

    const double scaleY =
        static_cast<double>(boxHeight) /
        static_cast<double>(bm.bmHeight);

    const double scale =
        scaleX < scaleY
            ? scaleX
            : scaleY;

    LONG drawWidth =
        static_cast<LONG>(
            static_cast<double>(bm.bmWidth) *
            scale
        );

    LONG drawHeight =
        static_cast<LONG>(
            static_cast<double>(bm.bmHeight) *
            scale
        );

    drawWidth =
        std::max<LONG>(
            1L,
            drawWidth
        );

    drawHeight =
        std::max<LONG>(
            1L,
            drawHeight
        );

    const LONG drawX =
        target.left +
        (
            boxWidth -
            drawWidth
        ) / 2;

    const LONG drawY =
        target.top +
        (
            boxHeight -
            drawHeight
        ) / 2;

    HDC source =
        CreateCompatibleDC(dc);

    if (!source)
        return;

    HBITMAP old =
        static_cast<HBITMAP>(
            SelectObject(
                source,
                bitmap
            )
        );

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    SetStretchBltMode(
        dc,
        HALFTONE
    );

    AlphaBlend(
        dc,
        drawX,
        drawY,
        drawWidth,
        drawHeight,
        source,
        0,
        0,
        bm.bmWidth,
        bm.bmHeight,
        blend
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

void MainWindow::ensureNewsArtwork()
{
    // Remove only old news artwork. Pack artwork remains cached.
    for (auto it = imageCache_.begin();
         it != imageCache_.end();)
    {
        if (
            it->first.rfind(
                L"news:",
                0
            ) == 0
        )
        {
            if (it->second)
                DeleteObject(
                    it->second
                );

            it =
                imageCache_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    const size_t count =
        std::min<size_t>(
            news_.size(),
            3
        );

    for (size_t i = 0; i < count; ++i)
    {
        const NewsItem& item =
            news_[i];

        if (item.imageUrl.empty())
            continue;

        const std::wstring key =
            L"news:" +
            item.imageUrl;

        HBITMAP bitmap =
            ImageLoader::loadFromUrlPreserveAspect(
                item.imageUrl,
                1200,
                900
            );

        if (bitmap)
        {
            imageCache_[key] =
                bitmap;
        }
    }
}

void MainWindow::refreshNews()
{
    newsStatus_ =
        L"Loading news...";

    openNewsIndex_ = -1;
    newsModalScrollY_ = 0;
    newsModalMaxScroll_ = 0;

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );

    UpdateWindow(
        hwnd_
    );

    try
    {
        news_ =
            NewsManager::fetch();

        if (news_.empty())
        {
            newsStatus_ =
                L"No news posts are currently published.";
        }
        else
        {
            newsStatus_ =
                std::to_wstring(
                    news_.size()
                ) +
                (
                    news_.size() == 1
                        ? L" news post available."
                        : L" news posts available."
                );
        }

        ensureNewsArtwork();
    }
    catch (const std::exception& error)
    {
        news_.clear();

        newsStatus_ =
            L"Unable to load news from the server.";

        OutputDebugStringA(
            (
                std::string(
                    "[NewtTech] News load failed: "
                ) +
                error.what() +
                "\n"
            ).c_str()
        );
    }

    InvalidateRect(
        hwnd_,
        nullptr,
        FALSE
    );
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
    homeCarouselOffset_ = 0;

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

        refreshNews();

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


bool MainWindow::shouldWarnAboutMemory() const
{
    int recommendedMb =
        currentManifest_.java.recommendedMemory;

    /*
        Pack manifests created by the current admin panel store the human
        recommendation as GB (for example 12), while older/alternate manifests
        may store it as MB (for example 12288).

        Launcher Settings always stores memoryMb in MB, so normalize the
        manifest value before comparing them.
    */
    if (
        recommendedMb > 0 &&
        recommendedMb <= 64
    )
    {
        recommendedMb *= 1024;
    }

    return
        recommendedMb > 0 &&
        settings_.memoryMb < recommendedMb;
}

void MainWindow::launchCurrentPack()
{
    try
    {
        /*
            Keep the managed multiplayer server synchronized at Play time too.
            This means a deleted/missing servers.dat is restored simply by
            pressing Play; Install or Verify/Repair is not required.
        */
        if (!currentManifest_.server.address.empty())
        {
            const std::filesystem::path instancePath(
                InstallEngine::packInstanceRoot(
                    settings_.installRoot,
                    currentManifest_.id
                )
            );

            ServersDat::writeManagedServer(
                instancePath,
                currentManifest_.name,
                currentManifest_.server.address
            );
        }

        setStatus(
            L"Preparing Minecraft " +
            currentManifest_.minecraft.version +
            L" + " +
            currentManifest_.minecraft.loader +
            L" " +
            currentManifest_.minecraft.loaderVersion +
            L"..."
        );

        const VersionPackageInfo version =
            VersionManager::ensurePackRuntime(
                currentManifest_
            );

        const std::wstring iconUrl =
            (
                selectedPack_ >= 0 &&
                selectedPack_ <
                    static_cast<int>(
                        packs_.size()
                    )
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
            return;
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

RECT MainWindow::lowMemoryModalRect(
    const RECT& client) const
{
    const LONG availableWidth =
        std::max<LONG>(
            360L,
            client.right - 220
        );

    const LONG width =
        std::min<LONG>(
            620L,
            availableWidth - 48
        );

    const LONG height = 360;

    const LONG left =
        220 +
        std::max<LONG>(
            24L,
            (
                availableWidth -
                width
            ) / 2
        );

    const LONG top =
        std::max<LONG>(
            30L,
            (
                client.bottom -
                height
            ) / 2
        );

    return RECT{
        left,
        top,
        left + width,
        top + height
    };
}

RECT MainWindow::lowMemoryProceedRect(
    const RECT& client) const
{
    const RECT modal =
        lowMemoryModalRect(client);

    return RECT{
        modal.left + 28,
        modal.bottom - 70,
        modal.left + 238,
        modal.bottom - 26
    };
}

RECT MainWindow::lowMemoryChangeRect(
    const RECT& client) const
{
    const RECT modal =
        lowMemoryModalRect(client);

    return RECT{
        modal.right - 238,
        modal.bottom - 70,
        modal.right - 28,
        modal.bottom - 26
    };
}

void MainWindow::paintLowMemoryWarning(
    HDC dc,
    const RECT& client)
{
    // Darken the page behind the warning.
    fillRectColor(
        dc,
        RECT{
            220,
            0,
            client.right,
            client.bottom
        },
        RGB(3, 7, 18)
    );

    const RECT modal =
        lowMemoryModalRect(client);

    fillRectColor(
        dc,
        modal,
        CARD
    );

    // NewtTech accent rail.
    fillRectColor(
        dc,
        RECT{
            modal.left,
            modal.top,
            modal.left + 5,
            modal.bottom
        },
        ACCENT
    );

    // Header separator.
    fillRectColor(
        dc,
        RECT{
            modal.left + 5,
            modal.top + 68,
            modal.right,
            modal.top + 69
        },
        BORDER
    );

    drawTextSimple(
        dc,
        L"LOW MEMORY ALLOCATION",
        RECT{
            modal.left + 28,
            modal.top + 20,
            modal.right - 28,
            modal.top + 54
        },
        fontTitle_,
        ACCENT,
        DT_LEFT |
        DT_VCENTER |
        DT_SINGLELINE
    );

    int recommendedMb =
        currentManifest_.java.recommendedMemory;

    if (
        recommendedMb > 0 &&
        recommendedMb <= 64
    )
    {
        recommendedMb *= 1024;
    }

    const int currentMb =
        settings_.memoryMb;

    const double recommendedGb =
        static_cast<double>(
            recommendedMb
        ) / 1024.0;

    const double currentGb =
        static_cast<double>(
            currentMb
        ) / 1024.0;

    auto memoryLabel =
        [](double gb) -> std::wstring
        {
            const int whole =
                static_cast<int>(gb);

            if (
                gb ==
                static_cast<double>(whole)
            )
            {
                return
                    std::to_wstring(whole) +
                    L" GB";
            }

            wchar_t buffer[32]{};

            swprintf(
                buffer,
                32,
                L"%.1f GB",
                gb
            );

            return buffer;
        };

    const std::wstring packName =
        currentManifest_.name.empty()
            ? L"This pack"
            : currentManifest_.name;

    const std::wstring message =
        packName +
        L" recommends " +
        memoryLabel(recommendedGb) +
        L" of memory.\n\n"
        L"You currently have " +
        memoryLabel(currentGb) +
        L" allocated.\n\n"
        L"This pack cannot guarantee stable frame rates, smooth chunk loading, "
        L"or successful game launch when using less than the recommended "
        L"memory allocation.";

    drawTextSimple(
        dc,
        message,
        RECT{
            modal.left + 28,
            modal.top + 88,
            modal.right - 28,
            modal.bottom - 92
        },
        fontNormal_,
        TEXT,
        DT_LEFT |
        DT_WORDBREAK
    );

    const RECT proceed =
        lowMemoryProceedRect(client);

    const RECT change =
        lowMemoryChangeRect(client);

    fillRectColor(
        dc,
        proceed,
        PANEL
    );

    fillRectColor(
        dc,
        RECT{
            proceed.left,
            proceed.bottom - 2,
            proceed.right,
            proceed.bottom
        },
        BORDER
    );

    drawTextSimple(
        dc,
        L"Proceed Anyway",
        proceed,
        fontNormal_,
        TEXT,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
    );

    fillRectColor(
        dc,
        change,
        ACCENT
    );

    drawTextSimple(
        dc,
        L"Change Now",
        change,
        fontNormal_,
        TEXT_DARK,
        DT_CENTER |
        DT_VCENTER |
        DT_SINGLELINE
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
            155,
            TITLEBAR_HEIGHT
        },
        fontSmall_,
        TEXT,
        DT_LEFT |
        DT_VCENTER |
        DT_SINGLELINE
    );

    const std::wstring versionTag =
        L"v" +
        std::wstring(
            LAUNCHER_VERSION
        );

    drawTextSimple(
        dc,
        versionTag,
        RECT{
            158,
            0,
            225,
            TITLEBAR_HEIGHT
        },
        fontMeta_,
        CYAN,
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
    /*
        DWMWA_WINDOW_CORNER_PREFERENCE = 33
        DWMWCP_DONOTROUND = 1
    */
    const DWORD cornerPreference = 1;

    DwmSetWindowAttribute(
        hwnd_,
        33,
        &cornerPreference,
        sizeof(cornerPreference)
    );

    /*
        DWMWA_BORDER_COLOR = 34
        DWMWA_COLOR_NONE = 0xFFFFFFFE

        This suppresses the Windows 11 DWM border while keeping DWM itself
        active for normal window management/resizing behavior.
    */
    const COLORREF borderColor =
        static_cast<COLORREF>(0xFFFFFFFEu);

    DwmSetWindowAttribute(
        hwnd_,
        34,
        &borderColor,
        sizeof(borderColor)
    );

    /*
        DWMWA_TRANSITIONS_FORCEDISABLED = 3
        Prevent activation/theme transitions from briefly animating the
        standard frame back over the custom NewtTech frame.
    */
    const BOOL transitionsDisabled = TRUE;

    DwmSetWindowAttribute(
        hwnd_,
        3,
        &transitionsDisabled,
        sizeof(transitionsDisabled)
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
    if (x < 7 || x > 205)
        return -1;

    for (int i = 0; i < 5; ++i)
    {
        RECT row{
            7,
            195 + i * 52,
            205,
            239 + i * 52
        };

        if (pointInRect(x,y,row))
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
