#include "dragsnap.h"
#include "app.h"
#include "snap.h"
#include <shellscalingapi.h>

namespace {

constexpr COLORREF kKey = RGB(255, 0, 255);  // transparent background of the overlay

HINSTANCE s_inst;
std::function<void(HWND, const MonCtx&, int)> s_onSnap;
HWND s_drag;          // window being dragged
SIZE s_startSize;     // a resize changes the size; a move doesn't
bool s_zones;         // right button toggled the zones on
HHOOK s_mouse;
HWND s_overlay;
size_t s_mon = SIZE_MAX;
int s_combo = -1;

COLORREF Mix(COLORREF a, COLORREF b, double t) {
    auto ch = [&](int x, int y) { return (BYTE)(x + (y - x) * t + 0.5); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

void Fill(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

bool Moving() {
    RECT r;
    return GetWindowRect(s_drag, &r) && r.right - r.left == s_startSize.cx && r.bottom - r.top == s_startSize.cy;
}

void Paint(HDC dc) {
    RECT client;
    GetClientRect(s_overlay, &client);
    Fill(dc, client, kKey);
    if (s_mon >= g_mons.size()) return;
    const MonCtx& m = g_mons[s_mon];
    POINT o{ m.work.left, m.work.top };
    // Other zones are outlines only (see-through inside), so the target is the one solid block.
    COLORREF color = g_cfg.overlayColor, outline = Mix(color, RGB(0, 0, 0), 0.25), border = RGB(255, 255, 255);
    for (auto& z : m.zones) {
        RECT r = z.actual;
        OffsetRect(&r, -o.x, -o.y);
        Fill(dc, r, outline);
        InflateRect(&r, -4, -4);
        Fill(dc, r, kKey);
    }
    if (s_combo >= 0 && s_combo < (int)m.grid.combos.size()) {
        RECT r = m.grid.combos[s_combo].actual;
        OffsetRect(&r, -o.x, -o.y);
        Fill(dc, r, border);
        InflateRect(&r, -4, -4);
        Fill(dc, r, color);
    }
}

LRESULT CALLBACK OverlayProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        Paint(dc);
        EndPaint(w, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(w, msg, wp, lp);
}

void Hide() {
    if (s_overlay) ShowWindow(s_overlay, SW_HIDE);
    s_mon = SIZE_MAX;
    s_combo = -1;
}

// Picks the target under the cursor and moves/repaints the overlay when it changes.
void Update(POINT p) {
    HMONITOR h = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    size_t mi = SIZE_MAX;
    for (size_t i = 0; i < g_mons.size(); ++i)
        if (g_mons[i].h == h && g_mons[i].ok) mi = i;
    if (mi == SIZE_MAX) { Hide(); return; }
    const MonCtx& m = g_mons[mi];
    int combo = ComboAtPoint(m.grid, p, m.spacing + SnapReach(m));
    if (mi == s_mon && combo == s_combo && IsWindowVisible(s_overlay)) return;
    bool moved = mi != s_mon;
    s_mon = mi;
    s_combo = combo;
    if (moved || !IsWindowVisible(s_overlay))
        SetWindowPos(s_overlay, HWND_TOPMOST, m.work.left, m.work.top, m.work.right - m.work.left, m.work.bottom - m.work.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(s_overlay, nullptr, FALSE);
}

LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && s_drag) {
        auto* m = (MSLLHOOKSTRUCT*)lp;
        if (wp == WM_RBUTTONDOWN) {
            // The right button is ours during a drag; the app and the move loop never see it.
            if (Moving()) {
                s_zones = !s_zones;
                if (s_zones) Update(m->pt);
                else Hide();
            }
            return 1;
        }
        if (wp == WM_RBUTTONUP) return 1;
        if (wp == WM_MOUSEMOVE && s_zones) Update(m->pt);
    }
    return CallNextHookEx(s_mouse, code, wp, lp);
}

void Stop() {
    if (s_mouse) UnhookWindowsHookEx(s_mouse);
    s_mouse = nullptr;
    s_drag = nullptr;
    s_zones = false;
    Hide();
}

}  // namespace

int SnapReach(const MonCtx& m) {
    if (!g_cfg.snapArea) return 0;
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(m.h, MDT_EFFECTIVE_DPI, &dx, &dy);
    return MulDiv(g_cfg.snapAreaPx, (int)dx, 96);
}

void DragSnapInit(HINSTANCE inst, std::function<void(HWND, const MonCtx&, int)> onSnap) {
    s_inst = inst;
    s_onSnap = std::move(onSnap);
    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"Tessel.DragZones";
    RegisterClassW(&wc);
    // Click-through, never activated: the drag keeps going underneath.
    s_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    SetLayeredWindowAttributes(s_overlay, kKey, 150, LWA_COLORKEY | LWA_ALPHA);
}

void DragSnapStart(HWND w) {
    Stop();
    if (!g_cfg.dragSnap || !IsSnappable(w, g_cfg.excluded)) return;
    RECT r;
    GetWindowRect(w, &r);
    s_drag = w;
    s_startSize = SIZE{ r.right - r.left, r.bottom - r.top };
    s_mouse = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, s_inst, 0);  // only while a drag is in progress
}

void DragSnapTest(HWND w, POINT p, bool keepOpen) {
    if (w != s_drag) DragSnapStart(w);
    if (w != s_drag) return;
    s_zones = true;
    Update(p);
    if (!keepOpen) DragSnapEnd(w);
}

void DragSnapEnd(HWND w) {
    if (w != s_drag) return;
    bool snap = s_zones && s_mon < g_mons.size() && s_combo >= 0 && Moving() && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000);
    size_t mon = s_mon;
    int combo = s_combo;
    Stop();
    if (snap && s_onSnap) s_onSnap(w, g_mons[mon], combo);
}
