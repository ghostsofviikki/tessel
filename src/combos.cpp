#include "combos.h"
#include <algorithm>
#include <climits>
#include <cmath>

static long long Area(const RECT& r) {
    return (long long)std::max(0L, r.right - r.left) * std::max(0L, r.bottom - r.top);
}

static RECT Intersect(const RECT& a, const RECT& b) {
    return RECT{ std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom) };
}

static RECT Union(const RECT& a, const RECT& b) {
    return RECT{ std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom) };
}

double IoU(const RECT& a, const RECT& b) {
    long long i = Area(Intersect(a, b));
    long long u = Area(a) + Area(b) - i;
    return u > 0 ? (double)i / (double)u : 0.0;
}

static int EdgeIndex(const std::vector<int>& v, int val) {
    return (int)(std::lower_bound(v.begin(), v.end(), val) - v.begin());
}

static int Width(Span s) { return s.b - s.a + 1; }

ComboGrid BuildCombos(const std::vector<Zone>& zones, int maxSpan) {
    ComboGrid g;
    if (zones.empty()) return g;
    for (auto& z : zones) {
        g.xs.push_back(z.logical.left);
        g.xs.push_back(z.logical.right);
        g.ys.push_back(z.logical.top);
        g.ys.push_back(z.logical.bottom);
    }
    std::sort(g.xs.begin(), g.xs.end());
    g.xs.erase(std::unique(g.xs.begin(), g.xs.end()), g.xs.end());
    std::sort(g.ys.begin(), g.ys.end());
    g.ys.erase(std::unique(g.ys.begin(), g.ys.end()), g.ys.end());
    int nx = (int)g.xs.size() - 1, ny = (int)g.ys.size() - 1;

    // Every zone on its own is always a valid target, whatever its size.
    for (auto& z : zones) {
        Span sx{ EdgeIndex(g.xs, z.logical.left), EdgeIndex(g.xs, z.logical.right) - 1 };
        Span sy{ EdgeIndex(g.ys, z.logical.top), EdgeIndex(g.ys, z.logical.bottom) - 1 };
        if (FindCombo(g, sx, sy) >= 0) continue;  // overlapping duplicates
        g.combos.push_back(Combo{ sx, sy, { z.id }, z.logical, z.actual });
    }

    // Multi-zone rectangles that are an exact union of whole zones.
    for (int x0 = 0; x0 < nx; ++x0)
        for (int x1 = x0; x1 < nx && x1 - x0 < maxSpan; ++x1)
            for (int y0 = 0; y0 < ny; ++y0)
                for (int y1 = y0; y1 < ny && y1 - y0 < maxSpan; ++y1) {
                    Span sx{ x0, x1 }, sy{ y0, y1 };
                    if (FindCombo(g, sx, sy) >= 0) continue;
                    RECT L{ g.xs[x0], g.ys[y0], g.xs[x1 + 1], g.ys[y1 + 1] };
                    Combo c{ sx, sy, {}, L, {} };
                    long long sum = 0;
                    bool bad = false;
                    for (auto& z : zones) {
                        long long in = Area(Intersect(z.logical, L));
                        if (in == 0) continue;
                        if (in != Area(z.logical)) { bad = true; break; }
                        sum += in;
                        c.actual = c.zones.empty() ? z.actual : Union(c.actual, z.actual);
                        c.zones.push_back(z.id);
                    }
                    if (bad || c.zones.size() < 2 || sum != Area(L)) continue;
                    std::sort(c.zones.begin(), c.zones.end());
                    g.combos.push_back(std::move(c));
                }

    auto order = [](Span l, Span r) {
        if (l.a + l.b != r.a + r.b) return l.a + l.b < r.a + r.b;
        return Width(l) < Width(r);
    };
    for (auto& c : g.combos) {
        if (std::find(g.spansX.begin(), g.spansX.end(), c.x) == g.spansX.end()) g.spansX.push_back(c.x);
        if (std::find(g.spansY.begin(), g.spansY.end(), c.y) == g.spansY.end()) g.spansY.push_back(c.y);
    }
    std::sort(g.spansX.begin(), g.spansX.end(), order);
    std::sort(g.spansY.begin(), g.spansY.end(), order);
    return g;
}

