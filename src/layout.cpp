#include "layout.h"
#include "json.h"
#include <shlobj.h>
#include <algorithm>
#include <fstream>
#include <sstream>

std::wstring FancyZonesDir() {
    wchar_t* p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) dir = std::wstring(p) + L"\\Microsoft\\PowerToys\\FancyZones";
    CoTaskMemFree(p);
    return dir;
}

std::wstring TesselLayoutsDir() {
    wchar_t* p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) dir = std::wstring(p) + L"\\Tessel\\layouts";
    CoTaskMemFree(p);
    if (dir.empty() || GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return dir;
    // First run: start from the user's FancyZones layouts, if there are any. Only once, so a later
    // FancyZones change or a deleted layout never comes back on its own.
    CreateDirectoryW(dir.substr(0, dir.find_last_of(L'\\')).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring fz = FancyZonesDir();
    for (const wchar_t* f : { L"\\custom-layouts.json", L"\\applied-layouts.json" })
        CopyFileW((fz + f).c_str(), (dir + f).c_str(), TRUE);
    return dir;
}

static bool ReadFile(const std::wstring& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static std::string Upper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static bool ParseCustom(const JVal& v, CustomLayout& l) {
    l.uuid = Upper(v.strAt("uuid"));
    l.name = v.strAt("name");
    const JVal* info = v.get("info");
    if (!info) return false;
    std::string type = v.strAt("type");
    if (type == "grid") {
        l.isGrid = true;
        GridInfo& g = l.grid;
        g.rows = info->intAt("rows");
        g.cols = info->intAt("columns");
        if (auto a = info->get("rows-percentage")) for (auto& x : a->a) g.rowPct.push_back(x.asInt());
        if (auto a = info->get("columns-percentage")) for (auto& x : a->a) g.colPct.push_back(x.asInt());
        if (auto a = info->get("cell-child-map"))
            for (auto& row : a->a) {
                std::vector<int> r;
                for (auto& x : row.a) r.push_back(x.asInt());
                g.cellMap.push_back(r);
            }
        if (g.rows <= 0 || g.cols <= 0 || (int)g.rowPct.size() != g.rows || (int)g.colPct.size() != g.cols || (int)g.cellMap.size() != g.rows) return false;
        for (auto& r : g.cellMap) if ((int)r.size() != g.cols) return false;
        return true;
    }
    if (type == "canvas") {
        l.isGrid = false;
        l.canvas.refW = info->intAt("ref-width");
        l.canvas.refH = info->intAt("ref-height");
        if (auto a = info->get("zones"))
            for (auto& z : a->a) {
                int x = z.intAt("X"), y = z.intAt("Y");
                l.canvas.zones.push_back(RECT{ x, y, x + z.intAt("width"), y + z.intAt("height") });
            }
        return l.canvas.refW > 0 && l.canvas.refH > 0;
    }
    return false;
}

bool LoadLayoutDB(const std::wstring& dir, LayoutDB& db, std::string& err) {
    db = LayoutDB{};
    std::string text;
    JVal root;
    if (ReadFile(dir + L"\\custom-layouts.json", text) && JParse(text, root)) {
        if (auto arr = root.get("custom-layouts"))
            for (auto& v : arr->a) {
                CustomLayout l;
                if (ParseCustom(v, l)) db.custom.push_back(std::move(l));
            }
    }
    text.clear();
    root = JVal{};
    if (!ReadFile(dir + L"\\applied-layouts.json", text)) { err = "applied-layouts.json not found"; return false; }
    if (!JParse(text, root)) { err = "applied-layouts.json is not valid JSON"; return false; }
    if (auto arr = root.get("applied-layouts"))
        for (auto& v : arr->a) {
            const JVal* dev = v.get("device");
            const JVal* lay = v.get("applied-layout");
            if (!dev || !lay) continue;
            AppliedLayout a;
            a.monitor = dev->strAt("monitor");
            a.instance = Upper(dev->strAt("monitor-instance"));
            a.virtualDesktop = Upper(dev->strAt("virtual-desktop"));
            a.uuid = Upper(lay->strAt("uuid"));
            a.type = lay->strAt("type");
            a.showSpacing = lay->boolAt("show-spacing");
            a.spacing = lay->intAt("spacing");
            a.zoneCount = lay->intAt("zone-count");
            db.applied.push_back(std::move(a));
        }
    return true;
}

static std::string GuidFromRegistry(HKEY root, const wchar_t* path) {
    BYTE buf[16];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, path, L"CurrentVirtualDesktop", RRF_RT_REG_BINARY, nullptr, buf, &size) != ERROR_SUCCESS || size != 16) return {};
    GUID g;
    memcpy(&g, buf, 16);
    wchar_t w[40];
    StringFromGUID2(g, w, 40);
    std::string s;
    for (wchar_t* p = w; *p; ++p) s += (char)*p;
    return Upper(s);
}

