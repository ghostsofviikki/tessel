// Tessel — Win+Arrow cycling through FancyZones zone combinations.
#include "app.h"
#include "dragsnap.h"
#include "editor.h"
#include "icon.h"
#include "layout.h"
#include "preview.h"
#include "settings.h"
#include "snap.h"
#include "store.h"
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <algorithm>
#include <map>
#include <climits>
#include <cstdio>
#include <cwctype>
#include <sstream>

static constexpr UINT WM_APP_SNAP = WM_APP + 1;
static constexpr UINT WM_APP_TRAY = WM_APP + 2;
static constexpr UINT WM_APP_FSCHANGE = WM_APP + 3;
static constexpr UINT WM_APP_SNAP_WINDOW = WM_APP + 4;  // test entry: wParam = dir | jump << 8, lParam = HWND
static constexpr UINT WM_APP_DRAG_TEST = WM_APP + 5;    // test entry: wParam = x | y << 16 | keepOpen << 31, lParam = HWND
static constexpr UINT_PTR TIMER_RELOAD = 1, TIMER_OVERLAY = 2;
enum { ID_SETTINGS = 100, ID_EXIT };
static constexpr int HOTKEY_EDITOR = 1;

HINSTANCE g_inst;
static HWND g_hwnd, g_overlay;
static HHOOK g_hook;
Config g_cfg;
LayoutDB g_db;
static std::string g_dbError;
std::vector<MonCtx> g_mons;
static std::string g_vd;
static std::wstring g_exeDir;
std::wstring g_iniPath;
static UINT WM_TASKBARCREATED;
static HICON g_icon;

// ---------- config ----------

static const char* kDefaultIni =
    "; Tessel settings. Saved changes are picked up automatically.\r\n"
    "[tessel]\r\n"
    "; Win+Arrow snapping on (1) or off (0)\r\n"
    "enabled=1\r\n"
    "; What Win+Left/Right does at the edge of a monitor: monitor (hop to the next monitor), wrap, stop\r\n"
    "edge_horizontal=monitor\r\n"
    "; Win+Up at the top row: maximize | stop.  Win+Down at the bottom row: minimize | stop\r\n"
    "up_at_top=maximize\r\n"
    "down_at_bottom=minimize\r\n"
    "; Largest combination, in cells per axis. 2 = singles, pairs and 2x2 blocks. 3 also allows full rows of 3.\r\n"
    "max_span=2\r\n"
    "; Win+Alt+Arrow jumps a whole zone at a time instead of a half step\r\n"
    "alt_jump=1\r\n"
    "; When a layout changes (gap, sizes, rows/columns), move snapped windows into the new zones\r\n"
    "reflow_windows=1\r\n"
    "; Move newly opened windows into the grid: the zone the app used last, otherwise the closest fit\r\n"
    "snap_new_windows=1\r\n"
    "remember_app_zones=1\r\n"
    "; While dragging a window, the right mouse button shows the zones; drop to snap\r\n"
    "drag_snap=1\r\n"
    "; Zone snapping: the area that snaps to two zones (gap) or four (corner) reaches this many px into the zones\r\n"
    "snap_area=1\r\n"
    "snap_area_px=20\r\n"
    "; Briefly highlight the target rectangle\r\n"
    "overlay=1\r\n"
    "overlay_ms=200\r\n"
    "; Zone color, used by the snap highlight and the layout editor (also settable from the tray menu)\r\n"
    "overlay_color=#0078D7\r\n"
    "overlay_alpha=90\r\n"
    "; Apps to ignore, ';'-separated, matched as part of the exe name (e.g. notepad;game.exe)\r\n"
    "excluded=\r\n"
    "; Folder with FancyZones' applied-layouts.json / custom-layouts.json. Empty = PowerToys default.\r\n"
    "layouts_dir=\r\n"
    "; Opens the layout editor on the monitor under the mouse. Modifiers win/ctrl/alt/shift + a key (letter, digit, `, f1-f12).\r\n"
    "editor_hotkey=win+shift+z\r\n";

static std::wstring IniStr(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048];
    GetPrivateProfileStringW(L"tessel", key, def, buf, 2048, g_iniPath.c_str());
    return buf;
}

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
}

