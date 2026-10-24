#include "preview.h"
#include <memory>
#include <string>

namespace {

constexpr COLORREF kKey = RGB(255, 0, 255);  // painted where the desktop should show through
constexpr UINT_PTR TIMER_CLOSE = 1;
constexpr int kCloseMs = 8000;

struct View {
    MonCtx mon;
    COLORREF color;
    bool hint;  // shows the "click to close" pill
};

std::vector<HWND> s_wins;
bool s_closing;

COLORREF Mix(COLORREF a, COLORREF b, double t) {
    auto ch = [&](int x, int y) { return (BYTE)(x + (y - x) * t + 0.5); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

std::wstring Widen(const std::string& s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), (int)w.size());
    return w;
}

bool IsLight(COLORREF c) { return GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114 > 160000; }

void CloseAll() {
    if (s_closing) return;
    s_closing = true;
    for (HWND w : s_wins) DestroyWindow(w);
    s_wins.clear();
    s_closing = false;
}

void Fill(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

void Paint(HWND w, HDC dc, const View& v) {
    RECT client;
    GetClientRect(w, &client);
    Fill(dc, client, kKey);
    double scale = GetDpiForWindow(w) / 96.0;
    auto S = [&](int x) { return (int)(x * scale + 0.5); };
    HFONT big = CreateFontW(-S(64), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT note = CreateFontW(-S(16), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    SetBkMode(dc, TRANSPARENT);
    COLORREF text = IsLight(v.color) ? RGB(0, 0, 0) : RGB(255, 255, 255);
    POINT origin{ v.mon.work.left, v.mon.work.top };

    if (!v.mon.ok) {
        RECT r = client;
        SelectObject(dc, note);
        RECT pill{ (r.right - S(420)) / 2, (r.bottom - S(56)) / 2, (r.right + S(420)) / 2, (r.bottom + S(56)) / 2 };
        Fill(dc, pill, RGB(32, 32, 32));
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, L"No layout on this monitor", -1, &pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    for (auto& z : v.mon.zones) {
        RECT r = z.actual;
        OffsetRect(&r, -origin.x, -origin.y);
        Fill(dc, r, Mix(v.color, RGB(255, 255, 255), 0.5));
        InflateRect(&r, -S(3), -S(3));
        Fill(dc, r, v.color);
        RECT num = r, size = r;
        num.bottom = (r.top + r.bottom) / 2 + S(20);
        size.top = num.bottom;
        SetTextColor(dc, text);
        SelectObject(dc, big);
        std::wstring n = std::to_wstring(z.id + 1);
        DrawTextW(dc, n.c_str(), -1, &num, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        SelectObject(dc, note);
        std::wstring s = std::to_wstring(z.actual.right - z.actual.left) + L" × " + std::to_wstring(z.actual.bottom - z.actual.top);
        DrawTextW(dc, s.c_str(), -1, &size, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }
    if (v.hint) {
        SelectObject(dc, note);
        std::wstring label = (v.mon.layoutName.empty() ? std::wstring() : Widen(v.mon.layoutName) + L"   ·   ") +
                             L"Click anywhere or press any key to close";
        RECT pill{ (client.right - S(560)) / 2, client.bottom - S(84), (client.right + S(560)) / 2, client.bottom - S(40) };
        Fill(dc, pill, RGB(32, 32, 32));
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, label.c_str(), -1, &pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    DeleteObject(big);
    DeleteObject(note);
}

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* v = (View*)GetWindowLongPtrW(w, GWLP_USERDATA);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (v) Paint(w, dc, *v);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        CloseAll();
        return 0;
    case WM_ACTIVATEAPP:
        if (!wp) CloseAll();
        return 0;
    case WM_TIMER:
        CloseAll();
        return 0;
    case WM_NCDESTROY:
        delete v;
        SetWindowLongPtrW(w, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

void ShowZonePreview(HINSTANCE inst, const std::vector<MonCtx>& mons, COLORREF color) {
    CloseAll();
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.lpszClassName = L"Tessel.ZonePreview";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    // The pill goes on the monitor under the mouse.
    POINT p;
    GetCursorPos(&p);
    HMONITOR active = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    HWND focus = nullptr;
    for (auto& m : mons) {
        const RECT& r = m.work;
        HWND w = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"Tessel.ZonePreview", L"Tessel zones", WS_POPUP,
                                 r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
        if (!w) continue;
        SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)new View{ m, color, m.h == active });
        SetLayeredWindowAttributes(w, kKey, 165, LWA_COLORKEY | LWA_ALPHA);
        ShowWindow(w, SW_SHOWNA);
        s_wins.push_back(w);
        if (m.h == active || !focus) focus = w;
    }
    if (focus) {
        SetForegroundWindow(focus);
        SetTimer(focus, TIMER_CLOSE, kCloseMs, nullptr);
    }
}
