
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <dwmapi.h>

#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")

#define MANIFEST_URL L"https://launcher.newttech.net/app/manifest.json"
#define PRODUCT_NAME L"NewtTech Launcher"
#define TITLE_HEIGHT 38
#define MAX_FILES 16
#define BUF_SIZE 65536

#define WM_INSTALL_PROGRESS (WM_APP + 1)
#define WM_INSTALL_DONE     (WM_APP + 2)

typedef struct {
    wchar_t name[260];
    wchar_t url[2048];
    wchar_t sha256[129];
} ManifestFile;

typedef struct {
    wchar_t version[64];
    int fileCount;
    ManifestFile files[MAX_FILES];
} Manifest;

typedef struct {
    BOOL active;
    BOOL complete;
    BOOL failed;
    int percent;
    wchar_t status[512];
} InstallState;

static HWND g_hwnd = NULL;
static HFONT g_font = NULL;
static HFONT g_small = NULL;
static HFONT g_title = NULL;
static InstallState g_state = {0};
static CRITICAL_SECTION g_lock;
static BOOL g_desktopShortcut = TRUE;
static BOOL g_launchAfter = TRUE;

static const COLORREF BG = RGB(5,10,24);
static const COLORREF PANEL = RGB(8,17,38);
static const COLORREF CARD = RGB(12,25,51);
static const COLORREF BORDER = RGB(25,52,83);
static const COLORREF TEXT = RGB(239,247,255);
static const COLORREF MUTED = RGB(124,151,178);
static const COLORREF ACCENT = RGB(255,35,180);
static const COLORREF CYAN = RGB(0,225,255);
static const COLORREF DARKTEXT = RGB(5,10,24);

static void fill_rect(HDC dc, RECT r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void draw_text(HDC dc, const wchar_t *s, RECT r, HFONT f, COLORREF c, UINT flags) {
    HFONT old = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, flags);
    SelectObject(dc, old);
}

static BOOL point_in(RECT r, int x, int y) {
    POINT p = {x,y};
    return PtInRect(&r, p);
}

static void set_state(int percent, const wchar_t *status) {
    EnterCriticalSection(&g_lock);
    g_state.percent = percent;
    wcsncpy(g_state.status, status, 511);
    g_state.status[511] = L'\0';
    LeaveCriticalSection(&g_lock);

    if (g_hwnd)
        PostMessageW(g_hwnd, WM_INSTALL_PROGRESS, 0, 0);
}

static BOOL get_known_folder(REFKNOWNFOLDERID id, wchar_t *out, DWORD outCount) {
    PWSTR p = NULL;
    if (FAILED(SHGetKnownFolderPath(id, 0, NULL, &p)))
        return FALSE;

    wcsncpy(out, p, outCount - 1);
    out[outCount - 1] = L'\0';
    CoTaskMemFree(p);
    return TRUE;
}

static void join_path(wchar_t *out, DWORD outCount, const wchar_t *a, const wchar_t *b) {
    _snwprintf(out, outCount, L"%s\\%s", a, b);
    out[outCount - 1] = L'\0';
}

static BOOL get_install_dir(wchar_t *out, DWORD count) {
    wchar_t local[MAX_PATH];
    if (!get_known_folder(&FOLDERID_LocalAppData, local, MAX_PATH))
        return FALSE;

    _snwprintf(out, count, L"%s\\Programs\\NewtTech Launcher", local);
    out[count - 1] = L'\0';
    return TRUE;
}

static BOOL crack_url(const wchar_t *url, wchar_t *host, DWORD hostLen,
                      wchar_t *path, DWORD pathLen, INTERNET_PORT *port, BOOL *secure) {
    URL_COMPONENTSW uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);

    uc.lpszHostName = host;
    uc.dwHostNameLength = hostLen;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = pathLen;

    if (!WinHttpCrackUrl(url, 0, 0, &uc))
        return FALSE;

    *port = uc.nPort;
    *secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    return TRUE;
}