static void LoadConfig() {
    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(g_iniPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD n;
            WriteFile(f, kDefaultIni, (DWORD)strlen(kDefaultIni), &n, nullptr);
            CloseHandle(f);
        }
    }
    Config c;
    c.enabled = GetPrivateProfileIntW(L"tessel", L"enabled", 1, g_iniPath.c_str()) != 0;
    c.edgeH =Lower(IniStr(L"edge_horizontal", L"monitor"));
    c.upAtTop = Lower(IniStr(L"up_at_top", L"maximize"));
    c.downAtBottom = Lower(IniStr(L"down_at_bottom", L"minimize"));
    c.maxSpan = std::max(1, (int)GetPrivateProfileIntW(L"tessel", L"max_span", 2, g_iniPath.c_str()));
    c.altJump = GetPrivateProfileIntW(L"tessel", L"alt_jump", 1, g_iniPath.c_str()) != 0;
    c.overlay = GetPrivateProfileIntW(L"tessel", L"overlay", 1, g_iniPath.c_str()) != 0;
    c.overlayMs = GetPrivateProfileIntW(L"tessel", L"overlay_ms", 200, g_iniPath.c_str());
    c.overlayAlpha = std::min(255, std::max(10, (int)GetPrivateProfileIntW(L"tessel", L"overlay_alpha", 90, g_iniPath.c_str())));
    std::wstring col = IniStr(L"overlay_color", L"#0078D7");
    if (col.size() == 7 && col[0] == L'#') {
        unsigned v = wcstoul(col.c_str() + 1, nullptr, 16);
        c.overlayColor = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    }
    std::wstringstream ex(Lower(IniStr(L"excluded", L"")));
    std::wstring item;
    while (std::getline(ex, item, L';'))
        if (!Trim(item).empty()) c.excluded.push_back(Trim(item));
    c.layoutsDir = Trim(IniStr(L"layouts_dir", L""));
    if (c.layoutsDir.empty()) c.layoutsDir = FancyZonesDir();
    c.editorHotkey = Lower(Trim(IniStr(L"editor_hotkey", L"win+shift+z")));
    c.reflow = GetPrivateProfileIntW(L"tessel", L"reflow_windows", 1, g_iniPath.c_str()) != 0;
    c.snapNew = GetPrivateProfileIntW(L"tessel", L"snap_new_windows", 1, g_iniPath.c_str()) != 0;
    c.rememberZones = GetPrivateProfileIntW(L"tessel", L"remember_app_zones", 1, g_iniPath.c_str()) != 0;
    c.dragSnap = GetPrivateProfileIntW(L"tessel", L"drag_snap", 1, g_iniPath.c_str()) != 0;
    c.snapArea = GetPrivateProfileIntW(L"tessel", L"snap_area", 1, g_iniPath.c_str()) != 0;
    c.snapAreaPx = std::clamp((int)GetPrivateProfileIntW(L"tessel", L"snap_area_px", 20, g_iniPath.c_str()), 0, 200);
    g_cfg = c;
}

// "win+shift+z" -> MOD_WIN | MOD_SHIFT, 'Z'
static bool ParseHotkey(const std::wstring& s, UINT& mods, UINT& vk) {
    mods = MOD_NOREPEAT;
    vk = 0;
    std::wstringstream ss(s);
    std::wstring part;
    while (std::getline(ss, part, L'+')) {
        part = Trim(part);
        if (part == L"win") mods |= MOD_WIN;
        else if (part == L"ctrl") mods |= MOD_CONTROL;
        else if (part == L"alt") mods |= MOD_ALT;
        else if (part == L"shift") mods |= MOD_SHIFT;
        else if (part.size() == 1 && iswalnum(part[0])) vk = towupper(part[0]);
        else if (part == L"`") vk = VK_OEM_3;
        else if (part.size() >= 2 && part[0] == L'f' && _wtoi(part.c_str() + 1) >= 1 && _wtoi(part.c_str() + 1) <= 12) vk = VK_F1 + _wtoi(part.c_str() + 1) - 1;
        else return false;
    }
    return vk != 0;
}

static std::wstring g_hotkeyRegistered;

static void RegisterEditorHotkey() {
    if (g_hotkeyRegistered == g_cfg.editorHotkey) return;
    UnregisterHotKey(g_hwnd, HOTKEY_EDITOR);
    g_hotkeyRegistered = g_cfg.editorHotkey;
    UINT mods, vk;
    if (!g_cfg.editorHotkey.empty() && ParseHotkey(g_cfg.editorHotkey, mods, vk)) RegisterHotKey(g_hwnd, HOTKEY_EDITOR, mods, vk);
}

// ---------- tray ----------

static void UpdateTray(DWORD msg) {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_APP_TRAY;
    if (g_icon) DestroyIcon(g_icon);
    g_icon = CreateMarkIcon(GetSystemMetrics(SM_CXSMICON), g_cfg.enabled ? TaskbarMarkColor() : RGB(128, 128, 128));
    nid.hIcon = g_icon;
    int ok = 0;
    for (auto& m : g_mons) ok += m.ok;
    std::wstring tip = !g_cfg.enabled ? L"Tessel (snapping off)" : L"Tessel - " + std::to_wstring(ok) + L" of " + std::to_wstring(g_mons.size()) + L" monitors zoned";
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(msg, &nid);
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static std::wstring ExePath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"Tessel", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, L"Tessel", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"Tessel");
    }
    RegCloseKey(k);
}

