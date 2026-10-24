#include "editor.h"
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <algorithm>
#include <string>

namespace {

enum Btn { B_GAP_DEC, B_GAP_INC, B_COL_DEC, B_COL_INC, B_ROW_DEC, B_ROW_INC, B_EQUAL, B_COLOR, B_CANCEL, B_SAVE };
enum Kind { K_LABEL, K_BUTTON, K_VALUE, K_PRIMARY, K_SWATCH };
struct Item { RECT r; int id; std::wstring text; Kind kind; };
struct Seg { int axis, idx; RECT hit, handle; };  // axis 0 = column boundary, 1 = row boundary

constexpr int kMinPct = 500;  // smallest row or column: 5%
constexpr int kMaxCells = 8;
constexpr int kMaxGap = 100;
constexpr COLORREF kBackdrop = RGB(16, 16, 20);
constexpr COLORREF kPanel = RGB(38, 38, 44), kButton = RGB(62, 62, 70), kButtonHot = RGB(88, 88, 98);
constexpr COLORREF kAccent = RGB(0, 120, 215), kAccentHot = RGB(30, 145, 235), kHandleHot = RGB(255, 185, 0);
constexpr COLORREF kText = RGB(255, 255, 255), kTextDim = RGB(170, 170, 180);

HWND s_wnd;
EditorRequest s_req;
std::function<void(const EditorRequest&)> s_onSave;
int s_w, s_h;
double s_scale = 1;
int s_dragAxis = -1, s_dragIdx = -1;
int s_hoverSeg = -1, s_hoverBtn = -1;

int S(int v) { return (int)(v * s_scale + 0.5); }

std::wstring Widen(const std::string& s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), (int)w.size());
    return w;
}

std::vector<int> Cum(const std::vector<int>& pct) {
    std::vector<int> c{ 0 };
    for (int p : pct) c.push_back(c.back() + p);
    return c;
}

int EdgePx(const std::vector<int>& pct, int i, int len) { return (int)((long long)Cum(pct)[i] * len / 10000); }

void ResetMap() { s_req.grid.cellMap = EvenGrid(s_req.grid.rows, s_req.grid.cols).cellMap; }

// Adding splits the widest row/column in half; removing folds the last one into its neighbor.
// Either way merged cells are reset to a plain grid.
void AddSplit(std::vector<int>& pct, int& count) {
    if (count >= kMaxCells) return;
    int k = (int)(std::max_element(pct.begin(), pct.end()) - pct.begin());
    if (pct[k] < 2 * kMinPct) return;
    int a = pct[k] / 2;
    pct.insert(pct.begin() + k + 1, pct[k] - a);
    pct[k] = a;
    ++count;
    ResetMap();
}

void RemoveSplit(std::vector<int>& pct, int& count) {
    if (count <= 1) return;
    pct[count - 2] += pct[count - 1];
    pct.pop_back();
    --count;
    ResetMap();
}

void Equalize(std::vector<int>& pct) {
    int n = (int)pct.size();
    std::fill(pct.begin(), pct.end(), 10000 / n);
    pct.back() += 10000 - (10000 / n) * n;
}

