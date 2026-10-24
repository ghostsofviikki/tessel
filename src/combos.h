// Zone combinations and half-step navigation. Pure geometry, no window access.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>

enum class Dir { Left, Right, Up, Down };

// logical: rect without spacing (cells line up exactly); actual: rect a window is placed in.
struct Zone { int id; RECT logical; RECT actual; };

struct Span { int a, b; };  // inclusive cell range
inline bool operator==(Span l, Span r) { return l.a == r.a && l.b == r.b; }

struct Combo {
    Span x, y;
    std::vector<int> zones;  // sorted zone ids
    RECT logical, actual;
};

struct ComboGrid {
    std::vector<int> xs, ys;           // logical cell edges
    std::vector<Combo> combos;
    std::vector<Span> spansX, spansY;  // half-step order along each axis
};

ComboGrid BuildCombos(const std::vector<Zone>& zones, int maxSpan);
int FindCombo(const ComboGrid& g, Span x, Span y);
// Next combo in direction d, or -1 at the edge. jump: skip to the next non-overlapping span.
int StepCombo(const ComboGrid& g, int cur, Dir d, bool jump);
// Combo at the edge a window enters from when travelling in d (used for wrap and monitor hops).
// n0..n1 is the window's normalized span (0..1) on the other axis.
int EntryCombo(const ComboGrid& g, Dir travel, double n0, double n1);
// Normalized span of a combo on the axis perpendicular to d.
void CrossSpan(const ComboGrid& g, int cur, Dir d, double& n0, double& n1);
int MatchCombo(const ComboGrid& g, const RECT& r, double* iou = nullptr);
// The combination in `to` that corresponds to combination idx of `from` after a layout change:
// the same cells and zones when the grid shape is unchanged, otherwise the closest by relative position.
int MapCombo(const ComboGrid& from, int idx, const ComboGrid& to);
int ZoneComboAt(const ComboGrid& g, POINT p);
// Drag snapping: every zone within `reach` px of the point (reach = gap + overlap into the zones),
// as one combination. In a gap that's both zones, at a corner all four. Falls back to the zone
// under (or nearest) the point when the picked zones don't form a valid combination.
int ComboAtPoint(const ComboGrid& g, POINT p, int reach);
double IoU(const RECT& a, const RECT& b);