// ---------- state ----------

struct SnappedWindow { HWND w; size_t mon; int combo; };  // mon / combo index into the layout before a reload
static std::vector<SnappedWindow> CaptureSnapped();
static void Reflow(const std::vector<SnappedWindow>& before, const std::vector<MonCtx>& oldMons);

void Reload() {
    std::vector<MonCtx> oldMons = g_mons;
    std::vector<SnappedWindow> snapped = g_cfg.reflow ? CaptureSnapped() : std::vector<SnappedWindow>{};
    LoadConfig();
    g_dbError.clear();
    LoadLayoutDB(g_cfg.layoutsDir, g_db, g_dbError);
    g_vd = CurrentVirtualDesktop();
    g_mons = BuildMonitors(g_db, g_vd, g_cfg.maxSpan);
    if (!snapped.empty()) Reflow(snapped, oldMons);
    if (g_hwnd) {
        UpdateTray(NIM_MODIFY);
        RegisterEditorHotkey();
        SettingsRefresh();
    }
}

void SetSetting(const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(L"tessel", key, value.c_str(), g_iniPath.c_str());
    Reload();
}

// ---------- layout editing ----------

std::wstring Widen(const std::string& s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), (int)w.size());
    return w;
}

void SaveZoneColor(COLORREF c) {
    wchar_t v[16];
    swprintf_s(v, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    WritePrivateProfileStringW(L"tessel", L"overlay_color", v, g_iniPath.c_str());
    g_cfg.overlayColor = c;
}

const CustomLayout* AppliedCustom(const MonCtx& m) {
    if (m.type != "custom") return nullptr;
    for (auto& l : g_db.custom)
        if (l.uuid == m.uuid) return &l;
    return nullptr;
}

static std::string FreshLayoutName() {
    for (int n = (int)g_db.custom.size() + 1;; ++n) {
        std::string name = "Tessel layout " + std::to_string(n);
        bool taken = false;
        for (auto& l : g_db.custom) taken |= l.name == name;
        if (!taken) return name;
    }
}

void OpenEditorFor(size_t mi, bool fresh) {
    if (mi >= g_mons.size()) return;
    const MonCtx& m = g_mons[mi];
    EditorRequest req;
    req.mon = m;
    req.spacing = m.spacing;
    req.color = g_cfg.overlayColor;
    const CustomLayout* cur = fresh ? nullptr : AppliedCustom(m);
    if (cur && !cur->isGrid) {
        MessageBoxW(nullptr, L"This monitor uses a canvas layout, which Tessel can't edit.\nUse the FancyZones editor, or pick a grid layout or template in Tessel settings.",
                    L"Tessel", MB_ICONINFORMATION);
        return;
    }
    if (cur) {
        req.uuid = cur->uuid;
        req.name = cur->name;
        req.grid = cur->grid;
    } else {
        // New layout: start from the current template's shape, or a 2x2 grid.
        req.name = FreshLayoutName();
        int n = (int)m.zones.size();
        if (!fresh && m.type == "columns" && n > 0) req.grid = EvenGrid(1, n);
        else if (!fresh && m.type == "rows" && n > 0) req.grid = EvenGrid(n, 1);
        else req.grid = EvenGrid(2, 2);
        if (fresh || !m.ok) req.spacing = 16;
    }
    std::string vd = g_vd;
    OpenEditor(g_inst, req, [vd](const EditorRequest& r) {
        if (r.color != g_cfg.overlayColor) SaveZoneColor(r.color);
        std::string uuid = r.uuid, err;
        if (!SaveGridLayout(g_cfg.layoutsDir, r.mon, vd, uuid, r.name, r.grid, r.spacing, err))
            MessageBoxW(nullptr, (L"Could not save the layout: " + Widen(err)).c_str(), L"Tessel", MB_ICONERROR);
        Reload();
    });
}

void ApplyLayoutFor(size_t mi, size_t li) {
    if (mi >= g_mons.size() || li >= g_db.custom.size()) return;
    std::string err;
    if (!ApplyCustomLayout(g_cfg.layoutsDir, g_mons[mi], g_vd, g_db.custom[li].uuid, err))
        MessageBoxW(nullptr, (L"Could not apply the layout: " + Widen(err)).c_str(), L"Tessel", MB_ICONERROR);
    Reload();
}

// Applies a ready-made layout: reuses an unchanged copy made earlier, otherwise saves a new custom layout.
void ApplyTemplateFor(size_t mi, size_t ti) {
    const auto& templates = LayoutTemplates();
    if (mi >= g_mons.size() || ti >= templates.size()) return;
    const LayoutTemplate& t = templates[ti];
    for (size_t li = 0; li < g_db.custom.size(); ++li)
        if (g_db.custom[li].isGrid && g_db.custom[li].name == t.name && SameGrid(g_db.custom[li].grid, t.grid)) {
            ApplyLayoutFor(mi, li);
            return;
        }
    std::string name = t.name;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (auto& l : g_db.custom) taken |= l.name == name;
        if (!taken) break;
        name = t.name + " " + std::to_string(n);
    }
    const MonCtx& m = g_mons[mi];
    std::string uuid, err;
    if (!SaveGridLayout(g_cfg.layoutsDir, m, g_vd, uuid, name, t.grid, m.ok ? m.spacing : 16, err))
        MessageBoxW(nullptr, (L"Could not apply the layout: " + Widen(err)).c_str(), L"Tessel", MB_ICONERROR);
    Reload();
}