std::string CurrentVirtualDesktop() {
    std::string vd = GuidFromRegistry(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops");
    if (!vd.empty()) return vd;
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::wstring p = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" + std::to_wstring(session) + L"\\VirtualDesktops";
    return GuidFromRegistry(HKEY_CURRENT_USER, p.c_str());
}

// Same math as FancyZones' CalculateGridZones: cumulative percentages, full spacing on
// outer edges and half spacing between zones.
std::vector<Zone> GridZones(const GridInfo& gi, RECT work, int spacing) {
    std::vector<Zone> zones;
    auto edges = [](const std::vector<int>& pct, int origin, int length) {
        std::vector<int> e{ origin };
        long long cum = 0;
        for (int p : pct) {
            cum += p;
            e.push_back(origin + (int)(cum * length / 10000));
        }
        return e;
    };
    std::vector<int> ys = edges(gi.rowPct, work.top, work.bottom - work.top);
    std::vector<int> xs = edges(gi.colPct, work.left, work.right - work.left);
    auto& m = gi.cellMap;
    for (int r = 0; r < gi.rows; ++r)
        for (int c = 0; c < gi.cols; ++c) {
            int id = m[r][c];
            bool topLeft = (r == 0 || m[r - 1][c] != id) && (c == 0 || m[r][c - 1] != id);
            if (!topLeft) continue;
            int maxRow = r, maxCol = c;
            while (maxRow + 1 < gi.rows && m[maxRow + 1][c] == id) ++maxRow;
            while (maxCol + 1 < gi.cols && m[r][maxCol + 1] == id) ++maxCol;
            RECT L{ xs[c], ys[r], xs[maxCol + 1], ys[maxRow + 1] };
            RECT A = L;
            A.left += c == 0 ? spacing : spacing / 2;
            A.top += r == 0 ? spacing : spacing / 2;
            A.right -= maxCol == gi.cols - 1 ? spacing : spacing / 2;
            A.bottom -= maxRow == gi.rows - 1 ? spacing : spacing / 2;
            zones.push_back(Zone{ id, L, A });
        }
    std::sort(zones.begin(), zones.end(), [](const Zone& a, const Zone& b) { return a.id < b.id; });
    return zones;
}

static std::vector<Zone> CanvasZones(const CanvasInfo& ci, RECT work) {
    std::vector<Zone> zones;
    double sx = (double)(work.right - work.left) / ci.refW, sy = (double)(work.bottom - work.top) / ci.refH;
    for (int i = 0; i < (int)ci.zones.size(); ++i) {
        const RECT& z = ci.zones[i];
        RECT r{ work.left + (int)(z.left * sx), work.top + (int)(z.top * sy), work.left + (int)(z.right * sx), work.top + (int)(z.bottom * sy) };
        zones.push_back(Zone{ i, r, r });
    }
    return zones;
}

GridInfo EvenGrid(int rows, int cols) {
    GridInfo g;
    g.rows = rows;
    g.cols = cols;
    auto split = [](int n) {
        std::vector<int> v(n, 10000 / n);
        v.back() += 10000 - (10000 / n) * n;
        return v;
    };
    g.rowPct = split(rows);
    g.colPct = split(cols);
    g.cellMap.assign(rows, std::vector<int>(cols));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) g.cellMap[r][c] = r * cols + c;
    return g;
}

std::vector<Zone> LayoutZones(const CustomLayout& l, RECT area, int spacing) {
    return l.isGrid ? GridZones(l.grid, area, spacing) : CanvasZones(l.canvas, area);
}

static GridInfo MakeGrid(std::vector<int> rowPct, std::vector<int> colPct, std::vector<std::vector<int>> map) {
    GridInfo g;
    g.rows = (int)rowPct.size();
    g.cols = (int)colPct.size();
    g.rowPct = std::move(rowPct);
    g.colPct = std::move(colPct);
    g.cellMap = std::move(map);
    return g;
}

