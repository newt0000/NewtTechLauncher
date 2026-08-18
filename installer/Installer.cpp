#include "InstallerConfig.h"

#include "HttpClient.h"
#include "JsonLite.h"

#include <windows.h>
#include <windowsx.h>
#include <bcrypt.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>



namespace
{
constexpr COLORREF BG       = RGB(5, 10, 24);
constexpr COLORREF PANEL    = RGB(8, 17, 38);
constexpr COLORREF CARD     = RGB(12, 25, 51);
constexpr COLORREF BORDER   = RGB(25, 52, 83);
constexpr COLORREF TEXT     = RGB(239, 247, 255);
constexpr COLORREF MUTED    = RGB(124, 151, 178);
constexpr COLORREF ACCENT   = RGB(255, 35, 180);
constexpr COLORREF CYAN     = RGB(0, 225, 255);
constexpr COLORREF DARKTEXT = RGB(5, 10, 24);

constexpr int TITLE_HEIGHT = 38;

constexpr UINT WM_INSTALL_PROGRESS = WM_APP + 1;
constexpr UINT WM_INSTALL_DONE     = WM_APP + 2;

struct InstallState
{
    bool active = false;
    bool complete = false;
    bool failed = false;
    int percent = 0;
    std::wstring status = L"Ready to install.";
    std::wstring version;
};

struct AppManifest
{
    std::wstring version;
    std::wstring url;
    std::wstring sha256;
};

HWND gWindow = nullptr;
HFONT gFont = nullptr;
HFONT gSmall = nullptr;
HFONT gTitle = nullptr;
InstallState gState;
std::mutex gMutex;
bool gDesktopShortcut = true;
bool gLaunchAfter = true;

void fill(HDC dc, RECT r, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void text(
    HDC dc,
    const std::wstring& value,
    RECT r,
    HFONT font,
    COLORREF color,
    UINT flags)
{
    HFONT old = static_cast<HFONT>(
        SelectObject(dc, font)
    );

    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);

    DrawTextW(
        dc,
        value.c_str(),
        static_cast<int>(value.size()),
        &r,
        flags
    );

    SelectObject(dc, old);
}

std::wstring utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};

    int count = MultiByteToWideChar(
        CP_UTF8, 0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr, 0
    );

    std::wstring result(count, L'\0');

    MultiByteToWideChar(
        CP_UTF8, 0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        count
    );

    return result;
}

std::wstring knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR path = nullptr;

    if (FAILED(
        SHGetKnownFolderPath(
            id, 0, nullptr, &path
        )
    ))
        return {};

    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

std::filesystem::path installDir()
{
    return std::filesystem::path(
        knownFolder(FOLDERID_LocalAppData)
    ) /
    L"Programs" /
    L"NewtTech Launcher";
}

std::filesystem::path launcherPath()
{
    return installDir() / L"NewtTechLauncher.exe";
}

std::filesystem::path uninstallerPath()
{
    return installDir() / L"Uninstall NewtTech Launcher.exe";
}

std::filesystem::path desktopShortcutPath()
{
    return std::filesystem::path(
        knownFolder(FOLDERID_Desktop)
    ) /
    L"NewtTech Launcher.lnk";
}

std::filesystem::path startMenuShortcutPath()
{
    return std::filesystem::path(
        knownFolder(FOLDERID_Programs)
    ) /
    L"NewtTech Launcher.lnk";
}

bool createShortcut(
    const std::filesystem::path& shortcut,
    const std::filesystem::path& target,
    const std::filesystem::path& workingDir,
    const std::wstring& description)
{
    IShellLinkW* link = nullptr;

    if (FAILED(
        CoCreateInstance(
            CLSID_ShellLink,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&link)
        )
    ))
        return false;

    link->SetPath(target.c_str());
    link->SetWorkingDirectory(workingDir.c_str());
    link->SetDescription(description.c_str());
    link->SetIconLocation(target.c_str(), 0);

    IPersistFile* persist = nullptr;

    bool ok = false;

    if (SUCCEEDED(
        link->QueryInterface(
            IID_PPV_ARGS(&persist)
        )
    ))
    {
        std::filesystem::create_directories(
            shortcut.parent_path()
        );

        ok = SUCCEEDED(
            persist->Save(
                shortcut.c_str(),
                TRUE
            )
        );

        persist->Release();
    }

    link->Release();

    return ok;
}

