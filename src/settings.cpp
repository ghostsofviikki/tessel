#include "settings.h"
#include "app.h"
#include "editor.h"
#include "icon.h"
#include "preview.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace {

enum Kind { W_SECTION, W_TEXT, W_DIM, W_TOGGLE, W_SEG, W_BUTTON, W_PRIMARY, W_SWATCH, W_THUMB, W_LINK, W_VALUE };

enum Id {
    ID_NONE = -1,
    ID_TAB = 100,          // + monitor index
    ID_CUSTOM = 200,       // + custom layout index
    ID_TEMPLATE = 500,     // + template index
    ID_EDIT = 700, ID_NEW, ID_SHOW, ID_COLOR,
    ID_T_ENABLED = 720, ID_T_AUTOSTART, ID_T_HIGHLIGHT, ID_T_REFLOW, ID_T_ALTJUMP, ID_T_NEWWIN, ID_T_LASTZONE, ID_T_DRAG, ID_T_AREA, ID_AREA_DEC, ID_AREA_INC,
    ID_SEG_EDGE = 800,     // + option (groups of 10)
    ID_SEG_UP = 810, ID_SEG_DOWN = 820, ID_SEG_SPAN = 830,
    ID_LINK_INI = 900, ID_LINK_FOLDER,
    MENU_APPLY = 1, MENU_EDIT, MENU_DELETE,
};

struct Widget {
    RECT r;
    int id;
    Kind kind;
    std::wstring text;
    bool on = false;      // toggle state, selected segment, current layout
    bool gallery = false; // lives in the scrolling gallery
};

struct Palette {
    COLORREF bg, card, text, dim, border, ctrl, ctrlHot, thumb, accent, onAccent;
};
const Palette kDark{ RGB(32, 32, 32), RGB(44, 44, 44), RGB(255, 255, 255), RGB(170, 170, 170), RGB(64, 64, 64),
                     RGB(58, 58, 58), RGB(74, 74, 74), RGB(28, 28, 28), RGB(0, 120, 215), RGB(255, 255, 255) };
const Palette kLight{ RGB(243, 243, 243), RGB(255, 255, 255), RGB(27, 27, 27), RGB(96, 96, 96), RGB(225, 225, 225),
                      RGB(240, 240, 240), RGB(226, 226, 226), RGB(232, 232, 232), RGB(0, 103, 192), RGB(255, 255, 255) };

constexpr int kW = 1000, kH = 760;  // client size in 96-dpi units

HWND s_wnd;
double s_scale = 1;
bool s_dark;
size_t s_mon;
int s_scroll, s_hover = ID_NONE;
HICON s_iconSmall, s_iconBig;

const Palette& P() { return s_dark ? kDark : kLight; }
int S(double v) { return (int)(v * s_scale + 0.5); }

bool AppsUseDarkTheme() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