const std::vector<LayoutTemplate>& LayoutTemplates() {
    static const std::vector<LayoutTemplate> list = {
        { "2 columns", EvenGrid(1, 2) },
        { "3 columns", EvenGrid(1, 3) },
        { "2 × 2 grid", EvenGrid(2, 2) },
        { "2 × 3 grid", EvenGrid(2, 3) },
        { "Wide center", MakeGrid({ 10000 }, { 2500, 5000, 2500 }, { { 0, 1, 2 } }) },
        { "Main + stack", MakeGrid({ 5000, 5000 }, { 6000, 4000 }, { { 0, 1 }, { 0, 2 } }) },
        { "Center + corners", MakeGrid({ 5000, 5000 }, { 2500, 5000, 2500 }, { { 0, 2, 3 }, { 1, 2, 4 } }) },
        { "2 rows", EvenGrid(2, 1) },
    };
    return list;
}

bool SameGrid(const GridInfo& a, const GridInfo& b) {
    return a.rows == b.rows && a.cols == b.cols && a.rowPct == b.rowPct && a.colPct == b.colPct && a.cellMap == b.cellMap;
}

static std::string Narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += (char)c;
    return s;
}

// \\?\DISPLAY#SDC419D#4&2ac30c55&0&UID8388688#{e6f07b5f-...}  ->  SDC419D, 4&2AC30C55&0&UID8388688
static void MonitorIds(const std::wstring& device, std::string& name, std::string& instance, std::string& instanceRaw) {
    DISPLAY_DEVICEW dd{ sizeof(dd) };
    for (DWORD i = 0; EnumDisplayDevicesW(device.c_str(), i, &dd, EDD_GET_DEVICE_INTERFACE_NAME); ++i, dd = DISPLAY_DEVICEW{ sizeof(dd) }) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
        std::string id = Narrow(dd.DeviceID);
        std::vector<std::string> parts;
        std::stringstream ss(id);
        std::string part;
        while (std::getline(ss, part, '#')) parts.push_back(part);
        if (parts.size() >= 3) {
            name = parts[1];
            instanceRaw = parts[2];
            instance = Upper(parts[2]);
            return;
        }
    }
}

static const AppliedLayout* FindApplied(const LayoutDB& db, const MonCtx& m, const std::string& vd) {
    const AppliedLayout* sameInstance = nullptr;
    const AppliedLayout* sameName = nullptr;
    for (auto& a : db.applied) {
        if (a.monitor.empty() || a.monitor != m.name) continue;
        if (!m.instance.empty() && a.instance == m.instance) {
            if (a.virtualDesktop == vd) return &a;
            if (!sameInstance) sameInstance = &a;
        }
        if (!sameName) sameName = &a;
    }
    return sameInstance ? sameInstance : sameName;
}

static BOOL CALLBACK EnumProc(HMONITOR h, HDC, LPRECT, LPARAM lp) {
    auto* list = (std::vector<MonCtx>*)lp;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(h, &mi)) return TRUE;
    MonCtx m;
    m.h = h;
    m.work = mi.rcWork;
    m.full = mi.rcMonitor;
    m.device = mi.szDevice;
    MonitorIds(m.device, m.name, m.instance, m.instanceRaw);
    m.number = (int)list->size() + 1;
    list->push_back(std::move(m));
    return TRUE;
}

std::vector<MonCtx> BuildMonitors(const LayoutDB& db, const std::string& vd, int maxSpan) {
    std::vector<MonCtx> list;
    EnumDisplayMonitors(nullptr, nullptr, EnumProc, (LPARAM)&list);
    for (auto& m : list) {
        const AppliedLayout* a = FindApplied(db, m, vd);
        if (!a) { m.status = "no layout applied to this monitor"; continue; }
        int spacing = a->showSpacing ? a->spacing : 0;
        m.uuid = a->uuid;
        m.type = a->type;
        m.spacing = spacing;
        if (a->type == "custom") {
            auto it = std::find_if(db.custom.begin(), db.custom.end(), [&](const CustomLayout& l) { return l.uuid == a->uuid; });
            if (it == db.custom.end()) { m.status = "custom layout " + a->uuid + " not found"; continue; }
            m.layoutName = it->name;
            m.zones = it->isGrid ? GridZones(it->grid, m.work, spacing) : CanvasZones(it->canvas, m.work);
        } else if ((a->type == "columns" || a->type == "rows") && a->zoneCount > 0) {
            m.layoutName = a->type + " (" + std::to_string(a->zoneCount) + ")";
            GridInfo g = a->type == "columns" ? EvenGrid(1, a->zoneCount) : EvenGrid(a->zoneCount, 1);
            m.zones = GridZones(g, m.work, spacing);
        } else {
            m.status = "layout type '" + a->type + "' is not supported (use a custom, columns or rows layout)";
            continue;
        }
        m.grid = BuildCombos(m.zones, maxSpan);
        m.ok = !m.grid.combos.empty();
        m.status = m.ok ? "ok" : "layout has no zones";
    }
    return list;
}