int FindCombo(const ComboGrid& g, Span x, Span y) {
    for (int i = 0; i < (int)g.combos.size(); ++i)
        if (g.combos[i].x == x && g.combos[i].y == y) return i;
    return -1;
}

int StepCombo(const ComboGrid& g, int cur, Dir d, bool jump) {
    const Combo& c = g.combos[cur];
    bool horiz = d == Dir::Left || d == Dir::Right;
    bool fwd = d == Dir::Right || d == Dir::Down;
    const auto& spans = horiz ? g.spansX : g.spansY;
    Span mine = horiz ? c.x : c.y;
    Span other = horiz ? c.y : c.x;

    int i = (int)(std::find(spans.begin(), spans.end(), mine) - spans.begin());
    std::vector<Span> ahead;
    if (fwd) for (int k = i + 1; k < (int)spans.size(); ++k) ahead.push_back(spans[k]);
    else for (int k = i - 1; k >= 0; --k) ahead.push_back(spans[k]);
    if (jump) {
        ahead.erase(std::remove_if(ahead.begin(), ahead.end(), [&](Span s) { return fwd ? s.a <= mine.b : s.b >= mine.a; }), ahead.end());
        // Prefer a span of the same width, so a single zone jumps to the next single zone.
        std::stable_partition(ahead.begin(), ahead.end(), [&](Span s) { return Width(s) == Width(mine); });
    }
    auto find = [&](Span s, Span o) { return horiz ? FindCombo(g, s, o) : FindCombo(g, o, s); };

    for (Span s : ahead) {
        int r = find(s, other);
        if (r >= 0) return r;
    }
    // Merged cells can make the exact cross span unavailable; take the closest one.
    for (Span s : ahead) {
        int best = -1, bestCost = INT_MAX;
        for (int k = 0; k < (int)g.combos.size(); ++k) {
            const Combo& o = g.combos[k];
            if (!((horiz ? o.x : o.y) == s)) continue;
            Span os = horiz ? o.y : o.x;
            int cost = std::abs(os.a - other.a) + std::abs(os.b - other.b);
            if (cost < bestCost) { bestCost = cost; best = k; }
        }
        if (best >= 0) return best;
    }
    return -1;
}

void CrossSpan(const ComboGrid& g, int cur, Dir d, double& n0, double& n1) {
    const Combo& c = g.combos[cur];
    bool horiz = d == Dir::Left || d == Dir::Right;
    const auto& e = horiz ? g.ys : g.xs;
    double lo = e.front(), len = std::max(1, e.back() - e.front());
    n0 = ((horiz ? c.logical.top : c.logical.left) - lo) / len;
    n1 = ((horiz ? c.logical.bottom : c.logical.right) - lo) / len;
}

int EntryCombo(const ComboGrid& g, Dir travel, double n0, double n1) {
    if (g.combos.empty()) return -1;
    bool horiz = travel == Dir::Left || travel == Dir::Right;
    bool fwd = travel == Dir::Right || travel == Dir::Down;
    const auto& spans = horiz ? g.spansX : g.spansY;
    Span entry = fwd ? spans.front() : spans.back();
    const auto& e = horiz ? g.ys : g.xs;
    double lo = e.front(), len = std::max(1, e.back() - e.front());

    int best = -1;
    double bestScore = -1e9;
    for (int k = 0; k < (int)g.combos.size(); ++k) {
        const Combo& c = g.combos[k];
        if (!((horiz ? c.x : c.y) == entry)) continue;
        double c0 = ((horiz ? c.logical.top : c.logical.left) - lo) / len;
        double c1 = ((horiz ? c.logical.bottom : c.logical.right) - lo) / len;
        double inter = std::max(0.0, std::min(n1, c1) - std::max(n0, c0));
        double uni = std::max(n1, c1) - std::min(n0, c0);
        double score = uni > 0 ? inter / uni : 0;
        score -= std::fabs((c0 + c1) - (n0 + n1)) * 1e-3;  // tie-break on center distance
        if (score > bestScore) { bestScore = score; best = k; }
    }
    return best;
}

