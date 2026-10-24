// State and actions shared by the tray app, the settings window and the editor glue (defined in main.cpp).
#pragma once
#include "layout.h"
#include <string>
#include <vector>

struct Config {
    bool enabled = true;                     // Win+Arrow snapping on/off
    std::wstring edgeH = L"monitor";         // monitor | wrap | stop
    std::wstring upAtTop = L"maximize";      // maximize | stop
    std::wstring downAtBottom = L"minimize"; // minimize | stop
    int maxSpan = 2;
    bool altJump = true;
    bool overlay = true;
    int overlayMs = 200;
    COLORREF overlayColor = RGB(0x00, 0x78, 0xD7);  // zone color
    int overlayAlpha = 90;
    std::vector<std::wstring> excluded;
    std::wstring layoutsDir;
    std::wstring editorHotkey = L"win+shift+z";
    bool reflow = true;
    bool snapNew = true;        // move newly opened windows into the grid
    bool rememberZones = true;  // ...into the zone that app used last
    bool dragSnap = true;       // drag + right button shows zones, drop snaps
    bool snapArea = true;       // zone snapping: the two/four-zone area reaches into the zones...
    int snapAreaPx = 20;        // ...by this many pixels (at 100% scaling)
};

extern HINSTANCE g_inst;
extern Config g_cfg;
extern LayoutDB g_db;
extern std::vector<MonCtx> g_mons;
extern std::wstring g_iniPath;

void Reload();
void OpenEditorFor(size_t mon, bool fresh);
void ApplyLayoutFor(size_t mon, size_t layout);
void ApplyTemplateFor(size_t mon, size_t tmpl);
void DeleteLayout(size_t layout, HWND owner);
// Writes one tessel.ini value and reloads.
void SetSetting(const wchar_t* key, const std::wstring& value);
void SaveZoneColor(COLORREF c);
bool AutostartEnabled();
void SetAutostart(bool on);
size_t MonitorUnderCursor();
const CustomLayout* AppliedCustom(const MonCtx& m);
std::wstring Widen(const std::string& s);
