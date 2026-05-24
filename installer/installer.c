// Mocida SDK installer
//
// A GUI installer for the Mocida SDK, built using Mocida itself.
// It copies the headers, mocida.dll and the import .lib to a chosen
// destination, then publishes MOCIDA_INCLUDE_DIR / MOCIDA_LIB_DIR and
// appends the lib folder to PATH so consumers can pick them up with:
//
//     $env:MOCIDA_INCLUDE_DIR = "<dest>\include"
//     $env:MOCIDA_LIB_DIR     = "<dest>\lib"
//     # PATH already includes <dest>\lib
//
// The "payload" the installer ships to the destination is the set of
// files staged next to the installer.exe by CMake at build time:
//
//     <exe_dir>/
//         mocida_installer.exe
//         mocida.dll, mocida.lib, SDL3*.dll, WebView2Loader.dll
//         include/uikit/*.h
//
// The defaults distinguish two scopes:
//   - User   (recommended): %LOCALAPPDATA%\Programs\Mocida  (no admin)
//   - System              : C:\Program Files\Mocida          (needs admin)
//
// The path text field is editable so a custom folder can be typed or
// picked via the native folder dialog.

#include <uikit/app.h>
#include <uikit/asset.h>
#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <wininet.h>

#include <stdio.h>
#include <string.h>

// Embedded Mocida logo (assets/logo.svg) — generated at CMake configure
// time. The installer is fully self-contained: no neighbouring assets/
// folder needed, no DLLs to ship.
#include "installer_assets.h"

// Stable "latest release" asset URL. GitHub redirects this to the
// asset of the most-recent published release, so the installer
// always pulls the freshest SDK without baking in a version number.
#define MOCIDA_SDK_ASSET_NAME "mocida-sdk-windows-x64.zip"
#define MOCIDA_SDK_URL                                                       \
    "https://github.com/liy77/mocida/releases/latest/download/"              \
    MOCIDA_SDK_ASSET_NAME

// --------------------------------------------------------------------
// Color palette. Built around the Mocida brand blue (#5271ff) and red
// (#ff3131), paired with a calibrated slate scale for chrome.
// --------------------------------------------------------------------
static const UIColor C_BG          = { 241, 245, 249, 1.0f }; // slate-100 backdrop
static const UIColor C_CARD        = { 255, 255, 255, 1.0f }; // panel surface
static const UIColor C_BRAND_BLUE  = {  82, 113, 255, 1.0f }; // Mocida blue
static const UIColor C_BRAND_RED   = { 255,  49,  49, 1.0f }; // Mocida red
static const UIColor C_TEXT_DARK   = {  15,  23,  42, 1.0f }; // slate-900 headings
static const UIColor C_TEXT_MED    = {  51,  65,  85, 1.0f }; // slate-700 body
static const UIColor C_TEXT_DIM    = { 100, 116, 139, 1.0f }; // slate-500 secondary
static const UIColor C_TEXT_FAINT  = { 148, 163, 184, 1.0f }; // slate-400 section labels
static const UIColor C_BORDER      = { 226, 232, 240, 1.0f }; // slate-200 separators
static const UIColor C_SUBTLE_BG   = { 248, 250, 252, 1.0f }; // slate-50 code panel
static const UIColor C_OK          = {   5, 150, 105, 1.0f }; // emerald-600
static const UIColor C_ERR         = { 220,  38,  38, 1.0f };
static const UIColor C_WARN        = { 217, 119,   6, 1.0f }; // amber-600
static const UIColor C_PRIMARY     = {  82, 113, 255, 1.0f }; // alias for brand blue
static const UIColor C_GREEN       = {  16, 185, 129, 1.0f }; // emerald-500 install button
static const UIColor C_MUTED       = { 241, 245, 249, 1.0f }; // slate-100 idle button
static const UIColor C_MUTED_BORDER= { 203, 213, 225, 1.0f }; // slate-300 outline

// Default install paths. The user-scope one uses %LOCALAPPDATA% so it
// resolves to e.g. C:\Users\<you>\AppData\Local\Programs\Mocida without
// admin. The system-scope one needs an elevated process.
static const char* kUserDefault   = "%LOCALAPPDATA%\\Programs\\Mocida";
static const char* kSystemDefault = "C:\\Program Files\\Mocida";

typedef struct {
    UIApp*         app;
    UIWidget*      userBtn;
    UIWidget*      sysBtn;
    UITextField*   pathField;
    UIWidget*      envPreview;   // multi-line label showing env-var preview
    UIWidget*      statusLabel;
    UIProgressBar* progress;     // live progress bar shown during install
    UIWidget*      progressW;    // wrapper widget (used for show/hide)
    UIWidget*      progressPctLabel; // small "45%" label next to the bar
    int            scope;        // 0 = user, 1 = system
    int            isAdmin;      // process is elevated
} InstallerState;

static InstallerState g;

// --------------------------------------------------------------------
// Small Win32 helpers
// --------------------------------------------------------------------

static void ExpandEnv(const char* in, char* out, DWORD outSize) {
    DWORD n = ExpandEnvironmentStringsA(in, out, outSize);
    if (n == 0 || n > outSize) {
        // Fallback: copy literally.
        strncpy(out, in, outSize);
        out[outSize - 1] = 0;
    }
}

// Process token check: returns 1 if the current process is elevated.
static int IsProcessElevated(void) {
    HANDLE tok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return 0;
    TOKEN_ELEVATION el = {0};
    DWORD ret = 0;
    int elevated = 0;
    if (GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &ret)) {
        elevated = el.TokenIsElevated ? 1 : 0;
    }
    CloseHandle(tok);
    return elevated;
}

