#include "icon.h"
#include <algorithm>
#include <vector>

void DrawMark(UINT32* px, int s, COLORREF c) {
    double m = s * 0.04, gap = std::max(1.5, s * 0.14), radius = std::max(0.6, s * 0.09);
    double inner = s - 2 * m;
    double w0 = (inner - gap) * 0.58, h = (inner - gap) / 2;
    double boxes[4][4] = {
        { m, m, m + w0, m + h },
        { m + w0 + gap, m, s - m, m + h },
        { m, m + h + gap, m + w0, s - m },
        { m + w0 + gap, m + h + gap, s - m, s - m },
    };
    auto inside = [&](double x, double y) {
        for (auto& b : boxes) {
            if (x < b[0] || x > b[2] || y < b[1] || y > b[3]) continue;
            double dx = std::max({ b[0] + radius - x, 0.0, x - (b[2] - radius) });
            double dy = std::max({ b[1] + radius - y, 0.0, y - (b[3] - radius) });
            if (dx * dx + dy * dy <= radius * radius) return true;
        }
        return false;
    };
    const int ss = 4;  // 4x4 supersampling for smooth edges at 16 px
    UINT32 rgb = (UINT32)GetRValue(c) << 16 | (UINT32)GetGValue(c) << 8 | GetBValue(c);
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) {
            int hits = 0;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx) hits += inside(x + (sx + 0.5) / ss, y + (sy + 0.5) / ss);
            UINT32 a = (UINT32)(hits * 255 / (ss * ss));
            px[y * s + x] = a << 24 | (a ? rgb : 0);
        }
}

HICON CreateMarkIcon(int s, COLORREF c) {
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), s, -s, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;
    DrawMark((UINT32*)bits, s, c);
    std::vector<BYTE> maskBits(((s + 15) / 16) * 2 * s, 0);
    HBITMAP mask = CreateBitmap(s, s, 1, 1, maskBits.data());
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

COLORREF TaskbarMarkColor() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light ? RGB(0, 0, 0) : RGB(255, 255, 255);
}