static BOOL http_get_memory(const wchar_t *url, BYTE **dataOut, DWORD *sizeOut) {
    wchar_t host[512] = {0};
    wchar_t path[4096] = {0};
    INTERNET_PORT port = 0;
    BOOL secure = FALSE;

    if (!crack_url(url, host, 512, path, 4096, &port, &secure))
        return FALSE;

    HINTERNET ses = WinHttpOpen(L"NewtTechInstaller/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    if (!ses) return FALSE;

    HINTERNET con = WinHttpConnect(ses, host, port, 0);
    if (!con) {
        WinHttpCloseHandle(ses);
        return FALSE;
    }

    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);

    if (!req) {
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return FALSE;
    }

    BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, NULL);

    if (!ok) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return FALSE;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(req,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    if (status < 200 || status >= 300) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return FALSE;
    }

    BYTE *buf = NULL;
    DWORD total = 0;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail))
            break;
        if (avail == 0)
            break;

        BYTE *next = NULL;

        if (buf == NULL) {
            next = (BYTE*)HeapAlloc(
                GetProcessHeap(),
                0,
                total + avail + 1
            );
        } else {
            next = (BYTE*)HeapReAlloc(
                GetProcessHeap(),
                0,
                buf,
                total + avail + 1
            );
        }

        if (!next) {
            if (buf)
                HeapFree(GetProcessHeap(), 0, buf);

            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return FALSE;
        }

        buf = next;

        DWORD read = 0;
        if (!WinHttpReadData(req, buf + total, avail, &read))
            break;
        total += read;
    }

    if (!buf) {
        buf = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 1);
        total = 0;
    }

    buf[total] = 0;

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);

    *dataOut = buf;
    *sizeOut = total;
    return TRUE;
}

static void set_download_error(const wchar_t *stage, DWORD errorCode) {
    wchar_t message[512];

    _snwprintf(
        message,
        512,
        L"%s (Windows error %lu)",
        stage,
        (unsigned long)errorCode
    );

    message[511] = L'\0';
    set_state(0, message);
}

static BOOL ensure_directory_tree(const wchar_t *path) {
    int result = SHCreateDirectoryExW(
        NULL,
        path,
        NULL
    );

    return
        result == ERROR_SUCCESS ||
        result == ERROR_ALREADY_EXISTS ||
        result == ERROR_FILE_EXISTS;
}