void DeleteLayout(size_t li, HWND owner) {
    if (li >= g_db.custom.size()) return;
    std::wstring name = Widen(g_db.custom[li].name);
    if (MessageBoxW(owner, (L"Delete the layout \"" + name + L"\"?").c_str(), L"Tessel", MB_ICONQUESTION | MB_OKCANCEL) != IDOK) return;
    std::string err;
    if (!DeleteCustomLayout(g_cfg.layoutsDir, g_db.custom[li].uuid, err))
        MessageBoxW(owner, (L"Could not delete \"" + name + L"\": " + Widen(err) + L".").c_str(), L"Tessel", MB_ICONINFORMATION);
    Reload();
}

size_t MonitorUnderCursor() {
    POINT p;
    GetCursorPos(&p);
    HMONITOR h = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    for (size_t i = 0; i < g_mons.size(); ++i)
        if (g_mons[i].h == h) return i;
    return 0;
}

static MonCtx* CtxFor(HMONITOR h) {
    std::string vd = CurrentVirtualDesktop();
    if (vd != g_vd) {
        g_vd = vd;
        g_mons = BuildMonitors(g_db, g_vd, g_cfg.maxSpan);
    }
    for (auto& m : g_mons)
        if (m.h == h) return &m;
    return nullptr;
}

static MonCtx* CtxForWindow(HWND w) {
    MonCtx* m = CtxFor(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST));
    return m && m->ok ? m : nullptr;
}

static bool ShouldHandle(HWND w) {
    return IsSnappable(w, g_cfg.excluded) && CtxForWindow(w) != nullptr;
}

// ---------- overlay ----------

static LRESULT CALLBACK OverlayProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        HBRUSH fill = CreateSolidBrush(g_cfg.overlayColor);
        FillRect(dc, &r, fill);
        DeleteObject(fill);
        HBRUSH border = CreateSolidBrush(RGB(255, 255, 255));
        for (int i = 0; i < 3; ++i) {
            FrameRect(dc, &r, border);
            InflateRect(&r, -1, -1);
        }
        DeleteObject(border);
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static void Flash(const RECT& r) {
    if (!g_cfg.overlay || !g_overlay) return;
    SetLayeredWindowAttributes(g_overlay, 0, (BYTE)g_cfg.overlayAlpha, LWA_ALPHA);
    SetWindowPos(g_overlay, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_overlay, nullptr, TRUE);
    SetTimer(g_hwnd, TIMER_OVERLAY, g_cfg.overlayMs, nullptr);
}

// ---------- snapping ----------

static const wchar_t* kPropCombo = L"Tessel.Combo";
static const wchar_t* kPropMon = L"Tessel.Monitor";

static INT_PTR PackCombo(const Combo& c) {
    return (INT_PTR)((c.x.a & 0x7F) | (c.x.b & 0x7F) << 7 | (c.y.a & 0x7F) << 14 | (c.y.b & 0x7F) << 21 | 1 << 28);
}

// ---------- per-app zone history ----------
// history.ini in %LOCALAPPDATA%\Tessel: exe name = monitor|instance|layout uuid|zone ids

static std::wstring g_historyPath;
static std::map<std::wstring, std::wstring> g_historyCache;  // skips rewriting unchanged entries

// The exe name, not the path: Store apps live in versioned folders that change with every update.
static std::wstring HistoryKey(HWND w) { return Lower(ProcessName(w)); }