// mkdir -p: walks the path and creates each segment in turn.
static int MakeDirRecursive(const char* path) {
    char tmp[MAX_PATH];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return 0;
    memcpy(tmp, path, n + 1);

    // Skip the drive letter prefix ("C:\") so we don't try to mkdir
    // "C:".
    size_t start = 0;
    if (n >= 3 && tmp[1] == ':' && (tmp[2] == '\\' || tmp[2] == '/')) start = 3;

    for (size_t i = start; i < n; i++) {
        if (tmp[i] == '\\' || tmp[i] == '/') {
            char saved = tmp[i];
            tmp[i] = 0;
            CreateDirectoryA(tmp, NULL);
            tmp[i] = saved;
        }
    }
    CreateDirectoryA(tmp, NULL);
    DWORD attrs = GetFileAttributesA(tmp);
    return (attrs != INVALID_FILE_ATTRIBUTES) &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Forward declaration: the curl progress callback (further down)
// needs to push updates into the status label which is defined below.
static void SetStatus(const char* msg, UIColor color);

// --------------------------------------------------------------------
// SDK download from GitHub releases
// --------------------------------------------------------------------
//
// The installer pulls a single zip asset from the latest published
// release on github.com/liy77/mocida. The zip is expected to contain
// an `include/uikit/*.h` tree and a flat `lib/` with mocida.dll,
// mocida.lib, the SDL3*.dll set, and WebView2Loader.dll. The release
// script (release/release.bat) is what produces this layout.

// Pumps SDL events and repaints once. Called periodically from the
// download loop so the OS doesn't paint "Not Responding" over the
// window and the status label updates are actually visible.
static void PumpAndRepaint(void) {
    if (!g.app || !g.app->window) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) { /* drain */ }
    UIWindow_Render(g.app->window);
}

// Updates the progress bar fill (0..1), updates the "XX%" label next
// to the bar, and forces a repaint so the user sees both move.
static void SetProgress(float value) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (g.progress) UIProgressBar_SetValue(g.progress, value);
    if (g.progressPctLabel && g.progressPctLabel->data) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", (int)(value * 100.0f + 0.5f));
        UIText_SetText((UIText*)g.progressPctLabel->data, buf);
    }
    PumpAndRepaint();
}

// Shows/hides both the bar and the % label as a pair.
static void ShowProgress(int visible) {
    if (g.progressW)        UIWidget_SetVisible(g.progressW, visible);
    if (g.progressPctLabel) UIWidget_SetVisible(g.progressPctLabel, visible);
}

// Downloads `url` to `outPath` using WinINet so we have ZERO third-party
// runtime DLLs alongside the installer (wininet.dll is a Windows system
// library; statically linking wininet.lib is just an import lib).
// Returns 1 on success, 0 on failure (error written into `errOut`).
static int DownloadFile(const char* url, const char* outPath,
                        char* errOut, size_t errSize) {
    HINTERNET hSession = InternetOpenA("Mocida-Installer/1.0",
                                        INTERNET_OPEN_TYPE_PRECONFIG,
                                        NULL, NULL, 0);
    if (!hSession) {
        snprintf(errOut, errSize, "InternetOpen failed (err %lu).",
                 GetLastError());
        return 0;
    }

    // INTERNET_FLAG_NO_CACHE_WRITE: don't litter the user's IE cache
    // with our 3.5MB zip. The other flags let WinINet follow GitHub's
    // redirect chain (latest/download -> release-assets URL).
    DWORD flags = INTERNET_FLAG_NO_CACHE_WRITE |
                  INTERNET_FLAG_RELOAD         |
                  INTERNET_FLAG_KEEP_CONNECTION;
    HINTERNET hReq = InternetOpenUrlA(hSession, url, NULL, 0, flags, 0);
    if (!hReq) {
        snprintf(errOut, errSize, "InternetOpenUrl failed (err %lu).",
                 GetLastError());
        InternetCloseHandle(hSession);
        return 0;
    }

    // Check HTTP status. GitHub returns 200 (after redirects).
    DWORD status = 0;
    DWORD statusLen = sizeof(status);
    DWORD idx = 0;
    if (HttpQueryInfoA(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &status, &statusLen, &idx) && status >= 400) {
        snprintf(errOut, errSize, "HTTP %lu fetching the SDK asset.", status);
        InternetCloseHandle(hReq);
        InternetCloseHandle(hSession);
        return 0;
    }

    // Content-Length lets us show a percentage. Absent for chunked
    // responses; we degrade gracefully to "bytes received" in that case.
    DWORD64 total = 0;
    char lenBuf[32];
    DWORD lenBufLen = sizeof(lenBuf);
    idx = 0;
    if (HttpQueryInfoA(hReq, HTTP_QUERY_CONTENT_LENGTH, lenBuf, &lenBufLen, &idx)) {
        total = _strtoui64(lenBuf, NULL, 10);
    }

    FILE* fp = fopen(outPath, "wb");
    if (!fp) {
        snprintf(errOut, errSize, "Could not open %s for writing.", outPath);
        InternetCloseHandle(hReq);
        InternetCloseHandle(hSession);
        return 0;
    }

    char         buf[16 * 1024];
    DWORD64      received = 0;
    DWORD        lastTickRepaint = GetTickCount();
    int          ok = 1;
    for (;;) {
        DWORD got = 0;
        if (!InternetReadFile(hReq, buf, (DWORD)sizeof(buf), &got)) {
            snprintf(errOut, errSize,
                     "InternetReadFile failed (err %lu).", GetLastError());
            ok = 0;
            break;
        }
        if (got == 0) break;                    // EOF
        if (fwrite(buf, 1, got, fp) != got) {
            snprintf(errOut, errSize, "Write to %s failed.", outPath);
            ok = 0;
            break;
        }
        received += got;

        // Repaint at most ~20 times/sec so we don't burn CPU on render.
        DWORD now = GetTickCount();
        if (now - lastTickRepaint >= 50) {
            char st[160];
            float frac = 0.0f;
            if (total > 0) {
                frac = (float)((double)received / (double)total);
                snprintf(st, sizeof(st),
                    "Downloading SDK... %.1f / %.1f MB",
                    (double)received / 1048576.0,
                    (double)total    / 1048576.0);
            } else {
                snprintf(st, sizeof(st),
                    "Downloading SDK... %.1f MB", (double)received / 1048576.0);
            }
            SetStatus(st, C_TEXT_DIM);
            // Download spans 5%..85% of the overall install. The other
            // 15% is split between extract (85..95) and env-var publish
            // (95..100), so the bar advances even when bytes stop.
            if (g.progress && total > 0) {
                SetProgress(0.05f + frac * 0.80f);
            } else {
                PumpAndRepaint();
            }
            lastTickRepaint = now;
        }
    }

    fclose(fp);
    InternetCloseHandle(hReq);
    InternetCloseHandle(hSession);

    if (!ok) {
        DeleteFileA(outPath);
        return 0;
    }
    if (received == 0) {
        DeleteFileA(outPath);
        snprintf(errOut, errSize, "Download produced 0 bytes.");
        return 0;
    }
    return 1;
}