std::wstring sha256File(
    const std::filesystem::path& path)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    if (BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0) < 0)
        return {};

    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD resultLength = 0;

    BCryptGetProperty(
        algorithm,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength),
        sizeof(objectLength),
        &resultLength,
        0
    );

    BCryptGetProperty(
        algorithm,
        BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashLength),
        sizeof(hashLength),
        &resultLength,
        0
    );

    std::vector<UCHAR> object(objectLength);
    std::vector<UCHAR> digest(hashLength);

    if (BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            objectLength,
            nullptr, 0, 0) < 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    std::ifstream input(path, std::ios::binary);

    if (!input)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    std::vector<char> buffer(1024 * 1024);

    while (input)
    {
        input.read(
            buffer.data(),
            static_cast<std::streamsize>(
                buffer.size()
            )
        );

        auto count = input.gcount();

        if (count > 0)
        {
            if (BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(
                        buffer.data()
                    ),
                    static_cast<ULONG>(count),
                    0) < 0)
            {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(
                    algorithm, 0
                );
                return {};
            }
        }
    }

    if (BCryptFinishHash(
            hash,
            digest.data(),
            hashLength,
            0) < 0)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(
            algorithm, 0
        );
        return {};
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::wstringstream output;
    output << std::hex << std::setfill(L'0');

    for (UCHAR value : digest)
        output << std::setw(2)
               << static_cast<int>(value);

    return output.str();
}

AppManifest fetchManifest()
{
    JsonValue root = JsonLite::parse(
        HttpClient::getUtf8(
            InstallerConfig::MANIFEST_URL
        )
    );

    AppManifest result;

    result.version = utf8ToWide(
        root.get("version").asString()
    );

    result.url = utf8ToWide(
        root.get("url").asString()
    );

    result.sha256 = utf8ToWide(
        root.get("sha256").asString()
    );

    if (
        result.version.empty() ||
        result.url.empty() ||
        result.sha256.empty()
    )
        throw std::runtime_error(
            "Installer manifest is incomplete."
        );

    return result;
}

void writeUninstallRegistry(
    const AppManifest& manifest)
{
    HKEY key = nullptr;

    const wchar_t* path =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NewtTech Launcher";

    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            path,
            0,
            nullptr,
            0,
            KEY_WRITE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS)
        return;

    auto setString =
        [&](const wchar_t* name,
            const std::wstring& value)
        {
            RegSetValueExW(
                key,
                name,
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(
                    value.c_str()
                ),
                static_cast<DWORD>(
                    (value.size() + 1) *
                    sizeof(wchar_t)
                )
            );
        };

    setString(
        L"DisplayName",
        InstallerConfig::PRODUCT_NAME
    );

    setString(
        L"DisplayVersion",
        manifest.version
    );

    setString(
        L"Publisher",
        InstallerConfig::PUBLISHER
    );

    setString(
        L"InstallLocation",
        installDir().wstring()
    );

    setString(
        L"DisplayIcon",
        launcherPath().wstring()
    );

    setString(
        L"UninstallString",
        L"\"" +
        uninstallerPath().wstring() +
        L"\" --uninstall"
    );

    DWORD one = 1;

    RegSetValueExW(
        key,
        L"NoModify",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&one),
        sizeof(one)
    );

    RegSetValueExW(
        key,
        L"NoRepair",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&one),
        sizeof(one)
    );

    RegCloseKey(key);
}

void updateState(
    int percent,
    const std::wstring& status)
{
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gState.percent = percent;
        gState.status = status;
    }

    PostMessageW(
        gWindow,
        WM_INSTALL_PROGRESS,
        0,
        0
    );
}

