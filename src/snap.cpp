#include "snap.h"
#include <dwmapi.h>
#include <algorithm>
#include <cwctype>

static bool TokenElevated(HANDLE process, bool assumeIfUnknown) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return assumeIfUnknown;
    TOKEN_ELEVATION e{};
    DWORD size = 0;
    bool elevated = assumeIfUnknown;
    if (GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size)) elevated = e.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

bool IsSelfElevated() {
    static int cached = -1;
    if (cached < 0) cached = TokenElevated(GetCurrentProcess(), false) ? 1 : 0;
    return cached == 1;
}

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring ProcessPath(HWND w) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t path[MAX_PATH];
    DWORD len = MAX_PATH;
    std::wstring out;
    if (QueryFullProcessImageNameW(p, 0, path, &len)) out = path;
    CloseHandle(p);
    return out;
}

std::wstring ProcessName(HWND w) {
    std::wstring name = ProcessPath(w);
    size_t slash = name.find_last_of(L'\\');
    return slash == std::wstring::npos ? name : name.substr(slash + 1);
}

// A non-elevated process can't move elevated windows (UIPI); leave those to Windows Snap.
static bool BlockedByElevation(HWND w) {
    if (IsSelfElevated()) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return true;
    bool elevated = TokenElevated(p, true);
    CloseHandle(p);
    return elevated;
}

bool IsSnappable(HWND w, const std::vector<std::wstring>& excluded) {
    if (!w || !IsWindowVisible(w) || IsIconic(w)) return false;
    if (GetAncestor(w, GA_ROOT) != w) return false;
    LONG style = GetWindowLongW(w, GWL_STYLE), ex = GetWindowLongW(w, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (ex & WS_EX_TOOLWINDOW)) return false;
    if (!(style & WS_THICKFRAME)) return false;  // not resizable

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;

    wchar_t cls[64];
    GetClassNameW(w, cls, 64);
    static const wchar_t* shell[] = { L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow" };
    for (auto s : shell)
        if (wcscmp(cls, s) == 0) return false;

    // Borderless fullscreen (games, video players, presentations).
    if (!IsZoomed(w)) {
        MONITORINFO mi{ sizeof(mi) };
        RECT r;
        GetWindowRect(w, &r);
        if (GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi) && !(style & WS_CAPTION) &&
            r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom)
            return false;
    }

    if (!excluded.empty()) {
        std::wstring name = Lower(ProcessName(w));
        for (auto& e : excluded)
            if (!e.empty() && name.find(e) != std::wstring::npos) return false;
    }
    return !BlockedByElevation(w);
}

RECT VisualRect(HWND w) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(w, &r);
    return r;
}

void PlaceWindow(HWND w, const RECT& target) {
    if (IsZoomed(w) || IsIconic(w)) ShowWindow(w, SW_RESTORE);
    HMONITOR before = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    // A second pass fixes the size after the window crosses to a monitor with another DPI.
    for (int pass = 0; pass < 2; ++pass) {
        RECT wr, fr = VisualRect(w);
        GetWindowRect(w, &wr);
        int l = fr.left - wr.left, t = fr.top - wr.top, r = wr.right - fr.right, b = wr.bottom - fr.bottom;
        SetWindowPos(w, nullptr, target.left - l, target.top - t,
                     (target.right - target.left) + l + r, (target.bottom - target.top) + t + b,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        if (MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST) == before) break;
        before = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    }
}
