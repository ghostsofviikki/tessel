// Layout files (FancyZones format) reading, monitor matching and zone rect math.
#pragma once
#include "combos.h"
#include <string>

struct GridInfo {
    int rows = 0, cols = 0;
    std::vector<int> rowPct, colPct;       // parts of 10000
    std::vector<std::vector<int>> cellMap; // [row][col] -> zone id
};

struct CanvasInfo {
    int refW = 0, refH = 0;
    std::vector<RECT> zones;  // x, y, x+w, y+h in reference size
};

struct CustomLayout {
    std::string uuid, name;
    bool isGrid = true;
    GridInfo grid;
    CanvasInfo canvas;
};

struct AppliedLayout {
    std::string monitor, instance, virtualDesktop;
    std::string uuid, type;
    bool showSpacing = false;
    int spacing = 0, zoneCount = 0;
};

struct LayoutDB {
    std::vector<CustomLayout> custom;
    std::vector<AppliedLayout> applied;
};

struct MonCtx {
    HMONITOR h = nullptr;
    RECT work{}, full{};
    std::wstring device;          // \\.\DISPLAY1
    std::string name, instance;   // SDC419D, 4&2AC30C55&0&UID8388688 (upper-cased for matching)
    std::string instanceRaw;      // as Windows reports it, for writing new entries
    int number = 0;               // 1-based enumeration order
    std::string layoutName, status;
    std::string uuid, type;       // applied layout
    int spacing = 0;              // effective gap in px (0 when spacing is off)
    std::vector<Zone> zones;
    ComboGrid grid;
    bool ok = false;
};

std::wstring FancyZonesDir();
// %LOCALAPPDATA%\Tessel\layouts. Created on first use, seeded with any existing FancyZones layouts.
std::wstring TesselLayoutsDir();
bool LoadLayoutDB(const std::wstring& dir, LayoutDB& db, std::string& err);
std::string CurrentVirtualDesktop();
// Enumerates monitors and resolves each one's zones for the given virtual desktop.
std::vector<MonCtx> BuildMonitors(const LayoutDB& db, const std::string& vd, int maxSpan);
std::vector<Zone> GridZones(const GridInfo& gi, RECT work, int spacing);
GridInfo EvenGrid(int rows, int cols);  // equal splits, zones numbered row by row
// Zones of a custom layout (grid or canvas) inside area, e.g. for thumbnails.
std::vector<Zone> LayoutZones(const CustomLayout& l, RECT area, int spacing);

// Ready-made grid layouts offered in the settings gallery.
struct LayoutTemplate { std::string name; GridInfo grid; };
const std::vector<LayoutTemplate>& LayoutTemplates();
bool SameGrid(const GridInfo& a, const GridInfo& b);