void runInstall()
{
    try
    {
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gState.active = true;
            gState.failed = false;
            gState.complete = false;
        }

        updateState(
            2,
            L"Checking latest version..."
        );

        AppManifest manifest =
            fetchManifest();

        {
            std::lock_guard<std::mutex> lock(gMutex);
            gState.version = manifest.version;
        }

        std::filesystem::path temp =
            std::filesystem::temp_directory_path() /
            L"NewtTechLauncher-download.exe";

        updateState(
            8,
            L"Downloading NewtTech Launcher " +
            manifest.version +
            L"..."
        );

        HttpClient::downloadToFile(
            manifest.url,
            temp.wstring(),
            [](unsigned long long done,
               unsigned long long total)
            {
                if (total > 0)
                {
                    int percent =
                        8 +
                        static_cast<int>(
                            done * 70 / total
                        );

                    updateState(
                        std::min(percent, 78),
                        L"Downloading launcher..."
                    );
                }
            }
        );

        updateState(
            80,
            L"Verifying SHA-256..."
        );

        std::wstring actual =
            sha256File(temp);

        auto normalize =
            [](std::wstring value)
            {
                std::transform(
                    value.begin(),
                    value.end(),
                    value.begin(),
                    towlower
                );

                return value;
            };

        if (
            normalize(actual) !=
            normalize(manifest.sha256)
        )
            throw std::runtime_error(
                "Downloaded launcher failed SHA-256 verification."
            );

        updateState(
            86,
            L"Installing launcher..."
        );

        std::filesystem::create_directories(
            installDir()
        );

        std::filesystem::copy_file(
            temp,
            launcherPath(),
            std::filesystem::copy_options::overwrite_existing
        );

        wchar_t self[MAX_PATH]{};
        GetModuleFileNameW(
            nullptr,
            self,
            MAX_PATH
        );

        std::filesystem::copy_file(
            self,
            uninstallerPath(),
            std::filesystem::copy_options::overwrite_existing
        );

        updateState(
            91,
            L"Creating shortcuts..."
        );

        createShortcut(
            startMenuShortcutPath(),
            launcherPath(),
            installDir(),
            L"NewtTech Modpack Launcher"
        );

        if (gDesktopShortcut)
        {
            createShortcut(
                desktopShortcutPath(),
                launcherPath(),
                installDir(),
                L"NewtTech Modpack Launcher"
            );
        }

        writeUninstallRegistry(
            manifest
        );

        updateState(
            100,
            L"Installation complete."
        );

        {
            std::lock_guard<std::mutex> lock(gMutex);
            gState.active = false;
            gState.complete = true;
        }

        PostMessageW(
            gWindow,
            WM_INSTALL_DONE,
            0,
            0
        );
    }
    catch (const std::exception& error)
    {
        {
            std::lock_guard<std::mutex> lock(gMutex);

            gState.active = false;
            gState.failed = true;
            gState.complete = false;
            gState.status =
                L"Installation failed: " +
                utf8ToWide(error.what());
        }

        PostMessageW(
            gWindow,
            WM_INSTALL_DONE,
            0,
            0
        );
    }
}

void uninstall()
{
    if (MessageBoxW(
            nullptr,
            L"Remove NewtTech Launcher from this PC?\n\n"
            L"Your downloaded modpack instances will NOT be deleted.",
            L"Uninstall NewtTech Launcher",
            MB_YESNO | MB_ICONQUESTION
        ) != IDYES)
        return;

    std::error_code ec;

    std::filesystem::remove(
        desktopShortcutPath(),
        ec
    );

    std::filesystem::remove(
        startMenuShortcutPath(),
        ec
    );

    std::filesystem::remove(
        launcherPath(),
        ec
    );

    RegDeleteTreeW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NewtTech Launcher"
    );

    const std::wstring self =
        uninstallerPath().wstring();

    const std::wstring directory =
        installDir().wstring();

    std::wstring command =
        L"/C timeout /T 2 /NOBREAK >NUL & "
        L"del /F /Q \"" +
        self +
        L"\" & rmdir /Q \"" +
        directory +
        L"\"";

    ShellExecuteW(
        nullptr,
        L"open",
        L"cmd.exe",
        command.c_str(),
        nullptr,
        SW_HIDE
    );
}

RECT closeRect(RECT client)
{
    return RECT{
        client.right - 46,
        0,
        client.right,
        TITLE_HEIGHT
    };
}

RECT minRect(RECT client)
{
    return RECT{
        client.right - 92,
        0,
        client.right - 46,
        TITLE_HEIGHT
    };
}