int MatchCombo(const ComboGrid& g, const RECT& r, double* iou) {
    int best = -1;
    double bestIoU = 0;
    for (int k = 0; k < (int)g.combos.size(); ++k) {
        double v = IoU(g.combos[k].actual, r);
        if (v > bestIoU) { bestIoU = v; best = k; }
    }
    if (iou) *iou = bestIoU;
    return best;
}

int MapCombo(const ComboGrid& from, int idx, const ComboGrid& to) {
    if (to.combos.empty()) return -1;
    const Combo& c = from.combos[idx];
    if (from.xs.size() == to.xs.size() && from.ys.size() == to.ys.size()) {
        int k = FindCombo(to, c.x, c.y);
        if (k >= 0 && to.combos[k].zones == c.zones) return k;
    }
    // Compare in layout-relative coordinates (0..1), so a different work area size doesn't matter.
    auto norm = [](const ComboGrid& g, const RECT& r, double out[4]) {
        double x0 = g.xs.front(), y0 = g.ys.front();
        double w = std::max(1, g.xs.back() - g.xs.front()), h = std::max(1, g.ys.back() - g.ys.front());
        out[0] = (r.left - x0) / w;
        out[1] = (r.top - y0) / h;
        out[2] = (r.right - x0) / w;
        out[3] = (r.bottom - y0) / h;
    };
    double a[4], b[4];
    norm(from, c.logical, a);
    int best = -1;
    double bestIoU = -1;
    for (int k = 0; k < (int)to.combos.size(); ++k) {
        norm(to, to.combos[k].logical, b);
        double iw = std::max(0.0, std::min(a[2], b[2]) - std::max(a[0], b[0]));
        double ih = std::max(0.0, std::min(a[3], b[3]) - std::max(a[1], b[1]));
        double inter = iw * ih;
        double uni = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter;
        double v = uni > 0 ? inter / uni : 0;
        if (v > bestIoU) { bestIoU = v; best = k; }
    }
    return best;
}

int ComboAtPoint(const ComboGrid& g, POINT p, int reach) {
    RECT bound{};
    int picked = 0;
    for (auto& c : g.combos) {
        if (c.zones.size() != 1) continue;
        const RECT& r = c.actual;
        long dx = std::max({ r.left - p.x, 0L, p.x - r.right }), dy = std::max({ r.top - p.y, 0L, p.y - r.bottom });
        if (std::max(dx, dy) > reach) continue;
        bound = picked++ ? Union(bound, c.logical) : c.logical;
    }
    if (picked == 0) return ZoneComboAt(g, p);
    Span sx{ EdgeIndex(g.xs, bound.left), EdgeIndex(g.xs, bound.right) - 1 };
    Span sy{ EdgeIndex(g.ys, bound.top), EdgeIndex(g.ys, bound.bottom) - 1 };
    int k = FindCombo(g, sx, sy);
    return k >= 0 ? k : ZoneComboAt(g, p);
}

int ZoneComboAt(const ComboGrid& g, POINT p) {
    int best = -1;
    long long bestArea = LLONG_MAX, bestDist = LLONG_MAX;
    for (int k = 0; k < (int)g.combos.size(); ++k) {
        const Combo& c = g.combos[k];
        if (c.zones.size() != 1) continue;
        const RECT& r = c.actual;
        if (PtInRect(&r, p)) {
            if (Area(r) < bestArea) { bestArea = Area(r); best = k; bestDist = -1; }
        } else if (bestDist >= 0) {
            long long dx = (r.left + r.right) / 2 - p.x, dy = (r.top + r.bottom) / 2 - p.y;
            if (dx * dx + dy * dy < bestDist) { bestDist = dx * dx + dy * dy; best = k; }
        }
    }
    return best;
}