// Extracts `zipPath` into `destDir` by invoking PowerShell's
// Expand-Archive. Avoids pulling in a zip dependency for what is a
// one-shot operation in an installer.
static int ExtractZip(const char* zipPath, const char* destDir,
                      char* errOut, size_t errSize) {
    // Escape any single quotes (paranoia — unlikely in install paths).
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
        "\"$ErrorActionPreference='Stop'; "
        "Expand-Archive -Force -LiteralPath '%s' -DestinationPath '%s'\"",
        zipPath, destDir);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags    = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        snprintf(errOut, errSize, "Could not launch PowerShell to extract.");
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (code != 0) {
        snprintf(errOut, errSize,
            "Extraction failed (PowerShell exit code %lu).", code);
        return 0;
    }
    return 1;
}

// Counts non-directory entries in a directory tree (recursive). Used
// for the "<N> headers" / "<N> libraries" status messages so the user
// gets a sense of what landed on disk.
static int CountFiles(const char* dir) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    do {
        if (fd.cFileName[0] == '.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char sub[MAX_PATH];
            snprintf(sub, sizeof(sub), "%s\\%s", dir, fd.cFileName);
            count += CountFiles(sub);
        } else {
            count++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
}

// Returns 1 if the substring `needle` appears (case-insensitive) in
// `hay`. Used to test whether a directory is already on PATH.
static int IContains(const char* hay, const char* needle) {
    return StrStrIA(hay, needle) != NULL;
}

// --------------------------------------------------------------------
// Persistent environment variables via the registry
// --------------------------------------------------------------------
//
// `setx` could do this too but it silently truncates PATH at 1024
// characters, which is unsafe. Writing to HKCU\Environment (or
// HKLM\...\Session Manager\Environment for system) bypasses that.
//
// After mutating values, broadcast WM_SETTINGCHANGE so explorer.exe
// and newly-spawned shells pick the change up without a reboot.

static LONG OpenEnvKey(int system, REGSAM access, HKEY* out) {
    if (system) {
        return RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
            0, access, out);
    }
    return RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, access, out);
}