COLORREF Mix(COLORREF a, COLORREF b, double t) {
    auto ch = [&](int x, int y) { return (BYTE)(x + (y - x) * t + 0.5); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

// ---------- layout of the window ----------

RECT R(int x, int y, int w, int h) { return RECT{ S(x), S(y), S(x + w), S(y + h) }; }

RECT GalleryRect() { return R(40, 120, 480, 520); }

const MonCtx* Mon() { return s_mon < g_mons.size() ? &g_mons[s_mon] : nullptr; }

std::wstring HotkeyLabel(std::wstring s) {
    std::wstring out;
    bool up = true;
    for (wchar_t c : s) {
        out += up ? (wchar_t)towupper(c) : c;
        up = c == L'+';
    }
    return out;
}

// Gallery content in content coordinates (before scrolling). Returns the content height.
int GalleryWidgets(std::vector<Widget>& out) {
    RECT g = GalleryRect();
    const MonCtx* m = Mon();
    double aspect = m ? (double)(m->work.bottom - m->work.top) / std::max(1L, m->work.right - m->work.left) : 0.6;
    int gap = S(14), tw = (g.right - g.left - 2 * gap) / 3, th = (int)(tw * aspect), label = S(26);
    int y = g.top;
    auto heading = [&](const wchar_t* t) {
        out.push_back(Widget{ RECT{ g.left, y, g.right, y + S(24) }, ID_NONE, W_DIM, t, false, true });
        y += S(30);
    };
    auto grid = [&](size_t count, auto make) {
        for (size_t i = 0; i < count; ++i) {
            int col = (int)(i % 3);
            if (i && col == 0) y += th + label + gap;
            int x = g.left + col * (tw + gap);
            out.push_back(make(i, RECT{ x, y, x + tw, y + th + label }));
        }
        if (count) y += th + label + S(18);
    };
    heading(L"Your layouts");
    grid(g_db.custom.size(), [&](size_t i, RECT r) {
        bool cur = m && m->type == "custom" && m->uuid == g_db.custom[i].uuid;
        return Widget{ r, ID_CUSTOM + (int)i, W_THUMB, Widen(g_db.custom[i].name), cur, true };
    });
    heading(L"Templates");
    const auto& t = LayoutTemplates();
    grid(t.size(), [&](size_t i, RECT r) { return Widget{ r, ID_TEMPLATE + (int)i, W_THUMB, Widen(t[i].name), false, true }; });
    return y - g.top;
}

int MaxScroll() {
    std::vector<Widget> tmp;
    RECT g = GalleryRect();
    return std::max(0, GalleryWidgets(tmp) - (int)(g.bottom - g.top));
}

std::vector<Widget> Build() {
    std::vector<Widget> w;
    auto add = [&](RECT r, int id, Kind k, std::wstring t = L"", bool on = false) { w.push_back(Widget{ r, id, k, std::move(t), on }); };

    // Left card: layouts.
    add(R(40, 36, 300, 28), ID_NONE, W_SECTION, L"Layout");
    if (g_mons.size() > 1) {
        int n = (int)std::min<size_t>(g_mons.size(), 4), tw = 480 / n;
        for (int i = 0; i < n; ++i)
            add(R(40 + i * tw, 76, tw - 4, 32), ID_TAB + i, W_SEG, std::to_wstring(i + 1) + L"  " + Widen(g_mons[i].name), (size_t)i == s_mon);
    } else if (const MonCtx* m = Mon()) {
        add(R(40, 76, 480, 32), ID_NONE, W_DIM,
            L"Display " + Widen(m->name) + L"   ·   " + std::to_wstring(m->work.right - m->work.left) + L" × " +
                std::to_wstring(m->work.bottom - m->work.top) + L"   ·   gap " + std::to_wstring(m->spacing) + L" px");
    }
    std::vector<Widget> gal;
    GalleryWidgets(gal);
    for (auto& x : gal) {
        OffsetRect(&x.r, 0, -s_scroll);
        w.push_back(x);
    }
    const CustomLayout* cur = Mon() ? AppliedCustom(*Mon()) : nullptr;
    add(R(40, 656, 196, 36), ID_EDIT, W_PRIMARY, cur ? L"Edit layout & gap" : L"Create layout");
    add(R(244, 656, 132, 36), ID_NEW, W_BUTTON, L"New layout");
    add(R(384, 656, 136, 36), ID_SHOW, W_BUTTON, L"Show zones");
    if (!g_cfg.editorHotkey.empty())
        add(R(40, 700, 480, 22), ID_NONE, W_DIM, HotkeyLabel(g_cfg.editorHotkey) + L" opens the editor on the monitor under the mouse");

    // Right card: appearance, behavior and mouse.
    const int x = 576, cw = 192, row = 36;  // right column starts at x and is 384 wide; segmented controls are cw wide
    int y = 36;
    auto label = [&](const wchar_t* t, int width = 330) { add(R(x, y, width, row), ID_NONE, W_TEXT, t); };
    auto section = [&](const wchar_t* t) {
        add(R(x, y, 300, 28), ID_NONE, W_SECTION, t);
        y += row;
    };
    auto toggle = [&](const wchar_t* t, int id, bool on) {
        label(t);
        add(R(x + 384 - 44, y + 8, 44, 20), id, W_TOGGLE, L"", on);
        y += row;
    };
    auto seg = [&](const wchar_t* t, int base, std::vector<const wchar_t*> opts, int sel) {
        label(t, 384 - cw - 8);
        int sw = cw / (int)opts.size();
        for (int i = 0; i < (int)opts.size(); ++i) add(R(x + 384 - cw + i * sw, y + 3, sw - 3, 30), base + i, W_SEG, opts[i], i == sel);
        y += row;
    };
    section(L"Appearance");
    label(L"Zone color");
    add(R(x + 384 - 56, y + 4, 56, 28), ID_COLOR, W_SWATCH);
    y += row;
    toggle(L"Highlight the zone while snapping", ID_T_HIGHLIGHT, g_cfg.overlay);
    y += 10;
    section(L"Behavior");
    toggle(L"Win+Arrow snapping", ID_T_ENABLED, g_cfg.enabled);
    toggle(L"Start with Windows", ID_T_AUTOSTART, AutostartEnabled());
    toggle(L"Move windows when the layout changes", ID_T_REFLOW, g_cfg.reflow);
    toggle(L"Snap new windows into the grid", ID_T_NEWWIN, g_cfg.snapNew);
    toggle(L"Open apps in the zone they used last", ID_T_LASTZONE, g_cfg.rememberZones);
    toggle(L"Win+Alt+Arrow jumps a whole zone", ID_T_ALTJUMP, g_cfg.altJump);
    seg(L"At the screen edge", ID_SEG_EDGE, { L"Monitor", L"Wrap", L"Stop" },
        g_cfg.edgeH == L"wrap" ? 1 : g_cfg.edgeH == L"stop" ? 2 : 0);
    seg(L"Win+Up at the top", ID_SEG_UP, { L"Maximize", L"Nothing" }, g_cfg.upAtTop == L"maximize" ? 0 : 1);
    seg(L"Win+Down at the bottom", ID_SEG_DOWN, { L"Minimize", L"Nothing" }, g_cfg.downAtBottom == L"minimize" ? 0 : 1);
    seg(L"Largest combination", ID_SEG_SPAN, { L"2 × 2", L"3 wide" }, g_cfg.maxSpan >= 3 ? 1 : 0);
    y += 10;
    section(L"Mouse");
    toggle(L"Drag + right-click snaps to zones", ID_T_DRAG, g_cfg.dragSnap);
    // Zone snapping: how far the two/four-zone area reaches into the zones, plus its on/off switch.
    label(L"Zone snapping", 384 - 172);
    add(R(x + 384 - 168, y + 4, 28, 28), ID_AREA_DEC, W_BUTTON, L"−", g_cfg.snapArea);
    add(R(x + 384 - 138, y + 4, 60, 28), ID_NONE, W_VALUE, std::to_wstring(g_cfg.snapAreaPx) + L" px", g_cfg.snapArea);
    add(R(x + 384 - 76, y + 4, 28, 28), ID_AREA_INC, W_BUTTON, L"+", g_cfg.snapArea);
    add(R(x + 384 - 44, y + 8, 44, 20), ID_T_AREA, W_TOGGLE, L"", g_cfg.snapArea);
    add(R(x, 716, 140, 22), ID_LINK_INI, W_LINK, L"Open settings file");
    add(R(x + 150, 716, 160, 22), ID_LINK_FOLDER, W_LINK, L"Open layout folder");
    return w;
}

int HitTest(POINT p) {
    RECT g = GalleryRect();
    for (auto& x : Build()) {
        if (x.id == ID_NONE || !PtInRect(&x.r, p)) continue;
        if (x.gallery && !PtInRect(&g, p)) continue;
        return x.id;
    }
    return ID_NONE;
}

// ---------- painting ----------

HFONT Font(int px, int weight, bool underline = false) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, underline, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

void Round(HDC dc, const RECT& r, COLORREF fill, int radius, COLORREF border = CLR_INVALID) {
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, fill);
    if (border == CLR_INVALID) {
        SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
    } else {
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, border);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    }
}