COLORREF Mix(COLORREF a, COLORREF b, double t) {
    auto ch = [&](int x, int y) { return (BYTE)(x + (y - x) * t + 0.5); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

bool IsLight(COLORREF c) { return GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114 > 160000; }

RECT PanelRect() {
    int w = std::min(S(1180), s_w - S(32)), h = S(132);
    int x = (s_w - w) / 2, y = s_h - h - S(48);
    return RECT{ x, y, x + w, y + h };
}

std::vector<Item> Items() {
    RECT p = PanelRect();
    std::vector<Item> v;
    int y = p.top + S(50), bh = S(36), x = p.left + S(24);
    auto add = [&](int w, int id, std::wstring t, Kind k) {
        v.push_back(Item{ RECT{ x, y, x + w, y + bh }, id, std::move(t), k });
        x += w + S(6);
    };
    const GridInfo& g = s_req.grid;
    add(S(44), -1, L"Gap", K_LABEL);
    add(bh, B_GAP_DEC, L"−", K_BUTTON);
    add(S(70), -1, std::to_wstring(s_req.spacing) + L" px", K_VALUE);
    add(bh, B_GAP_INC, L"+", K_BUTTON);
    x += S(18);
    add(S(72), -1, L"Columns", K_LABEL);
    add(bh, B_COL_DEC, L"−", K_BUTTON);
    add(S(36), -1, std::to_wstring(g.cols), K_VALUE);
    add(bh, B_COL_INC, L"+", K_BUTTON);
    x += S(18);
    add(S(48), -1, L"Rows", K_LABEL);
    add(bh, B_ROW_DEC, L"−", K_BUTTON);
    add(S(36), -1, std::to_wstring(g.rows), K_VALUE);
    add(bh, B_ROW_INC, L"+", K_BUTTON);
    x += S(18);
    add(S(96), B_EQUAL, L"Equalize", K_BUTTON);
    x += S(12);
    add(S(48), -1, L"Color", K_LABEL);
    add(bh, B_COLOR, L"", K_SWATCH);
    x = p.right - S(24) - S(96) * 2 - S(6);
    add(S(96), B_CANCEL, L"Cancel", K_BUTTON);
    add(S(96), B_SAVE, L"Save", K_PRIMARY);
    return v;
}

std::vector<Seg> Segments() {
    const GridInfo& g = s_req.grid;
    std::vector<Seg> out;
    int grab = std::max(S(8), s_req.spacing / 2 + S(2));
    for (int i = 1; i < g.cols; ++i) {
        int x = EdgePx(g.colPct, i, s_w);
        for (int r = 0; r < g.rows;) {
            if (g.cellMap[r][i - 1] == g.cellMap[r][i]) { ++r; continue; }  // merged across this line
            int r1 = r;
            while (r1 + 1 < g.rows && g.cellMap[r1 + 1][i - 1] != g.cellMap[r1 + 1][i]) ++r1;
            int top = EdgePx(g.rowPct, r, s_h), bot = EdgePx(g.rowPct, r1 + 1, s_h), mid = (top + bot) / 2;
            out.push_back(Seg{ 0, i, RECT{ x - grab, top, x + grab, bot }, RECT{ x - S(4), mid - S(28), x + S(4), mid + S(28) } });
            r = r1 + 1;
        }
    }
    for (int i = 1; i < g.rows; ++i) {
        int y = EdgePx(g.rowPct, i, s_h);
        for (int c = 0; c < g.cols;) {
            if (g.cellMap[i - 1][c] == g.cellMap[i][c]) { ++c; continue; }
            int c1 = c;
            while (c1 + 1 < g.cols && g.cellMap[i - 1][c1 + 1] != g.cellMap[i][c1 + 1]) ++c1;
            int l = EdgePx(g.colPct, c, s_w), r = EdgePx(g.colPct, c1 + 1, s_w), mid = (l + r) / 2;
            out.push_back(Seg{ 1, i, RECT{ l, y - grab, r, y + grab }, RECT{ mid - S(28), y - S(4), mid + S(28), y + S(4) } });
            c = c1 + 1;
        }
    }
    return out;
}

int HitButton(POINT p) {
    for (auto& it : Items())
        if (it.id >= 0 && PtInRect(&it.r, p)) return it.id;
    return -1;
}

int HitSeg(POINT p) {
    RECT panel = PanelRect();
    if (PtInRect(&panel, p)) return -1;
    auto segs = Segments();
    for (int i = 0; i < (int)segs.size(); ++i)
        if (PtInRect(&segs[i].hit, p)) return i;
    return -1;
}

void DragTo(int axis, int idx, int pos, bool snap) {
    auto& pct = axis == 0 ? s_req.grid.colPct : s_req.grid.rowPct;
    int len = axis == 0 ? s_w : s_h;
    auto c = Cum(pct);
    int u = (int)((long long)pos * 10000 / len);
    if (snap) u = (u + 250) / 500 * 500;
    u = std::clamp(u, c[idx - 1] + kMinPct, c[idx + 1] - kMinPct);
    pct[idx - 1] = u - c[idx - 1];
    pct[idx] = c[idx + 1] - u;
}

void Close(bool save) {
    EditorRequest result = s_req;
    auto cb = s_onSave;
    DestroyWindow(s_wnd);
    if (save && cb) cb(result);
}

void Do(int btn, bool shift) {
    GridInfo& g = s_req.grid;
    int step = shift ? 5 : 1;
    switch (btn) {
    case B_GAP_DEC: s_req.spacing = std::max(0, s_req.spacing - step); break;
    case B_GAP_INC: s_req.spacing = std::min(kMaxGap, s_req.spacing + step); break;
    case B_COL_DEC: RemoveSplit(g.colPct, g.cols); break;
    case B_COL_INC: AddSplit(g.colPct, g.cols); break;
    case B_ROW_DEC: RemoveSplit(g.rowPct, g.rows); break;
    case B_ROW_INC: AddSplit(g.rowPct, g.rows); break;
    case B_EQUAL: Equalize(g.colPct); Equalize(g.rowPct); break;
    case B_COLOR: PickColor(s_wnd, s_req.color); break;
    case B_CANCEL: Close(false); return;
    case B_SAVE: Close(true); return;
    }
    s_hoverSeg = -1;
    InvalidateRect(s_wnd, nullptr, FALSE);
}

HFONT Font(int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

void Fill(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

void Round(HDC dc, const RECT& r, COLORREF c, int radius) {
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, c);
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
}

void Text(HDC dc, const std::wstring& t, RECT r, COLORREF c, UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    SetTextColor(dc, c);
    DrawTextW(dc, t.c_str(), (int)t.size(), &r, flags | DT_NOPREFIX);
}

void Paint(HDC dc) {
    const GridInfo& g = s_req.grid;
    RECT all{ 0, 0, s_w, s_h };
    Fill(dc, all, kBackdrop);
    SetBkMode(dc, TRANSPARENT);

    HFONT big = Font(20, FW_SEMIBOLD), note = Font(14, FW_NORMAL), ui = Font(15, FW_NORMAL), title = Font(17, FW_SEMIBOLD);
    COLORREF zone = s_req.color, border = Mix(zone, RGB(255, 255, 255), 0.45);
    COLORREF zoneText = IsLight(zone) ? RGB(0, 0, 0) : kText, zoneDim = Mix(zone, zoneText, 0.7);
    for (auto& z : GridZones(g, all, s_req.spacing)) {
        Fill(dc, z.actual, border);
        RECT in = z.actual;
        InflateRect(&in, -S(2), -S(2));
        Fill(dc, in, zone);
        int w = z.actual.right - z.actual.left, h = z.actual.bottom - z.actual.top;
        int pw = (int)((z.logical.right - z.logical.left) * 1000LL / s_w), ph = (int)((z.logical.bottom - z.logical.top) * 1000LL / s_h);
        RECT a = in, b = in;
        a.bottom = (in.top + in.bottom) / 2 + S(4);
        b.top = a.bottom;
        SelectObject(dc, big);
        Text(dc, std::to_wstring(w) + L" × " + std::to_wstring(h), a, zoneText, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        SelectObject(dc, note);
        wchar_t pct[64];
        swprintf_s(pct, L"%.1f%% × %.1f%%", pw / 10.0, ph / 10.0);
        Text(dc, pct, b, zoneDim, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    auto segs = Segments();
    for (int i = 0; i < (int)segs.size(); ++i) {
        bool hot = i == s_hoverSeg || (segs[i].axis == s_dragAxis && segs[i].idx == s_dragIdx);
        Round(dc, segs[i].handle, hot ? kHandleHot : kText, S(8));
    }

    RECT p = PanelRect();
    Round(dc, p, kPanel, S(16));
    SelectObject(dc, title);
    RECT t{ p.left + S(24), p.top + S(12), p.right - S(24), p.top + S(44) };
    Text(dc, (s_req.uuid.empty() ? L"New layout: " : L"Editing: ") + Widen(s_req.name) + L"   (" + Widen(s_req.mon.name) + L")",
         t, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, ui);
    for (auto& it : Items()) {
        if (it.kind == K_BUTTON || it.kind == K_PRIMARY) {
            bool hot = it.id == s_hoverBtn;
            COLORREF c = it.kind == K_PRIMARY ? (hot ? kAccentHot : kAccent) : (hot ? kButtonHot : kButton);
            Round(dc, it.r, c, S(8));
        } else if (it.kind == K_SWATCH) {
            Round(dc, it.r, it.id == s_hoverBtn ? kText : kButtonHot, S(8));
            RECT in = it.r;
            InflateRect(&in, -S(3), -S(3));
            Round(dc, in, s_req.color, S(6));
        }
        Text(dc, it.text, it.r, it.kind == K_LABEL ? kTextDim : kText, (it.kind == K_LABEL ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, note);
    RECT h{ p.left + S(24), p.bottom - S(34), p.right - S(24), p.bottom - S(10) };
    Text(dc, L"Drag handles to resize (Shift: snap 5%)  ·  + / − or wheel: gap  ·  C / R: add column / row, Shift removes"
             L"  ·  E: equalize  ·  K: color  ·  Enter: save  ·  Esc: cancel",
         h, kTextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DeleteObject(big);
    DeleteObject(note);
    DeleteObject(ui);
    DeleteObject(title);
}

LRESULT CALLBACK EditorProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, s_w, s_h);
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem);
        BitBlt(dc, 0, 0, s_w, s_h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (s_dragAxis >= 0) {
            DragTo(s_dragAxis, s_dragIdx, s_dragAxis == 0 ? p.x : p.y, (wp & MK_SHIFT) != 0);
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        int seg = HitSeg(p), btn = HitButton(p);
        if (seg != s_hoverSeg || btn != s_hoverBtn) {
            s_hoverSeg = seg;
            s_hoverBtn = btn;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            int axis = s_dragAxis >= 0 ? s_dragAxis : (s_hoverSeg >= 0 ? Segments()[s_hoverSeg].axis : -1);
            SetCursor(LoadCursor(nullptr, axis == 0 ? IDC_SIZEWE : axis == 1 ? IDC_SIZENS : s_hoverBtn >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int btn = HitButton(p);
        if (btn >= 0) { Do(btn, (wp & MK_SHIFT) != 0); return 0; }
        int seg = HitSeg(p);
        if (seg >= 0) {
            auto s = Segments()[seg];
            s_dragAxis = s.axis;
            s_dragIdx = s.idx;
            SetCapture(w);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (s_dragAxis >= 0) {
            s_dragAxis = s_dragIdx = -1;
            ReleaseCapture();
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_RBUTTONUP:
        Close(false);
        return 0;
    case WM_MOUSEWHEEL:
        Do(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? B_GAP_INC : B_GAP_DEC, (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0);
        return 0;
    case WM_KEYDOWN: {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        switch (wp) {
        case VK_ESCAPE: Close(false); break;
        case VK_RETURN: Close(true); break;
        case VK_OEM_PLUS: case VK_ADD: Do(B_GAP_INC, shift); break;
        case VK_OEM_MINUS: case VK_SUBTRACT: Do(B_GAP_DEC, shift); break;
        case 'C': Do(shift ? B_COL_DEC : B_COL_INC, false); break;
        case 'R': Do(shift ? B_ROW_DEC : B_ROW_INC, false); break;
        case 'E': Do(B_EQUAL, false); break;
        case 'K': Do(B_COLOR, false); break;
        }
        return 0;
    }
    case WM_DESTROY:
        s_wnd = nullptr;
        s_dragAxis = s_dragIdx = s_hoverSeg = s_hoverBtn = -1;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

bool EditorOpen() { return s_wnd != nullptr; }

bool PickColor(HWND owner, COLORREF& color) {
    // Starts with a few useful zone colors in the custom slots; remembers picks for this run.
    static COLORREF custom[16] = { RGB(0, 120, 215), RGB(0, 153, 112), RGB(136, 23, 152), RGB(202, 80, 16),
                                   RGB(194, 57, 179), RGB(16, 124, 16), RGB(118, 118, 118), RGB(45, 45, 48) };
    CHOOSECOLORW cc{ sizeof(cc) };
    cc.hwndOwner = owner;
    cc.rgbResult = color;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&cc)) return false;
    color = cc.rgbResult;
    if (owner) InvalidateRect(owner, nullptr, FALSE);
    return true;
}

void OpenEditor(HINSTANCE inst, const EditorRequest& req, std::function<void(const EditorRequest&)> onSave) {
    if (s_wnd) {
        SetForegroundWindow(s_wnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = EditorProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"Tessel.Editor";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    s_req = req;
    s_onSave = std::move(onSave);
    const RECT& r = req.mon.work;
    s_w = r.right - r.left;
    s_h = r.bottom - r.top;
    s_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"Tessel.Editor", L"Tessel layout editor", WS_POPUP,
                            r.left, r.top, s_w, s_h, nullptr, nullptr, inst, nullptr);
    if (!s_wnd) return;
    s_scale = GetDpiForWindow(s_wnd) / 96.0;
    BOOL noFade = TRUE;  // appear at once instead of fading in
    DwmSetWindowAttribute(s_wnd, DWMWA_TRANSITIONS_FORCEDISABLED, &noFade, sizeof(noFade));
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
    SetFocus(s_wnd);
}