static BOOL http_download_file(
    const wchar_t *url,
    const wchar_t *dest,
    int basePercent,
    int spanPercent)
{
    wchar_t host[512] = {0};
    wchar_t path[4096] = {0};

    INTERNET_PORT port = 0;
    BOOL secure = FALSE;

    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;

    HANDLE file = INVALID_HANDLE_VALUE;
    BOOL success = FALSE;

    DWORD lastError = ERROR_SUCCESS;

    if (!crack_url(
            url,
            host,
            512,
            path,
            4096,
            &port,
            &secure))
    {
        set_download_error(
            L"Unable to parse download URL",
            GetLastError()
        );
        return FALSE;
    }

    session = WinHttpOpen(
        L"NewtTechInstaller/0.7.5",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!session) {
        set_download_error(
            L"Unable to initialize WinHTTP",
            GetLastError()
        );
        goto cleanup;
    }

    /* Keep downloads from hanging forever. */
    WinHttpSetTimeouts(
        session,
        15000,
        15000,
        30000,
        30000
    );

    connection = WinHttpConnect(
        session,
        host,
        port,
        0
    );

    if (!connection) {
        set_download_error(
            L"Unable to connect to download server",
            GetLastError()
        );
        goto cleanup;
    }

    request = WinHttpOpenRequest(
        connection,
        L"GET",
        path[0] ? path : L"/",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0
    );

    if (!request) {
        set_download_error(
            L"Unable to create download request",
            GetLastError()
        );
        goto cleanup;
    }

    /* Allow normal HTTP redirects if the server/CDN introduces one later. */
    DWORD redirectPolicy =
        WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;

    WinHttpSetOption(
        request,
        WINHTTP_OPTION_REDIRECT_POLICY,
        &redirectPolicy,
        sizeof(redirectPolicy)
    );

    if (!WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0))
    {
        set_download_error(
            L"Unable to send download request",
            GetLastError()
        );
        goto cleanup;
    }

    if (!WinHttpReceiveResponse(
            request,
            NULL))
    {
        set_download_error(
            L"Unable to receive download response",
            GetLastError()
        );
        goto cleanup;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);

    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX))
    {
        set_download_error(
            L"Unable to read HTTP status",
            GetLastError()
        );
        goto cleanup;
    }

    if (status < 200 || status >= 300) {
        wchar_t message[256];

        _snwprintf(
            message,
            256,
            L"Download server returned HTTP %lu",
            (unsigned long)status
        );

        message[255] = L'\0';
        set_state(0, message);
        goto cleanup;
    }

    /*
        Make sure the complete destination directory exists.
        This is more reliable than CreateDirectoryW() on only the final folder.
    */
    {
        wchar_t parent[MAX_PATH * 2];

        wcsncpy(
            parent,
            dest,
            (MAX_PATH * 2) - 1
        );

        parent[(MAX_PATH * 2) - 1] = L'\0';

        wchar_t *slash = wcsrchr(
            parent,
            L'\\'
        );

        if (slash) {
            *slash = L'\0';

            if (!ensure_directory_tree(parent)) {
                set_download_error(
                    L"Unable to create installation directory",
                    GetLastError()
                );
                goto cleanup;
            }
        }
    }

    file = CreateFileW(
        dest,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );

    if (file == INVALID_HANDLE_VALUE) {
        set_download_error(
            L"Unable to create downloaded file",
            GetLastError()
        );
        goto cleanup;
    }

    ULONGLONG contentLength = 0;

    {
        wchar_t lengthBuffer[64] = {0};
        DWORD lengthSize =
            sizeof(lengthBuffer);

        if (WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_CONTENT_LENGTH,
                WINHTTP_HEADER_NAME_BY_INDEX,
                lengthBuffer,
                &lengthSize,
                WINHTTP_NO_HEADER_INDEX))
        {
            contentLength =
                _wcstoui64(
                    lengthBuffer,
                    NULL,
                    10
                );
        }
    }

    ULONGLONG downloaded = 0;
    BYTE buffer[64 * 1024];

    for (;;) {
        DWORD bytesRead = 0;

        if (!WinHttpReadData(
                request,
                buffer,
                sizeof(buffer),
                &bytesRead))
        {
            set_download_error(
                L"Network error while downloading file",
                GetLastError()
            );
            goto cleanup;
        }

        if (bytesRead == 0)
            break;

        DWORD totalWritten = 0;

        while (totalWritten < bytesRead) {
            DWORD written = 0;

            if (!WriteFile(
                    file,
                    buffer + totalWritten,
                    bytesRead - totalWritten,
                    &written,
                    NULL))
            {
                set_download_error(
                    L"Unable to write downloaded file",
                    GetLastError()
                );
                goto cleanup;
            }

            if (written == 0) {
                set_state(
                    0,
                    L"Windows wrote zero bytes to the downloaded file."
                );
                goto cleanup;
            }

            totalWritten += written;
        }

        downloaded += bytesRead;

        if (contentLength > 0) {
            int percent =
                basePercent +
                (int)(
                    downloaded *
                    spanPercent /
                    contentLength
                );

            if (percent >
                basePercent + spanPercent)
            {
                percent =
                    basePercent + spanPercent;
            }

            wchar_t message[256];

            _snwprintf(
                message,
                256,
                L"Downloading installation files... %d%%",
                percent
            );

            message[255] = L'\0';

            set_state(
                percent,
                message
            );
        }
    }

    if (!FlushFileBuffers(file)) {
        set_download_error(
            L"Unable to finalize downloaded file",
            GetLastError()
        );
        goto cleanup;
    }

    success = TRUE;

cleanup:
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);

    if (request)
        WinHttpCloseHandle(request);

    if (connection)
        WinHttpCloseHandle(connection);

    if (session)
        WinHttpCloseHandle(session);

    if (!success)
        DeleteFileW(dest);

    return success;
}

/* Small JSON string reader for trusted installer manifest fields. */
static BOOL json_get_string(const char *json, const char *key, char *out, size_t outSize) {
    char pattern[256];
    _snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return FALSE;

    p = strchr(p + strlen(pattern), ':');
    if (!p) return FALSE;
    p++;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return FALSE;
    p++;

    size_t n = 0;
    while (*p && *p != '"' && n + 1 < outSize) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[n++] = '\n';
            else if (*p == 'r') out[n++] = '\r';
            else if (*p == 't') out[n++] = '\t';
            else out[n++] = *p;
            p++;
        } else {
            out[n++] = *p++;
        }
    }

    out[n] = 0;
    return (*p == '"');
}

static void utf8_to_wide(const char *src, wchar_t *dest, int destCount) {
    MultiByteToWideChar(CP_UTF8, 0, src, -1, dest, destCount);
    dest[destCount - 1] = L'\0';
}