static void RecordZone(HWND w, const MonCtx& m, const Combo& c) {
    std::wstring key = HistoryKey(w);
    if (key.empty() || g_historyPath.empty()) return;
    std::wstring v = Widen(m.name) + L"|" + Widen(m.instance) + L"|" + Widen(m.uuid) + L"|";
    for (size_t i = 0; i < c.zones.size(); ++i) v += (i ? L"," : L"") + std::to_wstring(c.zones[i]);
    auto it = g_historyCache.find(key);
    if (it != g_historyCache.end() && it->second == v) return;
    g_historyCache[key] = v;
    WritePrivateProfileStringW(L"history", key.c_str(), v.c_str(), g_historyPath.c_str());
}

// The zone combination this app was last in, if that monitor still shows the same layout.
static bool LastZone(HWND w, MonCtx*& mon, int& combo) {
    std::wstring key = HistoryKey(w);
    if (key.empty()) return false;
    wchar_t buf[512];
    GetPrivateProfileStringW(L"history", key.c_str(), L"", buf, 512, g_historyPath.c_str());
    std::vector<std::wstring> parts;
    std::wstringstream ss(buf);
    std::wstring part;
    while (std::getline(ss, part, L'|')) parts.push_back(part);
    if (parts.size() != 4) return false;
    std::vector<int> ids;
    std::wstringstream zs(parts[3]);
    while (std::getline(zs, part, L',')) ids.push_back(_wtoi(part.c_str()));
    for (auto& m : g_mons) {
        if (!m.ok || Widen(m.name) != parts[0] || Widen(m.instance) != parts[1] || Widen(m.uuid) != parts[2]) continue;
        for (int k = 0; k < (int)m.grid.combos.size(); ++k)
            if (m.grid.combos[k].zones == ids) {
                mon = &m;
                combo = k;
                return true;
            }
    }
    return false;
}

// Remembers where a window was put: as a hint for the next keypress, and for the app's next window.
static void MarkSnapped(HWND w, const MonCtx& m, const Combo& c) {
    SetPropW(w, kPropCombo, (HANDLE)PackCombo(c));
    SetPropW(w, kPropMon, (HANDLE)m.h);
    RecordZone(w, m, c);
}

// Which combination the window is in now. Geometry wins over the stored hint,
// so a window moved by mouse is picked up correctly.
static int CurrentCombo(HWND w, const MonCtx& m) {
    RECT vis = VisualRect(w);
    double bestIoU = 0;
    int best = MatchCombo(m.grid, vis, &bestIoU);
    int hint = -1;
    double hintIoU = 0;
    INT_PTR v = (INT_PTR)GetPropW(w, kPropCombo);
    if (v && (HMONITOR)GetPropW(w, kPropMon) == m.h) {
        Span x{ (int)(v & 0x7F), (int)((v >> 7) & 0x7F) }, y{ (int)((v >> 14) & 0x7F), (int)((v >> 21) & 0x7F) };
        hint = FindCombo(m.grid, x, y);
        if (hint >= 0) hintIoU = IoU(m.grid.combos[hint].actual, vis);
    }
    // A window with a minimum size larger than its zone only matches through the hint.
    if (hint >= 0 && hintIoU >= 0.5 && hintIoU >= bestIoU - 0.05) return hint;
    if (bestIoU >= 0.85) return best;
    if (hint >= 0 && hintIoU >= 0.5) return hint;
    return -1;
}

static MonCtx* Neighbor(const MonCtx& from, Dir d) {
    MonCtx* best = nullptr;
    long long bestScore = LLONG_MAX;
    long long fx = (from.full.left + from.full.right) / 2, fy = (from.full.top + from.full.bottom) / 2;
    for (auto& m : g_mons) {
        if (&m == &from || !m.ok) continue;
        long long x = (m.full.left + m.full.right) / 2, y = (m.full.top + m.full.bottom) / 2;
        long long along = d == Dir::Right ? x - fx : d == Dir::Left ? fx - x : d == Dir::Down ? y - fy : fy - y;
        long long across = (d == Dir::Left || d == Dir::Right) ? std::llabs(y - fy) : std::llabs(x - fx);
        if (along <= 0) continue;
        long long score = along + 2 * across;
        if (score < bestScore) { bestScore = score; best = &m; }
    }
    return best;
}

