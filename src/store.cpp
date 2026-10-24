#include "store.h"
#include "json.h"
#include <objbase.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

static std::string Upper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

int ZoneCount(const GridInfo& g) {
    std::set<int> ids;
    for (auto& row : g.cellMap) ids.insert(row.begin(), row.end());
    return (int)ids.size();
}

static bool ReadJson(const std::wstring& path, JVal& v, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { v = JVal{}; v.t = JVal::Obj; return true; }  // a missing file starts empty
    std::stringstream ss;
    ss << f.rdbuf();
    if (!JParse(ss.str(), v) || v.t != JVal::Obj) { err = "could not parse existing layout file"; return false; }
    return true;
}

// The very first write keeps a copy of the original file next to it (*.tessel-backup).
static bool WriteJson(const std::wstring& path, const JVal& v, std::string& err) {
    CopyFileW(path.c_str(), (path + L".tessel-backup").c_str(), TRUE);
    std::string text;
    JWrite(v, text);
    text += '\n';
    std::wstring tmp = path + L".tessel-tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = "could not write layout file"; return false; }
    DWORD n = 0;
    BOOL ok = WriteFile(f, text.data(), (DWORD)text.size(), &n, nullptr) && n == text.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        err = "could not replace layout file";
        return false;
    }
    return true;
}

static std::string NewUuid() {
    GUID g;
    CoCreateGuid(&g);
    wchar_t w[40];
    StringFromGUID2(g, w, 40);
    std::string s;
    for (wchar_t* p = w; *p; ++p) s += (char)*p;
    return s;
}

static JVal& ArrayAt(JVal& root, const char* key) {
    JVal& a = root.at(key);
    if (a.t != JVal::Arr) { a = JVal{}; a.t = JVal::Arr; }
    return a;
}

static JVal* FindCustom(JVal& root, const std::string& uuid) {
    for (auto& v : ArrayAt(root, "custom-layouts").a)
        if (Upper(v.strAt("uuid")) == Upper(uuid)) return &v;
    return nullptr;
}

// Finds or creates the applied-layouts entry for this monitor on this virtual desktop.
static JVal& AppliedEntry(JVal& root, const MonCtx& m, const std::string& vd) {
    JVal& arr = ArrayAt(root, "applied-layouts");
    for (auto& v : arr.a) {
        const JVal* dev = v.get("device");
        if (dev && dev->strAt("monitor") == m.name && Upper(dev->strAt("monitor-instance")) == m.instance &&
            Upper(dev->strAt("virtual-desktop")) == Upper(vd))
            return v;
    }
    JVal e;
    JVal& dev = e.at("device");
    dev.at("monitor") = JVal::String(m.name);
    dev.at("monitor-instance") = JVal::String(m.instanceRaw);
    dev.at("monitor-number") = JVal::Int(m.number);
    dev.at("serial-number") = JVal::String("");
    dev.at("virtual-desktop") = JVal::String(vd);
    arr.a.push_back(std::move(e));
    return arr.a.back();
}

static void SetApplied(JVal& entry, const std::string& uuid, bool showSpacing, int spacing, int zones, int sensitivity) {
    JVal& a = entry.at("applied-layout");
    a.at("uuid") = JVal::String(uuid);
    a.at("type") = JVal::String("custom");
    a.at("show-spacing") = JVal::Boolean(showSpacing);
    a.at("spacing") = JVal::Int(spacing);
    a.at("zone-count") = JVal::Int(zones);
    a.at("sensitivity-radius") = JVal::Int(sensitivity);
}