/* Parse manifest file entries from:
   "files":[{"name":"...","url":"...","sha256":"..."}, ...]
*/
static BOOL parse_manifest(const char *json, Manifest *m) {
    ZeroMemory(m, sizeof(*m));

    char value[4096];

    if (!json_get_string(json, "version", value, sizeof(value)))
        return FALSE;

    utf8_to_wide(value, m->version, 64);

    const char *files = strstr(json, "\"files\"");
    if (!files) return FALSE;

    const char *arr = strchr(files, '[');
    if (!arr) return FALSE;
    arr++;

    int count = 0;

    while (*arr && *arr != ']' && count < MAX_FILES) {
        const char *obj = strchr(arr, '{');
        if (!obj) break;

        const char *end = strchr(obj, '}');
        if (!end) break;

        size_t len = (size_t)(end - obj + 1);
        if (len >= 8192) return FALSE;

        char one[8192];
        memcpy(one, obj, len);
        one[len] = 0;

        char name[1024], url[4096], sha[256];

        if (!json_get_string(one, "name", name, sizeof(name)) ||
            !json_get_string(one, "url", url, sizeof(url)) ||
            !json_get_string(one, "sha256", sha, sizeof(sha)))
            return FALSE;

        utf8_to_wide(name, m->files[count].name, 260);
        utf8_to_wide(url, m->files[count].url, 2048);
        utf8_to_wide(sha, m->files[count].sha256, 129);

        count++;
        arr = end + 1;
    }

    m->fileCount = count;
    return count > 0;
}

static BOOL sha256_file(const wchar_t *path, wchar_t out[65]) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD objLen = 0, hashLen = 0, cb = 0;
    PUCHAR obj = NULL;
    UCHAR digest[32];
    HANDLE file = INVALID_HANDLE_VALUE;
    BOOL ok = FALSE;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        goto cleanup;

    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
            (PUCHAR)&objLen, sizeof(objLen), &cb, 0) < 0)
        goto cleanup;

    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
            (PUCHAR)&hashLen, sizeof(hashLen), &cb, 0) < 0)
        goto cleanup;

    if (hashLen != 32)
        goto cleanup;

    obj = (PUCHAR)HeapAlloc(GetProcessHeap(), 0, objLen);
    if (!obj) goto cleanup;

    if (BCryptCreateHash(alg, &hash, obj, objLen, NULL, 0, 0) < 0)
        goto cleanup;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE)
        goto cleanup;

    BYTE buffer[BUF_SIZE];
    DWORD read = 0;

    while (ReadFile(file, buffer, BUF_SIZE, &read, NULL) && read > 0) {
        if (BCryptHashData(hash, buffer, read, 0) < 0)
            goto cleanup;
    }

    if (BCryptFinishHash(hash, digest, 32, 0) < 0)
        goto cleanup;

    for (int i = 0; i < 32; ++i)
        _snwprintf(out + i * 2, 3, L"%02x", digest[i]);

    out[64] = L'\0';
    ok = TRUE;

cleanup:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash) BCryptDestroyHash(hash);
    if (obj) HeapFree(GetProcessHeap(), 0, obj);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static BOOL create_shortcut(
    const wchar_t *shortcut,
    const wchar_t *target,
    const wchar_t *working,
    const wchar_t *desc,
    const wchar_t *arguments)
{
    IShellLinkW *link = NULL;
    IPersistFile *persist = NULL;
    HRESULT hr = E_FAIL;

    hr = CoCreateInstance(
        &CLSID_ShellLink,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IShellLinkW,
        (void**)&link
    );

    if (FAILED(hr) || !link)
        return FALSE;

    hr = link->lpVtbl->SetPath(link, target);
    if (FAILED(hr))
        goto cleanup;

    if (working && *working) {
        hr = link->lpVtbl->SetWorkingDirectory(link, working);
        if (FAILED(hr))
            goto cleanup;
    }

    if (desc && *desc) {
        hr = link->lpVtbl->SetDescription(link, desc);
        if (FAILED(hr))
            goto cleanup;
    }

    if (arguments && *arguments) {
        hr = link->lpVtbl->SetArguments(link, arguments);
        if (FAILED(hr))
            goto cleanup;
    }

    link->lpVtbl->SetIconLocation(link, target, 0);

    hr = link->lpVtbl->QueryInterface(
        link,
        &IID_IPersistFile,
        (void**)&persist
    );

    if (FAILED(hr) || !persist)
        goto cleanup;

    hr = persist->lpVtbl->Save(
        persist,
        shortcut,
        TRUE
    );

cleanup:
    if (persist)
        persist->lpVtbl->Release(persist);

    if (link)
        link->lpVtbl->Release(link);

    return SUCCEEDED(hr);
}