static void SendDummyKey() {
    // Without this, releasing Win after a swallowed arrow opens the Start menu.
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = 0xE8;  // unassigned VK
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

static void HandleSnap(HWND w, Dir d, bool jump) {
    if (!ShouldHandle(w)) return;
    MonCtx* m = CtxForWindow(w);
    MonCtx* tm = m;
    int cur = CurrentCombo(w, *m);
    int target = -1;

    if (IsZoomed(w)) {
        if (d == Dir::Up) return;
        if (d == Dir::Down) target = cur >= 0 ? cur : ZoneComboAt(m->grid, POINT{ (m->work.left + m->work.right) / 2, (m->work.top + m->work.bottom) / 2 });
    }
    if (target < 0 && cur < 0) {
        RECT r = VisualRect(w);
        target = ZoneComboAt(m->grid, POINT{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 });
    } else if (target < 0) {
        target = StepCombo(m->grid, cur, d, jump);
        if (target < 0) {
            bool horiz = d == Dir::Left || d == Dir::Right;
            double n0, n1;
            CrossSpan(m->grid, cur, d, n0, n1);
            if (horiz && g_cfg.edgeH == L"monitor") {
                if (MonCtx* nm = Neighbor(*m, d)) {
                    tm = nm;
                    target = EntryCombo(nm->grid, d, n0, n1);
                }
            } else if (horiz && g_cfg.edgeH == L"wrap") {
                target = EntryCombo(m->grid, d, n0, n1);
            } else if (d == Dir::Up && g_cfg.upAtTop == L"maximize") {
                ShowWindow(w, SW_MAXIMIZE);
                return;
            } else if (d == Dir::Down && g_cfg.downAtBottom == L"minimize") {
                ShowWindow(w, SW_MINIMIZE);
                return;
            }
        }
    }
    if (target < 0) return;
    const Combo& c = tm->grid.combos[target];
    PlaceWindow(w, c.actual);
    MarkSnapped(w, *tm, c);
    Flash(c.actual);
}

// ---------- reflow after layout changes ----------

static BOOL CALLBACK CaptureProc(HWND w, LPARAM lp) {
    auto* out = (std::vector<SnappedWindow>*)lp;
    if (IsZoomed(w) || !IsSnappable(w, g_cfg.excluded)) return TRUE;
    HMONITOR h = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    for (size_t i = 0; i < g_mons.size(); ++i) {
        if (g_mons[i].h != h || !g_mons[i].ok) continue;
        int c = CurrentCombo(w, g_mons[i]);
        if (c >= 0) out->push_back(SnappedWindow{ w, i, c });
        break;
    }
    return TRUE;
}

// Windows that sit in a zone or combination right now, on the current layout.
static std::vector<SnappedWindow> CaptureSnapped() {
    std::vector<SnappedWindow> out;
    if (!g_mons.empty()) EnumWindows(CaptureProc, (LPARAM)&out);
    return out;
}

static bool SameZones(const MonCtx& a, const MonCtx& b) {
    if (a.zones.size() != b.zones.size()) return false;
    for (size_t i = 0; i < a.zones.size(); ++i)
        if (a.zones[i].id != b.zones[i].id || !EqualRect(&a.zones[i].actual, &b.zones[i].actual)) return false;
    return true;
}

// Moves windows captured before a reload into the matching zones of the new layout.
static void Reflow(const std::vector<SnappedWindow>& before, const std::vector<MonCtx>& oldMons) {
    for (auto& s : before) {
        if (!IsWindow(s.w) || IsZoomed(s.w) || IsIconic(s.w)) continue;
        const MonCtx& om = oldMons[s.mon];
        const MonCtx* nm = nullptr;
        for (auto& m : g_mons)
            if (m.ok && m.device == om.device && m.name == om.name && m.instance == om.instance) nm = &m;
        if (!nm || SameZones(om, *nm)) continue;
        int t = MapCombo(om.grid, s.combo, nm->grid);
        if (t < 0) continue;
        const Combo& c = nm->grid.combos[t];
        PlaceWindow(s.w, c.actual);
        MarkSnapped(s.w, *nm, c);
    }
}

// ---------- new windows ----------

static const wchar_t* kPropSeen = L"Tessel.Seen";
static constexpr UINT kNewWindowDelayMs = 250;  // let the app finish placing itself first

static void HandleNewWindow(HWND w) {
    if (!g_cfg.enabled || !g_cfg.snapNew || !IsWindow(w) || GetPropW(w, kPropSeen)) return;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (!IsWindowVisible(w) || cloaked) return;  // shown for real later (uncloak) — check again then
    SetPropW(w, kPropSeen, (HANDLE)1);           // only ever adjust a window once
    // Dialogs are owned by their app window; maximized windows keep their choice.
    if (IsZoomed(w) || GetWindow(w, GW_OWNER) || !IsSnappable(w, g_cfg.excluded)) return;

    MonCtx* m = nullptr;
    int target = -1;
    if (g_cfg.rememberZones) LastZone(w, m, target);
    if (target < 0) {
        m = CtxForWindow(w);
        if (!m || CurrentCombo(w, *m) >= 0) return;  // no layout here, or already in a zone
        RECT r = VisualRect(w);
        double iou = 0;
        target = MatchCombo(m->grid, r, &iou);
        if (iou < 0.05) target = ZoneComboAt(m->grid, POINT{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 });
    }
    if (target < 0) return;
    const Combo& c = m->grid.combos[target];
    PlaceWindow(w, c.actual);
    MarkSnapped(w, *m, c);
}

static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD ev, HWND w, LONG obj, LONG child, DWORD, DWORD) {
    if (!w || obj != OBJID_WINDOW || child != CHILDID_SELF || GetAncestor(w, GA_ROOT) != w) return;
    if (ev == EVENT_SYSTEM_MOVESIZESTART) {
        if (g_cfg.enabled) DragSnapStart(w);
        return;
    }
    if (ev == EVENT_SYSTEM_MOVESIZEEND) {
        DragSnapEnd(w);  // snaps if the zones were up
        // A window dragged into a zone by mouse counts as that app's last zone too.
        if (MonCtx* m = CtxForWindow(w)) {
            int c = CurrentCombo(w, *m);
            if (c >= 0) MarkSnapped(w, *m, m->grid.combos[c]);
        }
        return;
    }
    if (g_cfg.snapNew && !GetPropW(w, kPropSeen)) SetTimer(g_hwnd, (UINT_PTR)w, kNewWindowDelayMs, nullptr);  // timer id = window
}