static int SetEnvPersistent(const char* name, const char* value,
                            DWORD type, int system) {
    HKEY hKey;
    if (OpenEnvKey(system, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return 0;
    LONG r = RegSetValueExA(hKey, name, 0, type,
                            (const BYTE*)value,
                            (DWORD)(strlen(value) + 1));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

static int ReadEnvPersistent(const char* name, char* out, DWORD outSize,
                             int system) {
    HKEY hKey;
    out[0] = 0;
    if (OpenEnvKey(system, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) return 0;
    DWORD type = 0, size = outSize;
    LONG r = RegQueryValueExA(hKey, name, NULL, &type, (BYTE*)out, &size);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS) { out[0] = 0; return 0; }
    return 1;
}

// Append `libDir` to PATH if it is not already present (case-insensitive
// match). Returns 1 on success or no-op, 0 on registry failure.
static int AppendToPath(const char* libDir, int system) {
    static char path[32768];
    ReadEnvPersistent("Path", path, sizeof(path), system);
    if (path[0] && IContains(path, libDir)) return 1;

    static char newPath[32768];
    if (path[0]) snprintf(newPath, sizeof(newPath), "%s;%s", path, libDir);
    else         snprintf(newPath, sizeof(newPath), "%s", libDir);

    return SetEnvPersistent("Path", newPath, REG_EXPAND_SZ, system);
}

static void BroadcastEnvChange(void) {
    DWORD_PTR result = 0;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000,
                        &result);
}

// --------------------------------------------------------------------
// Status / preview helpers
// --------------------------------------------------------------------

static void SetStatus(const char* msg, UIColor color) {
    if (!g.statusLabel || !g.statusLabel->data) return;
    UIText* t = (UIText*)g.statusLabel->data;
    UIText_SetColor(t, color);
    UIText_SetText (t, (char*)msg);
}

static void UpdateEnvPreview(const char* dest) {
    if (!g.envPreview || !g.envPreview->data) return;
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "MOCIDA_INCLUDE_DIR = %s\\include\n"
        "MOCIDA_LIB_DIR     = %s\\lib\n"
        "PATH += %s\\lib",
        dest, dest, dest);
    UIText_SetText((UIText*)g.envPreview->data, buf);
}

static void RefreshScopeButtons(void) {
    UIButton* u = (UIButton*)g.userBtn->data;
    UIButton* s = (UIButton*)g.sysBtn->data;
    if (g.scope == 0) {
        UIButton_SetColors(u, C_PRIMARY, UI_COLOR_WHITE);
        UIButton_SetColors(s, C_MUTED,   C_TEXT_DARK);
    } else {
        UIButton_SetColors(u, C_MUTED,   C_TEXT_DARK);
        UIButton_SetColors(s, C_PRIMARY, UI_COLOR_WHITE);
    }
    UIText_DestroyTexture(u->label);
    UIText_DestroyTexture(s->label);
}

static void SwitchToScopeDefault(void) {
    char expanded[MAX_PATH];
    ExpandEnv(g.scope == 0 ? kUserDefault : kSystemDefault,
              expanded, sizeof(expanded));
    UITextField_SetText(g.pathField, expanded);
    UpdateEnvPreview(expanded);

    if (g.scope == 1 && !g.isAdmin) {
        SetStatus("System install needs admin. Re-run as Administrator or use User.",
                  C_WARN);
    } else {
        SetStatus("Ready.", C_TEXT_DIM);
    }
}

// --------------------------------------------------------------------
// Event handlers
// --------------------------------------------------------------------

static void OnUserClick(UIButton* btn, void* ud) {
    (void)btn; (void)ud;
    g.scope = 0;
    RefreshScopeButtons();
    SwitchToScopeDefault();
}

static void OnSystemClick(UIButton* btn, void* ud) {
    (void)btn; (void)ud;
    g.scope = 1;
    RefreshScopeButtons();
    SwitchToScopeDefault();
}

static void OnFolderPicked(void* ud, const char* const* filelist, int filter) {
    (void)ud; (void)filter;
    if (!filelist || !filelist[0]) return;
    UITextField_SetText(g.pathField, filelist[0]);
    UpdateEnvPreview(filelist[0]);
}

static void OnBrowse(UIButton* btn, void* ud) {
    (void)btn; (void)ud;
    SDL_Window* win = g.app->window->sdlWindow;
    SDL_ShowOpenFolderDialog(OnFolderPicked, NULL, win, NULL, false);
}

static void OnPathChange(UITextField* tf, const char* text, void* ud) {
    (void)tf; (void)ud;
    UpdateEnvPreview(text ? text : "");
}

static void OnInstall(UIButton* btn, void* ud) {
    (void)btn; (void)ud;

    const char* raw = UITextField_GetText(g.pathField);
    if (!raw || !*raw) {
        SetStatus("Please choose an install folder.", C_ERR);
        return;
    }

    // Expand any %ENV% in the user-typed path so we hand absolute
    // paths to CreateDirectoryA / PowerShell.
    char dest[MAX_PATH];
    ExpandEnv(raw, dest, sizeof(dest));

    char destInc[MAX_PATH], destLib[MAX_PATH], destIncUikit[MAX_PATH];
    snprintf(destInc,      sizeof(destInc),      "%s\\include",        dest);
    snprintf(destIncUikit, sizeof(destIncUikit), "%s\\include\\uikit", dest);
    snprintf(destLib,      sizeof(destLib),      "%s\\lib",            dest);

    ShowProgress(1);
    SetProgress(0.0f);

    SetStatus("Creating directories...", C_TEXT_DIM);
    if (!MakeDirRecursive(destIncUikit) || !MakeDirRecursive(destLib)) {
        SetStatus("Could not create target directories (permissions?).", C_ERR);
        ShowProgress(0);
        return;
    }
    SetProgress(0.05f);

    // Stage the download under %TEMP% so we don't pollute the install
    // folder with the raw zip after extraction.
    char tmpDir[MAX_PATH], tmpZip[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmpDir)) {
        SetStatus("Could not resolve %TEMP%.", C_ERR);
        return;
    }
    snprintf(tmpZip, sizeof(tmpZip), "%smocida-sdk.zip", tmpDir);

    SetStatus("Contacting GitHub...", C_TEXT_DIM);
    UIWindow_Render(g.app->window);

    char err[512];
    if (!DownloadFile(MOCIDA_SDK_URL, tmpZip, err, sizeof(err))) {
        SetStatus(err, C_ERR);
        ShowProgress(0);
        return;
    }
    SetProgress(0.85f);

    SetStatus("Extracting SDK...", C_TEXT_DIM);
    UIWindow_Render(g.app->window);
    if (!ExtractZip(tmpZip, dest, err, sizeof(err))) {
        SetStatus(err, C_ERR);
        ShowProgress(0);
        return;
    }
    DeleteFileA(tmpZip);
    SetProgress(0.95f);

    int headers = CountFiles(destIncUikit);
    int libs    = CountFiles(destLib);
    if (headers == 0 || libs == 0) {
        SetStatus("SDK zip is missing include/uikit or lib contents.", C_ERR);
        ShowProgress(0);
        return;
    }

    SetStatus("Publishing environment variables...", C_TEXT_DIM);
    UIWindow_Render(g.app->window);
    int sys = (g.scope == 1);
    int incOk = SetEnvPersistent("MOCIDA_INCLUDE_DIR", destInc, REG_SZ, sys);
    int libOk = SetEnvPersistent("MOCIDA_LIB_DIR",     destLib, REG_SZ, sys);
    if (!incOk || !libOk) {
        if (sys && !g.isAdmin) {
            SetStatus("Access denied. Re-run installer as Administrator for System scope.",
                      C_ERR);
        } else {
            SetStatus("Failed to write environment variables to the registry.",
                      C_ERR);
        }
        ShowProgress(0);
        return;
    }

    int pathOk = AppendToPath(destLib, sys);
    SetProgress(1.0f);

    // Mirror onto this process so the user can verify with `set` in a
    // shell launched from here.
    SetEnvironmentVariableA("MOCIDA_INCLUDE_DIR", destInc);
    SetEnvironmentVariableA("MOCIDA_LIB_DIR",     destLib);

    BroadcastEnvChange();

    char done[512];
    snprintf(done, sizeof(done),
        "Done. %d headers, %d libraries -> %s. %s"
        "Open a new shell to inherit the variables.",
        headers, libs, dest,
        pathOk ? "PATH updated. " : "PATH update failed. ");
    SetStatus(done, C_OK);
}