void Text(HDC dc, const std::wstring& t, RECT r, COLORREF c, UINT flags) {
    SetTextColor(dc, c);
    DrawTextW(dc, t.c_str(), (int)t.size(), &r, flags | DT_SINGLELINE | DT_NOPREFIX);
}

std::vector<Zone> ThumbZones(const Widget& w, RECT area) {
    int gap = std::max(2, S(3));
    if (w.id >= ID_TEMPLATE) return GridZones(LayoutTemplates()[w.id - ID_TEMPLATE].grid, area, gap);
    const CustomLayout& l = g_db.custom[w.id - ID_CUSTOM];
    return LayoutZones(l, area, l.isGrid ? gap : 0);
}

void PaintThumb(HDC dc, const Widget& w) {
    const Palette& p = P();
    bool hot = w.id == s_hover;
    RECT box = w.r;
    box.bottom -= S(26);
    Round(dc, box, p.thumb, S(10));
    RECT area = box;
    InflateRect(&area, -S(4), -S(4));
    COLORREF zone = g_cfg.overlayColor;
    for (auto& z : ThumbZones(w, area)) Round(dc, z.actual, zone, S(4));
    if (w.on || hot) {
        HPEN pen = CreatePen(PS_SOLID, S(w.on ? 3 : 2), w.on ? p.accent : p.dim);
        HGDIOBJ old = SelectObject(dc, pen);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, box.left + 1, box.top + 1, box.right - 1, box.bottom - 1, S(10), S(10));
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    RECT lab{ w.r.left, box.bottom + S(4), w.r.right, w.r.bottom };
    Text(dc, w.text + (w.on ? L"  ✓" : L""), lab, w.on ? p.text : p.dim, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
}

void Paint(HDC dc, RECT client) {
    const Palette& p = P();
    SetDCBrushColor(dc, p.bg);
    FillRect(dc, &client, (HBRUSH)GetStockObject(DC_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    Round(dc, R(20, 16, 520, kH - 32), p.card, S(12));
    Round(dc, R(556, 16, 424, kH - 32), p.card, S(12));

    HFONT section = Font(18, FW_SEMIBOLD), body = Font(14, FW_NORMAL), note = Font(13, FW_NORMAL), link = Font(13, FW_NORMAL, true);
    RECT g = GalleryRect();
    for (auto& w : Build()) {
        bool hot = w.id != ID_NONE && w.id == s_hover;
        if (w.gallery) {
            if (w.r.bottom < g.top || w.r.top > g.bottom) continue;
            SaveDC(dc);
            IntersectClipRect(dc, g.left - S(4), g.top, g.right + S(4), g.bottom);
        }
        switch (w.kind) {
        case W_SECTION: SelectObject(dc, section); Text(dc, w.text, w.r, p.text, DT_LEFT | DT_VCENTER); break;
        case W_TEXT: SelectObject(dc, body); Text(dc, w.text, w.r, p.text, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS); break;
        case W_DIM: SelectObject(dc, note); Text(dc, w.text, w.r, p.dim, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS); break;
        case W_VALUE: SelectObject(dc, body); Text(dc, w.text, w.r, w.on ? p.text : p.dim, DT_CENTER | DT_VCENTER); break;
        case W_LINK: SelectObject(dc, link); Text(dc, w.text, w.r, hot ? p.text : p.accent, DT_LEFT | DT_VCENTER); break;
        case W_BUTTON:
            Round(dc, w.r, hot ? p.ctrlHot : p.ctrl, S(8), p.border);
            SelectObject(dc, body);
            Text(dc, w.text, w.r, p.text, DT_CENTER | DT_VCENTER);
            break;
        case W_PRIMARY:
            Round(dc, w.r, hot ? Mix(p.accent, RGB(255, 255, 255), 0.15) : p.accent, S(8));
            SelectObject(dc, body);
            Text(dc, w.text, w.r, p.onAccent, DT_CENTER | DT_VCENTER);
            break;
        case W_SEG:
            Round(dc, w.r, w.on ? p.accent : (hot ? p.ctrlHot : p.ctrl), S(6), w.on ? CLR_INVALID : p.border);
            SelectObject(dc, note);
            Text(dc, w.text, w.r, w.on ? p.onAccent : p.text, DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
            break;
        case W_TOGGLE: {
            int h = w.r.bottom - w.r.top;
            if (w.on) Round(dc, w.r, hot ? Mix(p.accent, RGB(255, 255, 255), 0.15) : p.accent, h);
            else Round(dc, w.r, hot ? p.ctrlHot : p.ctrl, h, p.dim);
            int k = h - S(8), kx = w.on ? w.r.right - S(4) - k : w.r.left + S(4);
            RECT knob{ kx, w.r.top + S(4), kx + k, w.r.top + S(4) + k };
            Round(dc, knob, w.on ? p.onAccent : p.dim, k);
            break;
        }
        case W_SWATCH: {
            Round(dc, w.r, hot ? p.ctrlHot : p.ctrl, S(8), p.border);
            RECT in = w.r;
            InflateRect(&in, -S(4), -S(4));
            Round(dc, in, g_cfg.overlayColor, S(5));
            break;
        }
        case W_THUMB:
            SelectObject(dc, note);
            PaintThumb(dc, w);
            break;
        }
        if (w.gallery) RestoreDC(dc, -1);
    }
    // Scroll hint: fade lines at the gallery edges when there is more content.
    if (s_scroll > 0) { RECT t{ g.left, g.top, g.right, g.top + 1 }; SetDCBrushColor(dc, p.border); FillRect(dc, &t, (HBRUSH)GetStockObject(DC_BRUSH)); }
    if (s_scroll < MaxScroll()) { RECT b{ g.left, g.bottom - 1, g.right, g.bottom }; SetDCBrushColor(dc, p.border); FillRect(dc, &b, (HBRUSH)GetStockObject(DC_BRUSH)); }
    DeleteObject(section);
    DeleteObject(body);
    DeleteObject(note);
    DeleteObject(link);
}

// ---------- actions ----------

void UpdateIcons() {
    if (s_iconSmall) DestroyIcon(s_iconSmall);
    if (s_iconBig) DestroyIcon(s_iconBig);
    COLORREF c = s_dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
    s_iconSmall = CreateMarkIcon(GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(s_wnd)), c);
    s_iconBig = CreateMarkIcon(GetSystemMetricsForDpi(SM_CXICON, GetDpiForWindow(s_wnd)), c);
    SendMessageW(s_wnd, WM_SETICON, ICON_SMALL, (LPARAM)s_iconSmall);
    SendMessageW(s_wnd, WM_SETICON, ICON_BIG, (LPARAM)s_iconBig);
}

void ApplyTheme() {
    s_dark = AppsUseDarkTheme();
    BOOL dark = s_dark;
    DwmSetWindowAttribute(s_wnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    UpdateIcons();
}

void LayoutMenu(int id) {
    size_t li = (size_t)(id - ID_CUSTOM);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, MENU_APPLY, L"Use on this monitor");
    AppendMenuW(menu, MF_STRING | (g_db.custom[li].isGrid ? 0 : MF_GRAYED), MENU_EDIT, L"Edit...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MENU_DELETE, L"Delete");
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, s_wnd, nullptr);
    DestroyMenu(menu);
    if (cmd == MENU_APPLY) ApplyLayoutFor(s_mon, li);
    if (cmd == MENU_EDIT) {
        ApplyLayoutFor(s_mon, li);
        OpenEditorFor(s_mon, false);
    }
    if (cmd == MENU_DELETE) DeleteLayout(li, s_wnd);
}

void Click(int id) {
    if (id >= ID_TAB && id < ID_CUSTOM) { s_mon = (size_t)(id - ID_TAB); s_scroll = 0; }
    else if (id >= ID_CUSTOM && id < ID_TEMPLATE) ApplyLayoutFor(s_mon, (size_t)(id - ID_CUSTOM));
    else if (id >= ID_TEMPLATE && id < ID_EDIT) ApplyTemplateFor(s_mon, (size_t)(id - ID_TEMPLATE));
    else if (id == ID_EDIT) OpenEditorFor(s_mon, false);
    else if (id == ID_NEW) OpenEditorFor(s_mon, true);
    else if (id == ID_SHOW) ShowZonePreview(g_inst, g_mons, g_cfg.overlayColor);
    else if (id == ID_COLOR) {
        COLORREF c = g_cfg.overlayColor;
        if (PickColor(s_wnd, c)) { SaveZoneColor(c); Reload(); }
    }
    else if (id == ID_T_ENABLED) SetSetting(L"enabled", g_cfg.enabled ? L"0" : L"1");
    else if (id == ID_T_AUTOSTART) SetAutostart(!AutostartEnabled());
    else if (id == ID_T_HIGHLIGHT) SetSetting(L"overlay", g_cfg.overlay ? L"0" : L"1");
    else if (id == ID_T_REFLOW) SetSetting(L"reflow_windows", g_cfg.reflow ? L"0" : L"1");
    else if (id == ID_T_ALTJUMP) SetSetting(L"alt_jump", g_cfg.altJump ? L"0" : L"1");
    else if (id == ID_T_NEWWIN) SetSetting(L"snap_new_windows", g_cfg.snapNew ? L"0" : L"1");
    else if (id == ID_T_DRAG) SetSetting(L"drag_snap", g_cfg.dragSnap ? L"0" : L"1");
    else if (id == ID_T_AREA) SetSetting(L"snap_area", g_cfg.snapArea ? L"0" : L"1");
    else if (id == ID_AREA_DEC || id == ID_AREA_INC)
        SetSetting(L"snap_area_px", std::to_wstring(std::clamp(g_cfg.snapAreaPx + (id == ID_AREA_INC ? 5 : -5), 0, 100)));
    else if (id == ID_T_LASTZONE) SetSetting(L"remember_app_zones", g_cfg.rememberZones ? L"0" : L"1");
    else if (id >= ID_SEG_EDGE && id < ID_SEG_EDGE + 3) {
        static const wchar_t* edge[] = { L"monitor", L"wrap", L"stop" };
        SetSetting(L"edge_horizontal", edge[id - ID_SEG_EDGE]);
    }
    else if (id >= ID_SEG_UP && id < ID_SEG_UP + 2) SetSetting(L"up_at_top", id == ID_SEG_UP ? L"maximize" : L"stop");
    else if (id >= ID_SEG_DOWN && id < ID_SEG_DOWN + 2) SetSetting(L"down_at_bottom", id == ID_SEG_DOWN ? L"minimize" : L"stop");
    else if (id >= ID_SEG_SPAN && id < ID_SEG_SPAN + 2) SetSetting(L"max_span", id == ID_SEG_SPAN ? L"2" : L"3");
    else if (id == ID_LINK_INI) ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + g_iniPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    else if (id == ID_LINK_FOLDER) ShellExecuteW(nullptr, L"open", g_cfg.layoutsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (s_wnd) InvalidateRect(s_wnd, nullptr, FALSE);
}

void Resize() {
    UINT dpi = GetDpiForWindow(s_wnd);
    s_scale = dpi / 96.0;
    RECT r{ 0, 0, S(kW), S(kH) };
    AdjustWindowRectExForDpi(&r, GetWindowLongW(s_wnd, GWL_STYLE), FALSE, 0, dpi);
    SetWindowPos(s_wnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT c;
        GetClientRect(w, &c);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem, c);
        BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        int id = HitTest(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (id != s_hover) {
            s_hover = id;
            InvalidateRect(w, nullptr, FALSE);
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, w, 0 };
            TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        s_hover = ID_NONE;
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, s_hover != ID_NONE ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP: {
        int id = HitTest(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (id != ID_NONE) Click(id);
        return 0;
    }
    case WM_RBUTTONUP: {
        int id = HitTest(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (id >= ID_CUSTOM && id < ID_TEMPLATE) LayoutMenu(id);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        s_scroll = std::clamp(s_scroll - GET_WHEEL_DELTA_WPARAM(wp) * S(60) / WHEEL_DELTA, 0, MaxScroll());
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(w);
        return 0;
    case WM_DPICHANGED: {
        s_scale = HIWORD(wp) / 96.0;
        auto* r = (RECT*)lp;
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateIcons();
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
            ApplyTheme();
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        s_wnd = nullptr;
        s_hover = ID_NONE;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

void ShowSettings() {
    if (s_wnd) {
        ShowWindow(s_wnd, SW_RESTORE);
        SetForegroundWindow(s_wnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = g_inst;
        wc.lpszClassName = L"Tessel.Settings";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    s_mon = MonitorUnderCursor();
    s_scroll = 0;
    const MonCtx* m = Mon();
    RECT area = m ? m->work : RECT{ 0, 0, 1280, 800 };
    s_wnd = CreateWindowExW(0, L"Tessel.Settings", L"Tessel Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            area.left + 40, area.top + 40, 400, 300, nullptr, nullptr, g_inst, nullptr);
    if (!s_wnd) return;
    ApplyTheme();
    Resize();
    RECT wr;
    GetWindowRect(s_wnd, &wr);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    SetWindowPos(s_wnd, nullptr, area.left + std::max(0L, (area.right - area.left - w) / 2), area.top + std::max(0L, (area.bottom - area.top - h) / 2),
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
}

void SettingsRefresh() {
    if (!s_wnd) return;
    if (s_mon >= g_mons.size()) s_mon = 0;
    s_scroll = std::clamp(s_scroll, 0, MaxScroll());
    InvalidateRect(s_wnd, nullptr, FALSE);
}

void CloseSettings() {
    if (s_wnd) DestroyWindow(s_wnd);
}