static void register_uninstall(const wchar_t *version, const wchar_t *installDir,
                               const wchar_t *launcher, const wchar_t *uninstaller) {
    HKEY key = NULL;

    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NewtTech Launcher",
        0, NULL, 0, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;

    #define SETSTR(name,val) RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)(val),((DWORD)wcslen(val)+1)*sizeof(wchar_t))

    SETSTR(L"DisplayName", PRODUCT_NAME);
    SETSTR(L"DisplayVersion", version);
    SETSTR(L"Publisher", L"NewtTech");
    SETSTR(L"InstallLocation", installDir);
    SETSTR(L"DisplayIcon", launcher);

    wchar_t uninstallString[MAX_PATH * 2];
    _snwprintf(uninstallString, MAX_PATH * 2, L"\"%s\" --uninstall", uninstaller);
    SETSTR(L"UninstallString", uninstallString);

    DWORD one = 1;
    RegSetValueExW(key, L"NoModify", 0, REG_DWORD, (BYTE*)&one, sizeof(one));
    RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, (BYTE*)&one, sizeof(one));

    RegCloseKey(key);
    #undef SETSTR
}

static BOOL self_path(wchar_t *out, DWORD count) {
    return GetModuleFileNameW(NULL, out, count) > 0;
}

static void uninstall_app(void) {
    if (MessageBoxW(NULL,
        L"Remove NewtTech Launcher?\n\nYour downloaded modpack instances will be kept.",
        L"Uninstall NewtTech Launcher",
        MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    wchar_t install[MAX_PATH];
    if (!get_install_dir(install, MAX_PATH))
        return;

    wchar_t desktop[MAX_PATH], programs[MAX_PATH];
    wchar_t desktopLink[MAX_PATH];
    wchar_t newtTechMenu[MAX_PATH * 2];
    wchar_t startLink[MAX_PATH * 2];
    wchar_t uninstallLink[MAX_PATH * 2];

    if (get_known_folder(&FOLDERID_Desktop, desktop, MAX_PATH)) {
        join_path(desktopLink, MAX_PATH, desktop, L"NewtTech Launcher.lnk");
        DeleteFileW(desktopLink);
    }

    if (get_known_folder(&FOLDERID_Programs, programs, MAX_PATH)) {
        _snwprintf(
            newtTechMenu,
            MAX_PATH * 2,
            L"%s\\NewtTech",
            programs
        );

        _snwprintf(
            startLink,
            MAX_PATH * 2,
            L"%s\\NewtTech Launcher.lnk",
            newtTechMenu
        );

        _snwprintf(
            uninstallLink,
            MAX_PATH * 2,
            L"%s\\Uninstall NewtTech Launcher.lnk",
            newtTechMenu
        );

        DeleteFileW(startLink);
        DeleteFileW(uninstallLink);
        RemoveDirectoryW(newtTechMenu);
    }

    RegDeleteTreeW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NewtTech Launcher");

    wchar_t self[MAX_PATH];
    self_path(self, MAX_PATH);

    wchar_t cmd[4096];
    _snwprintf(cmd, 4096,
        L"/C timeout /T 2 /NOBREAK >NUL & rmdir /S /Q \"%s\"",
        install);

    ShellExecuteW(NULL, L"open", L"cmd.exe", cmd, NULL, SW_HIDE);
}

static DWORD WINAPI install_thread(LPVOID unused) {
    HRESULT comHr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL comInitialized = SUCCEEDED(comHr) || comHr == S_FALSE;

    (void)unused;

    BYTE *manifestBytes = NULL;
    DWORD manifestSize = 0;
    Manifest manifest;
    wchar_t install[MAX_PATH];

    EnterCriticalSection(&g_lock);
    g_state.active = TRUE;
    g_state.failed = FALSE;
    g_state.complete = FALSE;
    LeaveCriticalSection(&g_lock);

    set_state(2, L"Fetching installer manifest...");

    if (!http_get_memory(MANIFEST_URL, &manifestBytes, &manifestSize)) {
        set_state(0, L"Failed to download installer manifest.");
        goto fail;
    }

    if (!parse_manifest((const char*)manifestBytes, &manifest)) {
        set_state(0, L"Installer manifest is invalid.");
        goto fail;
    }

    if (!get_install_dir(install, MAX_PATH)) {
        set_state(0, L"Unable to determine install location.");
        goto fail;
    }

    CreateDirectoryW(install, NULL);

    set_state(5, L"Preparing installation...");

    for (int i = 0; i < manifest.fileCount; ++i) {
        ManifestFile *mf = &manifest.files[i];

        wchar_t dest[MAX_PATH * 2];
        _snwprintf(dest, MAX_PATH * 2, L"%s\\%s", install, mf->name);

        int base = 5 + (i * 80 / manifest.fileCount);
        int span = 80 / manifest.fileCount;

        wchar_t msg[512];
        _snwprintf(msg, 512, L"Downloading %s...", mf->name);
        set_state(base, msg);

        if (!http_download_file(mf->url, dest, base, span)) {
            /*
                http_download_file() now reports the exact failing stage
                and Windows error code itself. Do not overwrite that
                diagnostic with a generic filename-only message here.
            */
            goto fail;
        }

        wchar_t actual[65];
        if (!sha256_file(dest, actual) ||
            _wcsicmp(actual, mf->sha256) != 0) {
            DeleteFileW(dest);
            _snwprintf(msg, 512, L"SHA-256 verification failed for %s.", mf->name);
            set_state(0, msg);
            goto fail;
        }
    }

    set_state(88, L"Creating Start Menu shortcuts...");

    wchar_t launcher[MAX_PATH * 2];
    wchar_t uninstaller[MAX_PATH * 2];

    _snwprintf(
        launcher,
        MAX_PATH * 2,
        L"%s\\NewtTechLauncher.exe",
        install
    );

    _snwprintf(
        uninstaller,
        MAX_PATH * 2,
        L"%s\\Uninstall NewtTech Launcher.exe",
        install
    );

    wchar_t self[MAX_PATH];
    self_path(self, MAX_PATH);

    if (!CopyFileW(self, uninstaller, FALSE)) {
        set_state(0, L"Unable to create the NewtTech uninstaller.");
        goto fail;
    }

    wchar_t programs[MAX_PATH];

    if (!get_known_folder(&FOLDERID_Programs, programs, MAX_PATH)) {
        set_state(0, L"Unable to locate the Windows Start Menu.");
        goto fail;
    }

    wchar_t newtTechMenu[MAX_PATH * 2];

    _snwprintf(
        newtTechMenu,
        MAX_PATH * 2,
        L"%s\\NewtTech",
        programs
    );

    if (!CreateDirectoryW(newtTechMenu, NULL)) {
        DWORD menuError = GetLastError();

        if (menuError != ERROR_ALREADY_EXISTS) {
            set_state(0, L"Unable to create the NewtTech Start Menu folder.");
            goto fail;
        }
    }

    wchar_t launcherLink[MAX_PATH * 2];

    _snwprintf(
        launcherLink,
        MAX_PATH * 2,
        L"%s\\NewtTech Launcher.lnk",
        newtTechMenu
    );

    if (!create_shortcut(
            launcherLink,
            launcher,
            install,
            L"Launch NewtTech Launcher",
            L""))
    {
        set_state(0, L"Unable to create the Start Menu launcher shortcut.");
        goto fail;
    }

    wchar_t uninstallLink[MAX_PATH * 2];

    _snwprintf(
        uninstallLink,
        MAX_PATH * 2,
        L"%s\\Uninstall NewtTech Launcher.lnk",
        newtTechMenu
    );

    if (!create_shortcut(
            uninstallLink,
            uninstaller,
            install,
            L"Uninstall NewtTech Launcher",
            L"--uninstall"))
    {
        set_state(0, L"Unable to create the Start Menu uninstall shortcut.");
        goto fail;
    }

    if (g_desktopShortcut) {
        set_state(92, L"Creating desktop shortcut...");

        wchar_t desktop[MAX_PATH];

        if (!get_known_folder(&FOLDERID_Desktop, desktop, MAX_PATH)) {
            set_state(0, L"Unable to locate the Desktop folder.");
            goto fail;
        }

        wchar_t desktopLink[MAX_PATH * 2];

        _snwprintf(
            desktopLink,
            MAX_PATH * 2,
            L"%s\\NewtTech Launcher.lnk",
            desktop
        );

        if (!create_shortcut(
                desktopLink,
                launcher,
                install,
                L"Launch NewtTech Launcher",
                L""))
        {
            set_state(0, L"Unable to create the desktop shortcut.");
            goto fail;
        }
    }

    register_uninstall(manifest.version, install, launcher, uninstaller);

    set_state(100, L"Installation complete.");

    EnterCriticalSection(&g_lock);
    g_state.active = FALSE;
    g_state.complete = TRUE;
    LeaveCriticalSection(&g_lock);

    if (manifestBytes)
        HeapFree(GetProcessHeap(), 0, manifestBytes);

    PostMessageW(g_hwnd, WM_INSTALL_DONE, 0, 0);
    if (comInitialized)
        CoUninitialize();
    return 0;

fail:
    EnterCriticalSection(&g_lock);
    g_state.active = FALSE;
    g_state.failed = TRUE;
    g_state.complete = FALSE;
    LeaveCriticalSection(&g_lock);

    if (manifestBytes)
        HeapFree(GetProcessHeap(), 0, manifestBytes);

    PostMessageW(g_hwnd, WM_INSTALL_DONE, 0, 0);
    return 1;
}

static RECT close_rect(RECT c) { RECT r={c.right-46,0,c.right,TITLE_HEIGHT}; return r; }
static RECT min_rect(RECT c) { RECT r={c.right-92,0,c.right-46,TITLE_HEIGHT}; return r; }
static RECT install_rect(void) { RECT r={458,382,650,430}; return r; }
static RECT desktop_rect(void) { RECT r={70,315,350,342}; return r; }
static RECT launch_rect(void) { RECT r={70,349,350,376}; return r; }

static void paint_installer(HDC dc, RECT client) {
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    fill_rect(mem, client, BG);
    fill_rect(mem, (RECT){0,0,client.right,TITLE_HEIGHT}, RGB(6,12,29));
    fill_rect(mem, (RECT){0,TITLE_HEIGHT-1,client.right,TITLE_HEIGHT}, BORDER);
    fill_rect(mem, (RECT){14,15,20,21}, CYAN);

    draw_text(mem, L"NewtTech Installer",
        (RECT){29,0,260,TITLE_HEIGHT}, g_small, TEXT,
        DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    draw_text(mem, L"—", min_rect(client), g_font, MUTED,
        DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    fill_rect(mem, close_rect(client), ACCENT);
    draw_text(mem, L"×", close_rect(client), g_font, DARKTEXT,
        DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    draw_text(mem, L"Install NewtTech Launcher",
        (RECT){70,82,650,126}, g_title, TEXT,
        DT_LEFT|DT_SINGLELINE);

    draw_text(mem, L"Installs the launcher and everything it needs to run.",
        (RECT){72,132,650,160}, g_font, MUTED,
        DT_LEFT|DT_SINGLELINE);

    wchar_t install[MAX_PATH];
    get_install_dir(install, MAX_PATH);

    fill_rect(mem, (RECT){70,190,650,274}, PANEL);
    draw_text(mem, L"INSTALL LOCATION",
        (RECT){88,205,630,225}, g_small, CYAN,
        DT_LEFT|DT_SINGLELINE);
    draw_text(mem, install,
        (RECT){88,236,630,260}, g_font, TEXT,
        DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);

    fill_rect(mem, (RECT){72,319,88,335}, g_desktopShortcut ? ACCENT : CARD);
    if (g_desktopShortcut)
        draw_text(mem, L"✓", (RECT){70,314,90,338},
            g_small, DARKTEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    draw_text(mem, L"Create desktop shortcut",
        (RECT){100,312,350,340}, g_font, TEXT,
        DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    fill_rect(mem, (RECT){72,353,88,369}, g_launchAfter ? CYAN : CARD);
    if (g_launchAfter)
        draw_text(mem, L"✓", (RECT){70,348,90,372},
            g_small, DARKTEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    draw_text(mem, L"Launch when finished",
        (RECT){100,346,350,374}, g_font, TEXT,
        DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    InstallState state;
    EnterCriticalSection(&g_lock);
    state = g_state;
    LeaveCriticalSection(&g_lock);

    RECT button = install_rect();
    fill_rect(mem, button, state.active ? CARD : ACCENT);

    draw_text(mem,
        state.active ? L"Installing..." : (state.complete ? L"Installed" : L"Install"),
        button, g_font, state.active ? MUTED : DARKTEXT,
        DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    draw_text(mem, state.status,
        (RECT){70,398,430,438}, g_small,
        state.failed ? RGB(255,125,205) : MUTED,
        DT_LEFT|DT_WORDBREAK);

    RECT pbg = {70,454,650,464};
    fill_rect(mem, pbg, CARD);

    RECT pf = pbg;
    int clampedPercent = state.percent;

    if (clampedPercent < 0)
        clampedPercent = 0;

    if (clampedPercent > 100)
        clampedPercent = 100;

    pf.right = pf.left +
        ((pbg.right - pbg.left) * clampedPercent / 100);

    fill_rect(mem, pf, state.failed ? ACCENT : CYAN);

    BitBlt(dc,0,0,client.right,client.bottom,mem,0,0,SRCCOPY);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT c; GetClientRect(hwnd, &c);
            paint_installer(dc, c);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST: {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            RECT c; GetClientRect(hwnd, &c);

            if (point_in(close_rect(c),p.x,p.y) ||
                point_in(min_rect(c),p.x,p.y))
                return HTCLIENT;

            if (p.y < TITLE_HEIGHT)
                return HTCAPTION;

            return HTCLIENT;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            RECT c; GetClientRect(hwnd,&c);

            if (point_in(close_rect(c),x,y)) {
                EnterCriticalSection(&g_lock);
                BOOL active = g_state.active;
                LeaveCriticalSection(&g_lock);
                if (!active) DestroyWindow(hwnd);
                return 0;
            }

            if (point_in(min_rect(c),x,y)) {
                ShowWindow(hwnd,SW_MINIMIZE);
                return 0;
            }

            if (point_in(desktop_rect(),x,y)) {
                g_desktopShortcut = !g_desktopShortcut;
                InvalidateRect(hwnd,NULL,FALSE);
                return 0;
            }

            if (point_in(launch_rect(),x,y)) {
                g_launchAfter = !g_launchAfter;
                InvalidateRect(hwnd,NULL,FALSE);
                return 0;
            }

            if (point_in(install_rect(),x,y)) {
                EnterCriticalSection(&g_lock);
                BOOL canStart = !g_state.active && !g_state.complete;
                LeaveCriticalSection(&g_lock);

                if (canStart) {
                    HANDLE th = CreateThread(NULL,0,install_thread,NULL,0,NULL);
                    if (th) CloseHandle(th);
                }
                return 0;
            }

            return 0;
        }

        case WM_INSTALL_PROGRESS:
            InvalidateRect(hwnd,NULL,FALSE);
            return 0;

        case WM_INSTALL_DONE: {
            InvalidateRect(hwnd,NULL,FALSE);

            EnterCriticalSection(&g_lock);
            BOOL complete = g_state.complete;
            LeaveCriticalSection(&g_lock);

            if (complete && g_launchAfter) {
                wchar_t install[MAX_PATH], launcher[MAX_PATH*2];
                if (get_install_dir(install,MAX_PATH)) {
                    _snwprintf(launcher,MAX_PATH*2,L"%s\\NewtTechLauncher.exe",install);
                    ShellExecuteW(hwnd,L"open",launcher,NULL,install,SW_SHOWNORMAL);
                }
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitializeCriticalSection(&g_lock);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    for (int i=1; i<argc; ++i) {
        if (_wcsicmp(argv[i],L"--uninstall")==0) {
            if (argv) LocalFree(argv);
            uninstall_app();
            DeleteCriticalSection(&g_lock);
            CoUninitialize();
            return 0;
        }
    }

    if (argv) LocalFree(argv);

    wcscpy(g_state.status,L"Ready to install.");

    g_font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");

    g_small = CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");

    g_title = CreateFontW(-34,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");

    WNDCLASSEXW wc;
    ZeroMemory(&wc,sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.lpfnWndProc = wnd_proc;
    wc.lpszClassName = L"NewtTechStandaloneInstaller";
    wc.hCursor = LoadCursorW(NULL,IDC_ARROW);

    if (!RegisterClassExW(&wc))
        return 1;

    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"NewtTech Installer",
        WS_POPUP|WS_MINIMIZEBOX|WS_SYSMENU,
        CW_USEDEFAULT,CW_USEDEFAULT,
        720,520,
        NULL,NULL,inst,NULL
    );

    if (!g_hwnd)
        return 1;

    ShowWindow(g_hwnd,show);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg,NULL,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(g_font);
    DeleteObject(g_small);
    DeleteObject(g_title);

    DeleteCriticalSection(&g_lock);
    CoUninitialize();

    return (int)msg.wParam;
}