// --------------------------------------------------------------------
// UI builders
// --------------------------------------------------------------------

static UIWidget* BuildLabel(UIChildren* children, const char* text,
                            float size, UIColor color,
                            float x, float y) {
    UIText* t = UIText_Create((char*)text, size);
    UIText_SetFontFamily(t, UIGetFont("Segoe UI"));
    UIText_SetColor(t, color);
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return w;
}

static UIWidget* BuildButton(UIChildren* children, const char* text,
                             float fontSize, UIColor bg, UIColor fg,
                             float x, float y, float w, float h,
                             UIButtonCallback onClick) {
    UIButton* b = UIButton_Create(text, fontSize);
    UIButton_SetFontFamily(b, UIGetFont("Segoe UI"));
    UIButton_SetRadius(b, 8.0f);
    UIButton_SetColors(b, bg, fg);

    // Subtle 1 px border. UIButton_SetColors leaves the per-state
    // border colours as transparent, so we patch them in directly —
    // a darker tone on the muted variant (where it actually shows up
    // against the slate-100 fill) and a slightly tinted brand-blue
    // for the primary variant so it doesn't read as flat.
    UIButton_SetBorderWidth(b, 1.0f);
    UIColor borderC;
    if (bg.r == C_PRIMARY.r && bg.g == C_PRIMARY.g && bg.b == C_PRIMARY.b) {
        borderC = (UIColor){ 60, 90, 220, 1.0f };       // slightly darker brand blue
    } else {
        borderC = C_MUTED_BORDER;                       // slate-300 hairline
    }
    for (int s = 0; s < 4; s++) b->styles[s].border = borderC;

    UIButton_OnClick(b, onClick, NULL);
    UIWidget* widget = widgcs(b, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
    return widget;
}

// --------------------------------------------------------------------
// Small visual helpers used by the polished layout
// --------------------------------------------------------------------

// --------------------------------------------------------------------
// Logo animation: two overlapping rounded squares that breathe in and
// out via independent ping-pong rotations. Mirrors the animateTransform
// blocks in assets/banner.svg used by the README header.
// --------------------------------------------------------------------

static void OnLogoBlueDone(void* ud);
static void OnLogoRedDone (void* ud);

// Each callback re-arms the opposite-direction tween, producing a
// gentle perpetual oscillation. Slightly different durations keep
// the two blocks from staying in lockstep.
static void OnLogoBlueDone(void* ud) {
    UIWidget* w = (UIWidget*)ud;
    if (!w) return;
    float to = (w->rotation >= 0.0f) ? -5.0f : 5.0f;
    UIAnim_To(&w->rotation, to, 3500, UI_EASE_IN_OUT_CUBIC,
              OnLogoBlueDone, w);
}
static void OnLogoRedDone(void* ud) {
    UIWidget* w = (UIWidget*)ud;
    if (!w) return;
    float to = (w->rotation >= 0.0f) ? -7.0f : 7.0f;
    UIAnim_To(&w->rotation, to, 4000, UI_EASE_IN_OUT_CUBIC,
              OnLogoRedDone, w);
}

// Accent underline animation under the title: the rect's width grows
// from 0 to its target, holds for a beat, shrinks back, and the cycle
// repeats — same shape as the SVG <animate> in assets/banner.svg.
typedef struct {
    UIWidget* widget;
    float     fullWidth;
} AccentCtx;

static void AccentShrinkDone(void* ud);
static void AccentHoldDone  (void* ud);
static void AccentGrowDone  (void* ud);

static void AccentShrinkDone(void* ud) {
    AccentCtx* a = (AccentCtx*)ud;
    if (!a || !a->widget || !a->widget->width) return;
    UIAnim_To(a->widget->width, a->fullWidth, 1800,
              UI_EASE_OUT_CUBIC, AccentGrowDone, a);
}
static void AccentGrowDone(void* ud) {
    AccentCtx* a = (AccentCtx*)ud;
    if (!a || !a->widget || !a->widget->width) return;
    // Hold the full width for a beat before shrinking.
    UIAnim_To(a->widget->width, a->fullWidth, 1600,
              UI_EASE_LINEAR, AccentHoldDone, a);
}
static void AccentHoldDone(void* ud) {
    AccentCtx* a = (AccentCtx*)ud;
    if (!a || !a->widget || !a->widget->width) return;
    UIAnim_To(a->widget->width, 0.0f, 900,
              UI_EASE_IN_CUBIC, AccentShrinkDone, a);
}

// 1px slate-200 hairline used as a section divider.
static void BuildDivider(UIChildren* children, float x, float y, float w) {
    UIRectangle* r = UIRectangle_Create();
    UIRectangle_SetColor(r, C_BORDER);
    UIWidget* widget = widgcs(r, w, 1.0f);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
}

// Tiny uppercase section label drawn in the slate-400 tone.
static void BuildSectionLabel(UIChildren* children, const char* text,
                              float x, float y) {
    UIText* t = UIText_Create((char*)text, 11.0f);
    UIText_SetFontFamily(t, UIGetFont("Segoe UI"));
    UIText_SetFontStyle (t, Bold);
    UIText_SetColor     (t, C_TEXT_FAINT);
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
}

int main(void) {
    // Layout geometry. The card sits inside a small backdrop margin so
    // its drop shadow has room to breathe.
    const int   WIN_W      = 760;
    const int   WIN_H      = 620;
    const float PAD        = 24.0f;
    const float CARD_X     = PAD;
    const float CARD_Y     = PAD;
    const float CARD_W     = (float)WIN_W - 2.0f * PAD;
    const float CARD_H     = (float)WIN_H - 2.0f * PAD;
    const float CONTENT_X  = CARD_X + 32.0f;
    const float CONTENT_W  = CARD_W - 64.0f;

    UIApp* app = UIApp_Create("Mocida SDK installer", WIN_W, WIN_H);
    if (!app) return 1;

    UIApp_SetAppId(app, "Mocida.Installer");
    UIApp_SetTargetFPS(app, 60);
    UIApp_SetRenderQuality(app, UI_QUALITY_HIGH);
    // FXAA softens text edges across the entire frame; the analytic
    // coverage AA used by COVERAGE is already plenty for the shapes
    // here and keeps glyphs crisp.
    UIApp_SetAAMode      (app, UI_AA_COVERAGE);
    // The installer is a fixed-form dialog — locking the window keeps
    // the hand-positioned layout from being clipped or stretched.
    UIApp_SetResizable(app, 0);
    UISearchFonts();
    // Load the window icon straight from the embedded SVG bytes — no
    // disk lookup, no nearby assets/ folder required.
    if (g_logo_svg_len > 0) {
        SDL_Surface* iconSurf =
            UIAsset_LoadSurfaceFromMemory(g_logo_svg, g_logo_svg_len);
        if (iconSurf) {
            UIApp_SetWindowIconFromSurface(app, iconSurf);
            SDL_DestroySurface(iconSurf);
        }
    }

    g.app     = app;
    g.scope   = 0;
    g.isAdmin = IsProcessElevated();

    UIChildren* children = UIChildren_Create(32);

    // ----------------------------------------------------------------
    // Card surface with a soft drop shadow
    // ----------------------------------------------------------------
    UIRectangle* panel = UIRectangle_Create();
    UIRectangle_SetColor (panel, C_CARD);
    UIRectangle_SetRadius(panel, 16.0f);
    UIRectangle_SetShadow(panel, (UIShadow){
        .offsetX = 0.0f, .offsetY = 12.0f,
        .blur    = 32.0f, .spread = -8.0f,
        .color   = { 15, 23, 42, 0.15f }
    });
    UIWidget* panelW = widgcs(panel, CARD_W, CARD_H);
    UIWidget_SetPosition(panelW, CARD_X, CARD_Y);
    UIChildren_Add(children, panelW);

    // ----------------------------------------------------------------
    // Header: logo + title + subtitle
    // ----------------------------------------------------------------
    const float HEADER_Y   = CARD_Y + 28.0f;
    const float LOGO_SIZE  = 56.0f;

    // Animated Mocida mark: two overlapping rounded squares (Mocida
    // blue + Mocida red) that gently breathe via independent ping-pong
    // rotations. Same logic as the SVG banner used in the README.
    {
        const float BLOCK  = 40.0f;
        const float OFFSET = LOGO_SIZE - BLOCK;  // 16 px — half-overlap.

        // Subtle neutral shadow — colored glows looked muddy at this
        // small size. A short, low-opacity black drop is enough to
        // separate the blocks from the white card behind them.
        const UIShadow softShadow = (UIShadow){
            .offsetX = 0.0f, .offsetY = 2.0f,
            .blur    =  6.0f, .spread = -2.0f,
            .color   = { 15, 23, 42, 0.18f }
        };

        UIRectangle* blueR = UIRectangle_Create();
        UIRectangle_SetColor (blueR, C_BRAND_BLUE);
        UIRectangle_SetRadius(blueR, 6.0f);
        UIRectangle_SetShadow(blueR, softShadow);
        UIWidget* blueW = widgcs(blueR, BLOCK, BLOCK);
        UIWidget_SetPosition(blueW, CONTENT_X, HEADER_Y);
        UIChildren_Add(children, blueW);

        UIRectangle* redR = UIRectangle_Create();
        UIRectangle_SetColor (redR, C_BRAND_RED);
        UIRectangle_SetRadius(redR, 6.0f);
        UIRectangle_SetShadow(redR, softShadow);
        UIWidget* redW = widgcs(redR, BLOCK, BLOCK);
        UIWidget_SetPosition(redW, CONTENT_X + OFFSET, HEADER_Y + OFFSET);
        UIWidget_SetZIndex(redW, 1);    // draw on top of blue
        UIChildren_Add(children, redW);

        // Kick off the perpetual ping-pong rotation tweens. The blocks
        // rotate around their own centres, matching the SVG banner.
        blueW->rotation = -5.0f;
        UIAnim_To(&blueW->rotation, 5.0f, 3500,
                  UI_EASE_IN_OUT_CUBIC, OnLogoBlueDone, blueW);
        redW->rotation = 7.0f;
        UIAnim_To(&redW->rotation, -7.0f, 4000,
                  UI_EASE_IN_OUT_CUBIC, OnLogoRedDone, redW);
    }

    // Title aligned to the right of the logo.
    {
        UIText* title = UIText_Create((char*)"Mocida SDK installer", 24.0f);
        UIText_SetFontFamily(title, UIGetFont("Segoe UI"));
        UIText_SetFontStyle (title, Bold);
        UIText_SetColor     (title, C_TEXT_DARK);
        UIWidget* tW = widgc(title);
        UIWidget_SetPosition(tW, CONTENT_X + LOGO_SIZE + 18.0f, HEADER_Y - 2.0f);
        UIChildren_Add(children, tW);
    }
    {
        UIWidget* subW = BuildLabel(children,
            "Installs the Mocida headers, mocida.dll and mocida.lib, then\n"
            "publishes MOCIDA_INCLUDE_DIR / MOCIDA_LIB_DIR and updates PATH.",
            12.5f, C_TEXT_DIM,
            CONTENT_X + LOGO_SIZE + 18.0f, HEADER_Y + 30.0f);
        UIText* sub = (UIText*)subW->data;
        UIText_SetWrapMode    (sub, UI_WRAP_WORD);
        UIText_SetWrapToBounds(sub, 1);
        UIWidget_SetSize(subW, CONTENT_W - LOGO_SIZE - 18.0f, 40.0f);
    }

    // Animated brand-blue accent line under the subtitle — matches the
    // <animate> rect under the title in assets/banner.svg.
    {
        const float ACCENT_X = CONTENT_X + LOGO_SIZE + 18.0f;
        const float ACCENT_Y = HEADER_Y + 72.0f;
        const float ACCENT_MAX = 140.0f;
        UIRectangle* acc = UIRectangle_Create();
        UIRectangle_SetColor (acc, C_BRAND_BLUE);
        UIRectangle_SetRadius(acc, 1.5f);
        // Start width = 0 so the first tween grows from nothing.
        UIWidget* accW = widgcs(acc, 0.0f, 2.5f);
        UIWidget_SetPosition(accW, ACCENT_X, ACCENT_Y);
        UIChildren_Add(children, accW);

        // The AccentCtx is leaked intentionally — it lives for the
        // app's lifetime (the tween chain never stops).
        static AccentCtx s_accentCtx;
        s_accentCtx.widget    = accW;
        s_accentCtx.fullWidth = ACCENT_MAX;
        UIAnim_To(accW->width, ACCENT_MAX, 1800,
                  UI_EASE_OUT_CUBIC, AccentGrowDone, &s_accentCtx);
    }

    // Divider under the header.
    const float DIVIDER1_Y = HEADER_Y + LOGO_SIZE + 28.0f;
    BuildDivider(children, CONTENT_X, DIVIDER1_Y, CONTENT_W);

    // ----------------------------------------------------------------
    // Scope section
    // ----------------------------------------------------------------
    const float SCOPE_Y = DIVIDER1_Y + 22.0f;
    BuildSectionLabel(children, "INSTALL SCOPE", CONTENT_X, SCOPE_Y);

    const float SCOPE_BTN_Y = SCOPE_Y + 22.0f;
    g.userBtn = BuildButton(children, "User (recommended)", 15.5f,
                            C_PRIMARY, UI_COLOR_WHITE,
                            CONTENT_X, SCOPE_BTN_Y, 220.0f, 44.0f, OnUserClick);
    g.sysBtn  = BuildButton(children, "System (needs admin)", 15.5f,
                            C_MUTED, C_TEXT_MED,
                            CONTENT_X + 232.0f, SCOPE_BTN_Y, 220.0f,
                            44.0f, OnSystemClick);

    // The hint lives to the right of the scope buttons, constrained to
    // a narrow column so it never bleeds past the card.
    {
        UIWidget* hintW = BuildLabel(children,
            g.isAdmin
              ? "Elevated process.\nSystem scope is writable."
              : "Process not elevated.\nUser scope only.",
            11.5f, g.isAdmin ? C_OK : C_WARN,
            CONTENT_X + 470.0f, SCOPE_BTN_Y + 4.0f);
        UIText* hint = (UIText*)hintW->data;
        UIText_SetWrapMode    (hint, UI_WRAP_WORD);
        UIText_SetWrapToBounds(hint, 1);
        UIWidget_SetSize(hintW, CONTENT_W - 470.0f, 40.0f);
    }

    // ----------------------------------------------------------------
    // Path section
    // ----------------------------------------------------------------
    const float PATH_Y = SCOPE_BTN_Y + 66.0f;
    BuildSectionLabel(children, "INSTALL LOCATION", CONTENT_X, PATH_Y);

    char defaultPath[MAX_PATH];
    ExpandEnv(kUserDefault, defaultPath, sizeof(defaultPath));

    UITextField* pf = UITextField_Create(defaultPath, 13.5f);
    UITextField_SetFontFamily   (pf, UIGetFont("Segoe UI"));
    UITextField_SetPlaceholder  (pf, "C:\\path\\to\\mocida");
    UITextField_SetRadius       (pf, 8.0f);
    UITextField_SetBorder       (pf, C_BORDER, C_BRAND_BLUE, 1.0f);
    UITextField_SetPadding      (pf, 12.0f, 10.0f);
    UITextField_SetTextColor    (pf, C_TEXT_DARK);
    UITextField_SetPlaceholderColor(pf, C_TEXT_FAINT);
    UITextField_OnChange        (pf, OnPathChange, NULL);
    UIWidget* pfW = widgcs(pf, CONTENT_W - 110.0f, 44.0f);
    UIWidget_SetPosition(pfW, CONTENT_X, PATH_Y + 22.0f);
    UIChildren_Add(children, pfW);
    g.pathField = pf;

    BuildButton(children, "Browse...", 15.0f, C_MUTED, C_TEXT_MED,
                CONTENT_X + CONTENT_W - 100.0f, PATH_Y + 22.0f,
                100.0f, 44.0f, OnBrowse);

    // ----------------------------------------------------------------
    // Env-var preview section (code-block style)
    // ----------------------------------------------------------------
    const float ENV_Y = PATH_Y + 86.0f;
    BuildSectionLabel(children, "ENVIRONMENT VARIABLES", CONTENT_X, ENV_Y);

    // Subtle background panel so the var listing reads like a code block.
    UIRectangle* envBg = UIRectangle_Create();
    UIRectangle_SetColor      (envBg, C_SUBTLE_BG);
    UIRectangle_SetRadius     (envBg, 8.0f);
    UIRectangle_SetBorderWidth(envBg, 1.0f);
    UIRectangle_SetBorderColor(envBg, C_BORDER);
    UIWidget* envBgW = widgcs(envBg, CONTENT_W, 86.0f);
    UIWidget_SetPosition(envBgW, CONTENT_X, ENV_Y + 22.0f);
    UIChildren_Add(children, envBgW);

    UIText* envP = UIText_Create((char*)"", 12.5f);
    UIText_SetFontFamily   (envP, UIGetFont("Consolas"));
    UIText_SetColor        (envP, C_TEXT_MED);
    UIText_SetWrapMode     (envP, UI_WRAP_WORD);
    UIText_SetWrapToBounds (envP, 1);
    UIWidget* envPW = widgcs(envP, CONTENT_W - 24.0f, 70.0f);
    UIWidget_SetPosition(envPW, CONTENT_X + 14.0f, ENV_Y + 32.0f);
    UIChildren_Add(children, envPW);
    g.envPreview = envPW;

    // ----------------------------------------------------------------
    // Footer divider + progress bar + status + install button
    // ----------------------------------------------------------------
    const float FOOTER_DIV_Y = CARD_Y + CARD_H - 88.0f;
    BuildDivider(children, CONTENT_X, FOOTER_DIV_Y, CONTENT_W);

    // Slim progress bar that fills as the install advances. Hidden
    // until the Install button is pressed so the idle UI stays clean.
    // The bar is slightly narrower than the row to leave room for the
    // "XX%" label drawn to its right.
    const float PB_GAP    = 12.0f;
    const float PB_PCT_W  = 44.0f;
    const float PB_ROW_W  = CONTENT_W - 200.0f;
    const float PB_BAR_W  = PB_ROW_W - PB_GAP - PB_PCT_W;

    UIProgressBar* pb = UIProgressBar_Create(0.0f);
    UIProgressBar_SetColors(pb, C_BORDER, C_BRAND_BLUE);
    UIProgressBar_SetRadius(pb, 4.0f);
    UIWidget* pbW = widgcs(pb, PB_BAR_W, 8.0f);
    UIWidget_SetPosition(pbW, CONTENT_X, FOOTER_DIV_Y + 14.0f);
    UIWidget_SetVisible(pbW, 0);
    UIChildren_Add(children, pbW);
    g.progress  = pb;
    g.progressW = pbW;

    // Percent label sitting to the right of the bar, vertically
    // centred on the bar's mid-line.
    UIText* pct = UIText_Create((char*)"0%", 12.0f);
    UIText_SetFontFamily(pct, UIGetFont("Segoe UI"));
    UIText_SetFontStyle (pct, Bold);
    UIText_SetColor     (pct, C_TEXT_MED);
    UIText_SetHAlign    (pct, UI_TEXT_HALIGN_RIGHT);
    UIText_SetVAlign    (pct, UI_TEXT_VALIGN_CENTER);
    UIWidget* pctW = widgcs(pct, PB_PCT_W, 16.0f);
    UIWidget_SetPosition(pctW,
        CONTENT_X + PB_BAR_W + PB_GAP, FOOTER_DIV_Y + 10.0f);
    UIWidget_SetVisible(pctW, 0);
    UIChildren_Add(children, pctW);
    g.progressPctLabel = pctW;

    UIText* st = UIText_Create((char*)"Ready.", 12.5f);
    UIText_SetFontFamily   (st, UIGetFont("Segoe UI"));
    UIText_SetColor        (st, C_TEXT_DIM);
    UIText_SetWrapMode     (st, UI_WRAP_WORD);
    UIText_SetWrapToBounds (st, 1);
    UIText_SetVAlign       (st, UI_TEXT_VALIGN_TOP);
    // Slide status down a touch so the progress bar sits cleanly
    // above it without overlapping.
    UIWidget* stW = widgcs(st, CONTENT_W - 200.0f, 44.0f);
    UIWidget_SetPosition(stW, CONTENT_X, FOOTER_DIV_Y + 30.0f);
    UIChildren_Add(children, stW);
    g.statusLabel = stW;

    {
        UIButton* iBtn = UIButton_Create("Install", 18.0f);
        UIButton_SetFontFamily(iBtn, UIGetFont("Segoe UI"));
        UIButton_SetFontStyle (iBtn, Bold);
        UIButton_SetRadius    (iBtn, 10.0f);
        UIButton_SetColors    (iBtn, C_GREEN, UI_COLOR_WHITE);
        UIButton_SetShadow    (iBtn, (UIShadow){
            .offsetX = 0.0f, .offsetY = 4.0f,
            .blur    = 14.0f, .spread = -4.0f,
            .color   = { 0, 0, 0, 0.20f }
        });
        UIButton_OnClick(iBtn, OnInstall, NULL);
        UIWidget* iBtnW = widgcs(iBtn, 176.0f, 48.0f);
        UIWidget_SetPosition(iBtnW,
            CARD_X + CARD_W - 32.0f - 176.0f, FOOTER_DIV_Y + 20.0f);
        UIChildren_Add(children, iBtnW);
    }

    // First-time preview & status
    UpdateEnvPreview(defaultPath);
    SetStatus(g.isAdmin
                  ? "Ready. Pick a scope and install location."
                  : "Ready. User scope selected (process is not elevated).",
              C_TEXT_DIM);

    UIApp_SetChildren       (app, children);
    UIApp_SetBackgroundColor(app, C_BG);
    UIApp_ShowWindow        (app);
    UIApp_Run               (app);
    UIApp_Destroy           (app);
    return 0;
}