static LRESULT CALLBACK KeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_cfg.enabled && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        auto* k = (KBDLLHOOKSTRUCT*)lp;
        auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        int dir = -1;
        switch (k->vkCode) {
        case VK_LEFT: dir = (int)Dir::Left; break;
        case VK_RIGHT: dir = (int)Dir::Right; break;
        case VK_UP: dir = (int)Dir::Up; break;
        case VK_DOWN: dir = (int)Dir::Down; break;
        }
        if (dir >= 0 && !(k->flags & LLKHF_INJECTED) && (down(VK_LWIN) || down(VK_RWIN)) && !down(VK_SHIFT) && !down(VK_CONTROL)) {
            bool alt = down(VK_MENU);
            if ((!alt || g_cfg.altJump) && ShouldHandle(GetForegroundWindow())) {
                PostMessageW(g_hwnd, WM_APP_SNAP, dir, alt);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

// ---------- dump ----------

static std::string RectStr(const RECT& r) {
    char b[96];
    sprintf_s(b, "(%ld,%ld)-(%ld,%ld) %ldx%ld", r.left, r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top);
    return b;
}

static std::string DumpText() {
    std::ostringstream o;
    std::string dir;
    for (wchar_t c : g_cfg.layoutsDir) dir += (char)c;
    o << "Tessel zone dump\r\nlayouts: " << dir << "\r\n";
    if (!g_dbError.empty()) o << "error: " << g_dbError << "\r\n";
    o << "virtual desktop: " << g_vd << "\r\ncustom layouts: " << g_db.custom.size() << ", applied entries: " << g_db.applied.size() << "\r\n";
    o << "elevated: " << (IsSelfElevated() ? "yes" : "no") << ", max_span: " << g_cfg.maxSpan << "\r\n\r\n";
    for (auto& m : g_mons) {
        std::string dev;
        for (wchar_t c : m.device) dev += (char)c;
        o << "monitor " << dev << "  " << m.name << " / " << m.instance << "\r\n";
        o << "  work area " << RectStr(m.work) << "\r\n";
        o << "  layout: " << (m.layoutName.empty() ? "-" : m.layoutName) << "  [" << m.status << "]\r\n";
        for (auto& z : m.zones) o << "  zone " << z.id << ": " << RectStr(z.actual) << "\r\n";
        o << "  " << m.grid.combos.size() << " combinations:\r\n";
        for (auto& c : m.grid.combos) {
            o << "    x" << c.x.a << "-" << c.x.b << " y" << c.y.a << "-" << c.y.b << "  zones {";
            for (size_t i = 0; i < c.zones.size(); ++i) o << (i ? "," : "") << c.zones[i];
            o << "}  " << RectStr(c.actual) << "\r\n";
        }
        o << "\r\n";
    }
    return o.str();
}

static std::wstring WriteDump() {
    std::wstring path = g_exeDir + L"\\tessel-dump.txt";
    std::string text = DumpText();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n;
        WriteFile(f, text.data(), (DWORD)text.size(), &n, nullptr);
        CloseHandle(f);
    }
    return path;
}

// ---------- window ----------

static DWORD WINAPI WatchThread(LPVOID) {
    HANDLE h[2];
    DWORD count = 0;
    const DWORD filter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME;
    for (const std::wstring& dir : { g_cfg.layoutsDir, g_exeDir }) {
        HANDLE x = FindFirstChangeNotificationW(dir.c_str(), FALSE, filter);
        if (x != INVALID_HANDLE_VALUE) h[count++] = x;
    }
    if (!count) return 0;
    for (;;) {
        DWORD r = WaitForMultipleObjects(count, h, FALSE, INFINITE);
        if (r >= WAIT_OBJECT_0 + count) return 0;
        PostMessageW(g_hwnd, WM_APP_FSCHANGE, 0, 0);
        FindNextChangeNotification(h[r - WAIT_OBJECT_0]);
    }
}

static void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, ID_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_SNAP:
        SendDummyKey();
        HandleSnap(GetForegroundWindow(), (Dir)wp, lp != 0);
        return 0;
    case WM_APP_SNAP_WINDOW:
        HandleSnap((HWND)lp, (Dir)(wp & 0xFF), (wp >> 8) != 0);
        return 0;
    case WM_APP_DRAG_TEST:
        DragSnapTest((HWND)lp, POINT{ (LONG)(wp & 0x7FFF), (LONG)((wp >> 16) & 0x7FFF) }, (wp >> 31) & 1);
        return 0;
    case WM_APP_FSCHANGE:
    case WM_DISPLAYCHANGE:
        SetTimer(w, TIMER_RELOAD, 400, nullptr);  // debounce bursts of writes
        return 0;
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA) SetTimer(w, TIMER_RELOAD, 400, nullptr);
        if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {  // light/dark theme switched
            UpdateTray(NIM_MODIFY);
            SettingsRefresh();
        }
        return 0;
    case WM_TIMER:
        KillTimer(w, wp);
        if (wp == TIMER_RELOAD) Reload();
        else if (wp == TIMER_OVERLAY) ShowWindow(g_overlay, SW_HIDE);
        else HandleNewWindow((HWND)wp);
        return 0;
    case WM_APP_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP) ShowSettings();
        else if (LOWORD(lp) == WM_RBUTTONUP) ShowMenu();
        return 0;
    case WM_HOTKEY:
        if (wp == HOTKEY_EDITOR && g_cfg.enabled) OpenEditorFor(MonitorUnderCursor(), false);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_SETTINGS: ShowSettings(); break;
        case ID_EXIT: DestroyWindow(w); break;
        }
        return 0;
    case WM_DESTROY: {
        CloseSettings();
        NOTIFYICONDATAW nid{ sizeof(nid) };
        nid.hWnd = w;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    }
    if (msg == WM_TASKBARCREATED) {
        UpdateTray(NIM_ADD);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmd, int) {
    g_inst = inst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring exe = ExePath();
    g_exeDir = exe.substr(0, exe.find_last_of(L'\\'));
    g_iniPath = g_exeDir + L"\\tessel.ini";
    // Kept out of the watched exe folder: it changes on every snap.
    if (wchar_t* local = nullptr; SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        std::wstring dir = std::wstring(local) + L"\\Tessel";
        CreateDirectoryW(dir.c_str(), nullptr);
        g_historyPath = dir + L"\\history.ini";
        CoTaskMemFree(local);
    }

    if (wcsstr(cmd, L"--dump")) {
        Reload();
        std::wstring path = WriteDump();
        std::string text = DumpText();
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!out || out == INVALID_HANDLE_VALUE) {
            if (AttachConsole(ATTACH_PARENT_PROCESS)) out = GetStdHandle(STD_OUTPUT_HANDLE);
        }
        DWORD n;
        if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text.data(), (DWORD)text.size(), &n, nullptr);
        return 0;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Tessel.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Tessel is already running (see the tray).", L"Tessel", MB_ICONINFORMATION);
        return 0;
    }

    Reload();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"Tessel.Main";
    RegisterClassW(&wc);
    WNDCLASSW oc{};
    oc.lpfnWndProc = OverlayProc;
    oc.hInstance = inst;
    oc.lpszClassName = L"Tessel.Overlay";
    oc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&oc);

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Tessel", 0, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                oc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    UpdateTray(NIM_ADD);
    RegisterEditorHotkey();

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, inst, 0);
    if (!g_hook) {
        MessageBoxW(nullptr, L"Could not install the keyboard hook.", L"Tessel", MB_ICONERROR);
        return 1;
    }
    CloseHandle(CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr));
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, WinEventProc, 0, 0, flags);
    SetWinEventHook(EVENT_OBJECT_UNCLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, WinEventProc, 0, 0, flags);
    SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, WinEventProc, 0, 0, flags);
    DragSnapInit(inst, [](HWND w, const MonCtx& m, int combo) {
        const Combo& c = m.grid.combos[combo];
        PlaceWindow(w, c.actual);
        MarkSnapped(w, m, c);
    });

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    UnhookWindowsHookEx(g_hook);
    if (mutex) CloseHandle(mutex);
    return 0;
}
