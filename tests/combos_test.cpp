// Navigation tests on the real layouts from this machine plus edge cases.
#include "../src/combos.h"
#include "../src/layout.h"
#include "../src/store.h"
#include <cstdio>
#include <string>

static int g_fail = 0, g_pass = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (cond) ++g_pass;                                             \
        else { ++g_fail; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static GridInfo Grid(int rows, int cols, std::vector<int> rp, std::vector<int> cp, std::vector<std::vector<int>> map) {
    GridInfo g;
    g.rows = rows;
    g.cols = cols;
    g.rowPct = rp;
    g.colPct = cp;
    g.cellMap = map;
    return g;
}

static std::string Zones(const ComboGrid& g, int i) {
    if (i < 0) return "edge";
    std::string s;
    for (int z : g.combos[i].zones) s += (s.empty() ? "" : ",") + std::to_string(z);
    return s;
}

static int ByZones(const ComboGrid& g, const std::string& z) {
    for (int i = 0; i < (int)g.combos.size(); ++i)
        if (Zones(g, i) == z) return i;
    return -1;
}

static std::string Walk(const ComboGrid& g, const std::string& start, Dir d, int steps, bool jump = false) {
    int cur = ByZones(g, start);
    std::string out = Zones(g, cur);
    for (int i = 0; i < steps && cur >= 0; ++i) {
        cur = StepCombo(g, cur, d, jump);
        out += " > " + Zones(g, cur);
    }
    return out;
}

int main() {
    RECT laptop{ 0, 0, 2880, 1752 };

    // Custom layout 1 (laptop): 2x2, zones 0 2 / 1 3
    {
        auto zones = GridZones(Grid(2, 2, { 4935, 5065 }, { 5840, 4160 }, { { 0, 2 }, { 1, 3 } }), laptop, 21);
        CHECK(zones.size() == 4);
        CHECK(zones[0].actual.left == 21 && zones[0].actual.top == 21);
        CHECK(zones[0].actual.right == 1681 - 10 && zones[2].actual.left == 1681 + 10);
        ComboGrid g = BuildCombos(zones, 2);
        CHECK(g.combos.size() == 9);
        CHECK(Walk(g, "0", Dir::Right, 3) == "0 > 0,2 > 2 > edge");
        CHECK(Walk(g, "2", Dir::Left, 3) == "2 > 0,2 > 0 > edge");
        CHECK(Walk(g, "0", Dir::Down, 3) == "0 > 0,1 > 1 > edge");
        CHECK(Walk(g, "0,1", Dir::Right, 3) == "0,1 > 0,1,2,3 > 2,3 > edge");
        CHECK(Walk(g, "3", Dir::Up, 2) == "3 > 2,3 > 2");
        CHECK(Walk(g, "0", Dir::Right, 2, true) == "0 > 2 > edge");
        CHECK(Walk(g, "0,1", Dir::Right, 1, true) == "0,1 > 2,3");
        // Union rect of a pair spans the gap between zones.
        int top = ByZones(g, "0,2");
        CHECK(top >= 0 && g.combos[top].actual.left == 21 && g.combos[top].actual.right == 2880 - 21);
        // Matching: the zone rect itself matches, a maximized window matches the full combo.
        CHECK(MatchCombo(g, zones[3].actual) == ByZones(g, "3"));
        CHECK(MatchCombo(g, laptop) == ByZones(g, "0,1,2,3"));
        // Entry from the left edge at the bottom half picks zone 1.
        CHECK(Zones(g, EntryCombo(g, Dir::Right, 0.5, 1.0)) == "1");
        CHECK(Zones(g, EntryCombo(g, Dir::Left, 0.0, 1.0)) == "2,3");
        CHECK(Zones(g, ZoneComboAt(g, POINT{ 2000, 1500 })) == "3");
        // max_span = 1 means single zones only.
        CHECK(BuildCombos(zones, 1).combos.size() == 4);
    }

    // Custom layout 2 (LDN2416): 2x3, zones 0 2 4 / 1 3 5
    {
        auto zones = GridZones(Grid(2, 3, { 4996, 5004 }, { 4398, 2814, 2788 }, { { 0, 2, 4 }, { 1, 3, 5 } }), RECT{ 2880, 0, 4800, 1160 }, 0);
        ComboGrid g = BuildCombos(zones, 2);
        CHECK(g.combos.size() == 15);
        CHECK(Walk(g, "0", Dir::Right, 5) == "0 > 0,2 > 2 > 2,4 > 4 > edge");
        CHECK(Walk(g, "0,1", Dir::Right, 4) == "0,1 > 0,1,2,3 > 2,3 > 2,3,4,5 > 4,5");
        CHECK(Walk(g, "0", Dir::Right, 3, true) == "0 > 2 > 4 > edge");
        CHECK(Walk(g, "0,2", Dir::Right, 2, true) == "0,2 > 4 > edge");
        CHECK(Walk(g, "5", Dir::Up, 3) == "5 > 4,5 > 4 > edge");
        ComboGrid g3 = BuildCombos(zones, 3);
        CHECK(ByZones(g3, "0,2,4") >= 0);
        CHECK(Walk(g3, "0", Dir::Right, 6) == "0 > 0,2 > 2 > 0,2,4 > 2,4 > 4 > edge");
    }

    // Merged cells: zone 0 spans the top row.
    {
        auto zones = GridZones(Grid(2, 2, { 5000, 5000 }, { 5000, 5000 }, { { 0, 0 }, { 1, 2 } }), RECT{ 0, 0, 1000, 1000 }, 10);
        CHECK(zones.size() == 3);
        ComboGrid g = BuildCombos(zones, 2);
        CHECK(g.combos.size() == 5);  // 0, 1, 2, {1,2}, {0,1,2}
        CHECK(ByZones(g, "0,1") < 0);
        CHECK(Walk(g, "1", Dir::Right, 2) == "1 > 1,2 > 2");
        CHECK(Walk(g, "1", Dir::Up, 2) == "1 > 0,1,2 > 0");
        CHECK(Walk(g, "0", Dir::Right, 1) == "0 > 2");  // falls back to the nearest row
    }

    // Drag snapping hit areas: gap 21, overlap 20 px -> reach 41.
    {
        auto zones = GridZones(Grid(2, 2, { 4935, 5065 }, { 5840, 4160 }, { { 0, 2 }, { 1, 3 } }), RECT{ 0, 0, 2880, 1704 }, 21);
        ComboGrid g = BuildCombos(zones, 2);
        auto at = [&](int x, int y, int reach) { return Zones(g, ComboAtPoint(g, POINT{ x, y }, reach)); };
        // Zone 0 spans x 21..1671, zone 2 starts at 1691; rows meet around y 840.
        CHECK(at(800, 400, 41) == "0");
        CHECK(at(1681, 400, 41) == "0,2");          // in the gap: both zones
        CHECK(at(1660, 400, 41) == "0,2");          // 11 px inside zone 0: still both (overlap)
        CHECK(at(1620, 400, 41) == "0");            // 51 px inside: only zone 0
        CHECK(at(1681, 840, 41) == "0,1,2,3");      // where four zones meet
        CHECK(at(1650, 820, 41) == "0,1,2,3");      // near the corner, inside zone 0
        CHECK(at(1660, 400, 21) == "0");            // overlap off: only the gap counts
        CHECK(at(1681, 400, 21) == "0,2");
        CHECK(at(2875, 1700, 41) == "3");           // screen corner outside the zone margin
        // max_span 1: no multi-zone combos, so a gap falls back to the nearest zone.
        ComboGrid singles = BuildCombos(zones, 1);
        CHECK(ComboAtPoint(singles, POINT{ 1681, 400 }, 41) >= 0);
    }

    // Mapping windows from an old layout to a new one.
    {
        RECT work{ 0, 0, 2880, 1704 };
        ComboGrid old = BuildCombos(GridZones(Grid(2, 2, { 4935, 5065 }, { 5840, 4160 }, { { 0, 2 }, { 1, 3 } }), work, 21), 2);
        auto map = [&](const ComboGrid& to, const std::string& z) { return Zones(to, MapCombo(old, ByZones(old, z), to)); };
        // Only the gap and a divider changed: every window keeps its zones, even when the zone shrank a lot.
        ComboGrid dragged = BuildCombos(GridZones(Grid(2, 2, { 4935, 5065 }, { 3000, 7000 }, { { 0, 2 }, { 1, 3 } }), work, 8), 2);
        CHECK(map(dragged, "0") == "0");
        CHECK(map(dragged, "0,2") == "0,2");
        CHECK(map(dragged, "2,3") == "2,3");
        // A column added (zones renumbered row by row): windows go where they were on screen.
        ComboGrid three = BuildCombos(GridZones(Grid(2, 3, { 4935, 5065 }, { 2920, 2920, 4160 }, { { 0, 1, 2 }, { 3, 4, 5 } }), work, 21), 2);
        CHECK(map(three, "2,3") == "2,5");  // right half -> right column
        CHECK(map(three, "1") == "3,4");    // bottom-left -> bottom of the first two columns
        CHECK(map(three, "0,1,2,3") != "edge");
        // A different work area size (taskbar moved) keeps the same zones.
        ComboGrid shorter = BuildCombos(GridZones(Grid(2, 2, { 4935, 5065 }, { 5840, 4160 }, { { 0, 2 }, { 1, 3 } }), RECT{ 0, 0, 2880, 1600 }, 21), 2);
        CHECK(map(shorter, "1,3") == "1,3");
    }

    // Save / apply round trip on a temp copy of FancyZones-style files.
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring dir = std::wstring(tmp) + L"tessel-store-test";
        CreateDirectoryW(dir.c_str(), nullptr);
        DeleteFileW((dir + L"\\custom-layouts.json.tessel-backup").c_str());
        DeleteFileW((dir + L"\\applied-layouts.json.tessel-backup").c_str());
        auto put = [&](const wchar_t* name, const char* text) {
            HANDLE f = CreateFileW((dir + L"\\" + name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD n;
            WriteFile(f, text, (DWORD)strlen(text), &n, nullptr);
            CloseHandle(f);
        };
        put(L"custom-layouts.json", R"({"custom-layouts":[{"uuid":"{39588577-4466-4035-9643-D9BE8E8DA2F7}","name":"Custom layout 1","type":"grid",
            "info":{"rows":2,"columns":2,"rows-percentage":[4935,5065],"columns-percentage":[5840,4160],"cell-child-map":[[0,2],[1,3]],
            "show-spacing":true,"spacing":21,"sensitivity-radius":69}}]})");
        put(L"applied-layouts.json", R"({"applied-layouts":[{"device":{"monitor":"SDC419D","monitor-instance":"4&2ac30c55&0&UID8388688",
            "monitor-number":1,"serial-number":"0","virtual-desktop":"{B27700A4-93F7-4759-A833-32C06719D5E2}"},
            "applied-layout":{"uuid":"{39588577-4466-4035-9643-D9BE8E8DA2F7}","type":"custom","show-spacing":true,"spacing":21,"zone-count":3,"sensitivity-radius":69}}]})");

        MonCtx m;
        m.name = "SDC419D";
        m.instance = "4&2AC30C55&0&UID8388688";
        m.instanceRaw = "4&2ac30c55&0&UID8388688";
        m.number = 1;
        std::string vd = "{B27700A4-93F7-4759-A833-32C06719D5E2}", err;

        // Edit the existing layout: 3 columns, gap 8.
        std::string uuid = "{39588577-4466-4035-9643-D9BE8E8DA2F7}";
        GridInfo g = Grid(2, 3, { 5000, 5000 }, { 3000, 4000, 3000 }, { { 0, 1, 2 }, { 3, 4, 5 } });
        CHECK(SaveGridLayout(dir, m, vd, uuid, "Custom layout 1", g, 8, err));
        LayoutDB db;
        CHECK(LoadLayoutDB(dir, db, err));
        CHECK(db.custom.size() == 1 && db.custom[0].grid.cols == 3 && db.custom[0].grid.colPct[1] == 4000);
        CHECK(db.applied.size() == 1 && db.applied[0].spacing == 8 && db.applied[0].showSpacing && db.applied[0].zoneCount == 6);
        CHECK(db.applied[0].instance == m.instance);
        CHECK(GetFileAttributesW((dir + L"\\custom-layouts.json.tessel-backup").c_str()) != INVALID_FILE_ATTRIBUTES);

        // Gap 0 turns spacing off but keeps the last gap stored.
        CHECK(SaveGridLayout(dir, m, vd, uuid, "Custom layout 1", g, 0, err));
        CHECK(LoadLayoutDB(dir, db, err) && !db.applied[0].showSpacing && db.applied[0].spacing == 8);

        // New layout on another virtual desktop gets its own uuid and applied entry.
        std::string fresh;
        CHECK(SaveGridLayout(dir, m, "{00000000-0000-0000-0000-000000000001}", fresh, "Tessel layout 2", EvenGrid(2, 2), 16, err));
        CHECK(!fresh.empty() && fresh != uuid);
        CHECK(LoadLayoutDB(dir, db, err) && db.custom.size() == 2 && db.applied.size() == 2);

        // Apply the first layout back onto that desktop.
        CHECK(ApplyCustomLayout(dir, m, "{00000000-0000-0000-0000-000000000001}", uuid, err));
        CHECK(LoadLayoutDB(dir, db, err) && db.applied.size() == 2 && db.applied[1].uuid == uuid && db.applied[1].zoneCount == 6);
    }

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
