// Build helper: writes the black app mark as a multi-size .ico for the exe resource.
#include "../src/icon.h"
#include <cstdio>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { fwprintf(stderr, L"usage: make_icon out.ico\n"); return 1; }
    const int sizes[] = { 16, 20, 24, 32, 40, 48, 64 };
    const int n = (int)(sizeof(sizes) / sizeof(sizes[0]));
    std::vector<std::vector<BYTE>> images;
    for (int s : sizes) {
        std::vector<UINT32> px(s * s);
        DrawMark(px.data(), s, RGB(0, 0, 0));
        BITMAPINFOHEADER h{ sizeof(h), s, s * 2, 1, 32, BI_RGB };  // height counts the AND mask
        std::vector<BYTE> img((BYTE*)&h, (BYTE*)&h + sizeof(h));
        for (int y = s - 1; y >= 0; --y)  // ICO bitmaps are bottom-up
            img.insert(img.end(), (BYTE*)&px[y * s], (BYTE*)&px[y * s + s]);
        img.resize(img.size() + ((s + 31) / 32) * 4 * s, 0);  // AND mask, all opaque (alpha decides)
        images.push_back(std::move(img));
    }
    FILE* f = _wfopen(argv[1], L"wb");
    if (!f) return 1;
    WORD header[3] = { 0, 1, (WORD)n };
    fwrite(header, sizeof(header), 1, f);
    DWORD offset = 6 + 16 * n;
    for (int i = 0; i < n; ++i) {
        BYTE wh = (BYTE)sizes[i];
        BYTE e[16] = { wh, wh, 0, 0, 1, 0, 32, 0 };
        DWORD size = (DWORD)images[i].size();
        memcpy(e + 8, &size, 4);
        memcpy(e + 12, &offset, 4);
        fwrite(e, 16, 1, f);
        offset += size;
    }
    for (auto& img : images) fwrite(img.data(), img.size(), 1, f);
    fclose(f);
    return 0;
}