RECT installButton()
{
    return RECT{
        458,
        382,
        650,
        430
    };
}

RECT desktopCheck()
{
    return RECT{
        70,
        315,
        330,
        342
    };
}

RECT launchCheck()
{
    return RECT{
        70,
        349,
        330,
        376
    };
}

bool contains(
    RECT r,
    int x,
    int y)
{
    POINT p{x,y};
    return PtInRect(&r, p) != 0;
}

void paintInstaller(HDC dc, RECT client)
{
    HDC mem =
        CreateCompatibleDC(dc);

    HBITMAP bmp =
        CreateCompatibleBitmap(
            dc,
            client.right,
            client.bottom
        );

    HBITMAP old =
        static_cast<HBITMAP>(
            SelectObject(mem, bmp)
        );

    fill(mem, client, BG);

    // title bar
    fill(
        mem,
        RECT{
            0,0,
            client.right,
            TITLE_HEIGHT
        },
        RGB(6,12,29)
    );

    fill(
        mem,
        RECT{
            0,
            TITLE_HEIGHT-1,
            client.right,
            TITLE_HEIGHT
        },
        BORDER
    );

    fill(
        mem,
        RECT{14,15,20,21},
        CYAN
    );

    text(
        mem,
        L"NewtTech Installer",
        RECT{29,0,260,TITLE_HEIGHT},
        gSmall,
        TEXT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE
    );

    text(
        mem,
        L"—",
        minRect(client),
        gFont,
        MUTED,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );

    fill(
        mem,
        closeRect(client),
        ACCENT
    );

    text(
        mem,
        L"×",
        closeRect(client),
        gFont,
        DARKTEXT,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );

    text(
        mem,
        L"Install NewtTech Launcher",
        RECT{70,82,650,126},
        gTitle,
        TEXT,
        DT_LEFT | DT_SINGLELINE
    );

    text(
        mem,
        L"A lightweight launcher for your managed Minecraft modpacks.",
        RECT{72,132,650,160},
        gFont,
        MUTED,
        DT_LEFT | DT_SINGLELINE
    );

    fill(
        mem,
        RECT{70,190,650,274},
        PANEL
    );

    text(
        mem,
        L"INSTALL LOCATION",
        RECT{88,205,630,225},
        gSmall,
        CYAN,
        DT_LEFT | DT_SINGLELINE
    );

    text(
        mem,
        installDir().wstring(),
        RECT{88,236,630,260},
        gFont,
        TEXT,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    // checkboxes
    fill(
        mem,
        RECT{72,319,88,335},
        gDesktopShortcut ? ACCENT : CARD
    );

    if (gDesktopShortcut)
    {
        text(
            mem,
            L"✓",
            RECT{70,314,90,338},
            gSmall,
            DARKTEXT,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );
    }

    text(
        mem,
        L"Create desktop shortcut",
        RECT{100,312,350,340},
        gFont,
        TEXT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE
    );

    fill(
        mem,
        RECT{72,353,88,369},
        gLaunchAfter ? CYAN : CARD
    );

    if (gLaunchAfter)
    {
        text(
            mem,
            L"✓",
            RECT{70,348,90,372},
            gSmall,
            DARKTEXT,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );
    }

    text(
        mem,
        L"Launch when finished",
        RECT{100,346,350,374},
        gFont,
        TEXT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE
    );

    InstallState state;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        state = gState;
    }

    RECT button =
        installButton();

    fill(
        mem,
        button,
        state.active ? CARD : ACCENT
    );

    text(
        mem,
        state.active
            ? L"Installing..."
            : (
                state.complete
                    ? L"Installed"
                    : L"Install"
            ),
        button,
        gFont,
        state.active ? MUTED : DARKTEXT,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );

    text(
        mem,
        state.status,
        RECT{70,398,430,438},
        gSmall,
        state.failed
            ? RGB(255,125,205)
            : MUTED,
        DT_LEFT | DT_WORDBREAK
    );

    // progress
    RECT progressBg{
        70,454,650,464
    };

    fill(
        mem,
        progressBg,
        CARD
    );

    RECT progress =
        progressBg;

    progress.right =
        progress.left +
        (
            (progressBg.right - progressBg.left) *
            std::clamp(state.percent, 0, 100) /
            100
        );

    fill(
        mem,
        progress,
        state.failed ? ACCENT : CYAN
    );

    BitBlt(
        dc,
        0,0,
        client.right,
        client.bottom,
        mem,
        0,0,
        SRCCOPY
    );

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK wndProc(
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
            HDC dc = BeginPaint(hwnd, &ps);

            RECT client{};
            GetClientRect(hwnd, &client);

            paintInstaller(dc, client);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST:
        {
            POINT p{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            ScreenToClient(hwnd, &p);

            RECT client{};
            GetClientRect(hwnd, &client);

            if (
                contains(closeRect(client), p.x, p.y) ||
                contains(minRect(client), p.x, p.y)
            )
                return HTCLIENT;

            if (p.y < TITLE_HEIGHT)
                return HTCAPTION;

            return HTCLIENT;
        }

        case WM_LBUTTONUP:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            RECT client{};
            GetClientRect(hwnd, &client);

            if (contains(closeRect(client), x, y))
            {
                InstallState state;
                {
                    std::lock_guard<std::mutex> lock(gMutex);
                    state = gState;
                }

                if (!state.active)
                    DestroyWindow(hwnd);

                return 0;
            }

            if (contains(minRect(client), x, y))
            {
                ShowWindow(hwnd, SW_MINIMIZE);
                return 0;
            }

            if (contains(desktopCheck(), x, y))
            {
                gDesktopShortcut = !gDesktopShortcut;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (contains(launchCheck(), x, y))
            {
                gLaunchAfter = !gLaunchAfter;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (contains(installButton(), x, y))
            {
                bool canStart = false;

                {
                    std::lock_guard<std::mutex> lock(gMutex);

                    if (!gState.active && !gState.complete)
                    {
                        gState.active = true;
                        canStart = true;
                    }
                }

                if (canStart)
                {
                    std::thread(runInstall).detach();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }

                return 0;
            }

            return 0;
        }

        case WM_INSTALL_PROGRESS:
        {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_INSTALL_DONE:
        {
            InstallState state;
            {
                std::lock_guard<std::mutex> lock(gMutex);
                state = gState;
            }

            InvalidateRect(hwnd, nullptr, FALSE);

            if (
                state.complete &&
                gLaunchAfter
            )
            {
                ShellExecuteW(
                    hwnd,
                    L"open",
                    launcherPath().c_str(),
                    nullptr,
                    installDir().c_str(),
                    SW_SHOWNORMAL
                );
            }

            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}
}

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE,
    LPSTR,
    int showCommand)
{
    CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED
    );

    int argc = 0;

    LPWSTR* argv =
        CommandLineToArgvW(
            GetCommandLineW(),
            &argc
        );

    bool isUninstall = false;

    for (int i = 1; i < argc; ++i)
    {
        if (
            _wcsicmp(
                argv[i],
                L"--uninstall"
            ) == 0
        )
        {
            isUninstall = true;
            break;
        }
    }

    if (argv)
        LocalFree(argv);

    if (isUninstall)
    {
        uninstall();
        CoUninitialize();
        return 0;
    }

    gFont = CreateFontW(
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

    gSmall = CreateFontW(
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

    gTitle = CreateFontW(
        -34,0,0,0,
        FW_SEMIBOLD,
        FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Segoe UI"
    );

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = wndProc;
    wc.lpszClassName =
        L"NewtTechInstallerWindow";
    wc.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW
        );

    RegisterClassExW(&wc);

    gWindow = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"NewtTech Installer",
        WS_POPUP |
        WS_MINIMIZEBOX |
        WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        520,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!gWindow)
        return 1;

    DWM_WINDOW_CORNER_PREFERENCE preference =
        DWMWCP_DONOTROUND;

    DwmSetWindowAttribute(
        gWindow,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &preference,
        sizeof(preference)
    );

    ShowWindow(
        gWindow,
        showCommand
    );

    UpdateWindow(gWindow);

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

    if (gFont) DeleteObject(gFont);
    if (gSmall) DeleteObject(gSmall);
    if (gTitle) DeleteObject(gTitle);

    CoUninitialize();

    return static_cast<int>(
        msg.wParam
    );
}