bool SaveGridLayout(const std::wstring& dir, const MonCtx& m, const std::string& vd, std::string& uuid,
                    const std::string& name, const GridInfo& g, int spacing, std::string& err) {
    JVal custom, applied;
    if (!ReadJson(dir + L"\\custom-layouts.json", custom, err) || !ReadJson(dir + L"\\applied-layouts.json", applied, err)) return false;

    JVal* layout = uuid.empty() ? nullptr : FindCustom(custom, uuid);
    if (!layout) {
        if (uuid.empty()) uuid = NewUuid();
        JVal fresh;
        fresh.at("uuid") = JVal::String(uuid);
        fresh.at("name") = JVal::String(name);
        fresh.at("type") = JVal::String("grid");
        ArrayAt(custom, "custom-layouts").a.push_back(std::move(fresh));
        layout = &ArrayAt(custom, "custom-layouts").a.back();
    }
    layout->at("type") = JVal::String("grid");
    JVal& info = layout->at("info");
    int sensitivity = info.intAt("sensitivity-radius", 20);
    info.at("rows") = JVal::Int(g.rows);
    info.at("columns") = JVal::Int(g.cols);
    info.at("rows-percentage") = JVal::IntArray(g.rowPct);
    info.at("columns-percentage") = JVal::IntArray(g.colPct);
    JVal map;
    map.t = JVal::Arr;
    for (auto& row : g.cellMap) map.a.push_back(JVal::IntArray(row));
    info.at("cell-child-map") = std::move(map);
    // FancyZones keeps the last gap even when spacing is off; keep that so toggling back restores it.
    bool show = spacing > 0;
    info.at("show-spacing") = JVal::Boolean(show);
    if (show) info.at("spacing") = JVal::Int(spacing);
    int storedSpacing = info.intAt("spacing", spacing);
    info.at("sensitivity-radius") = JVal::Int(sensitivity);

    // Every monitor using this layout gets the new gap and zone count, plus this monitor gets the layout.
    int zones = ZoneCount(g);
    for (auto& e : ArrayAt(applied, "applied-layouts").a) {
        JVal* a = e.get("applied-layout");
        if (a && Upper(a->strAt("uuid")) == Upper(uuid)) SetApplied(e, uuid, show, storedSpacing, zones, a->intAt("sensitivity-radius", sensitivity));
    }
    SetApplied(AppliedEntry(applied, m, vd), uuid, show, storedSpacing, zones, sensitivity);

    return WriteJson(dir + L"\\custom-layouts.json", custom, err) && WriteJson(dir + L"\\applied-layouts.json", applied, err);
}

bool DeleteCustomLayout(const std::wstring& dir, const std::string& uuid, std::string& err) {
    JVal custom, applied;
    if (!ReadJson(dir + L"\\custom-layouts.json", custom, err) || !ReadJson(dir + L"\\applied-layouts.json", applied, err)) return false;
    for (auto& e : ArrayAt(applied, "applied-layouts").a) {
        const JVal* a = e.get("applied-layout");
        if (a && Upper(a->strAt("uuid")) == Upper(uuid)) { err = "it is still used by a monitor or virtual desktop"; return false; }
    }
    auto& list = ArrayAt(custom, "custom-layouts").a;
    auto it = std::find_if(list.begin(), list.end(), [&](const JVal& v) { return Upper(v.strAt("uuid")) == Upper(uuid); });
    if (it == list.end()) { err = "layout not found"; return false; }
    list.erase(it);
    return WriteJson(dir + L"\\custom-layouts.json", custom, err);
}

bool ApplyCustomLayout(const std::wstring& dir, const MonCtx& m, const std::string& vd, const std::string& uuid, std::string& err) {
    JVal custom, applied;
    if (!ReadJson(dir + L"\\custom-layouts.json", custom, err) || !ReadJson(dir + L"\\applied-layouts.json", applied, err)) return false;
    JVal* layout = FindCustom(custom, uuid);
    if (!layout || !layout->get("info")) { err = "layout not found"; return false; }
    const JVal& info = *layout->get("info");
    int zones = 0;
    if (layout->strAt("type") == "grid") {
        GridInfo g;
        if (auto a = info.get("cell-child-map"))
            for (auto& row : a->a) {
                std::vector<int> r;
                for (auto& x : row.a) r.push_back(x.asInt());
                g.cellMap.push_back(r);
            }
        zones = ZoneCount(g);
    } else if (auto z = info.get("zones")) {
        zones = (int)z->a.size();
    }
    SetApplied(AppliedEntry(applied, m, vd), layout->strAt("uuid"), info.boolAt("show-spacing"), info.intAt("spacing"), zones,
               info.intAt("sensitivity-radius", 20));
    return WriteJson(dir + L"\\applied-layouts.json", applied, err);
}
