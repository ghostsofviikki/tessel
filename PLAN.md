# Tessel — a tiny FancyZones replacement with multi-zone Win+Arrow cycling

## 1. What FancyZones does today (studied)

**Keyboard snapping (needs "Override Windows Snap"):**

| Mode | Keys | Behavior |
|---|---|---|
| Zone index (**your current setting**) | Win+Left/Right | Moves the window to the previous/next zone by number. Always one zone. Win+Up/Down stay native (maximize/minimize). |
| Relative position | Win+Arrows | Moves to the neighboring zone in that direction. Always one zone. |
| Relative position | Win+Ctrl+Alt+Arrows | *Extends* the window into the neighboring zone. This is the only keyboard multi-zone feature. It only grows the window, you can't cycle with it, it uses a 4-key chord, and it doesn't work in zone index mode. |

**Mouse multi-zone snapping:** hover near the shared edge of two zones, hold Ctrl while dragging, or middle-click to add zones. You use this now (`mouseMiddleClickSpanningMultipleZones: true`).

**How it works inside** (PowerToys `src/modules/fancyzones/FancyZonesLib`):
- A low-level keyboard hook (`WH_KEYBOARD_LL`) catches Win+Arrow and swallows it before Windows Snap sees it. It then sends a dummy key so releasing Win doesn't open the Start menu.
- A window's zone is stored as a **zone index set** (a bitmask). A multi-zone snap uses the smallest rectangle that contains every zone in the set.
- Layouts live in `%LOCALAPPDATA%\Microsoft\PowerToys\FancyZones\*.json`. Grid layouts are rows and columns given as percentages ×100, plus a `cell-child-map` that says which zone owns each cell.
- Placement: restore the window if it's maximized, then `SetWindowPos`. The invisible DWM resize border is corrected using `DWMWA_EXTENDED_FRAME_BOUNDS`. When a window moves between monitors with different DPI, it's positioned twice.
- FancyZones runs inside the PowerToys runner. Running it means running PowerToys.exe, the settings host, and other modules, which together use tens of MB of RAM.

**Your layouts** (read from your config):

```
Laptop SDC419D — "Custom layout 1", 2x2      LDN2416 — "Custom layout 2", 2x3
 58% | 42%                                    44% | 28% | 28%
+-----+----+                                 +-----+----+----+
|  0  | 2  |                                 |  0  | 2  | 4  |
+-----+----+                                 +-----+----+----+
|  1  | 3  |                                 |  1  | 3  | 5  |
+-----+----+                                 +-----+----+----+
```

## 2. The new behavior: zone combinations by half-steps

Picture each arrow press as moving the window **half a cell** along one axis. Along one axis, the window covers either one cell or two neighboring cells:

- 2 columns → positions `L`, `L+R`, `R` (3 stops)
- 3 columns → `0`, `0-1`, `1`, `1-2`, `2` (5 stops)

Win+Left/Right changes the horizontal span. Win+Up/Down changes the vertical span. Combining the two axes gives every 1-zone, 2-zone, and 2x2 (4-zone) rectangle, which is what you asked for.

Your laptop (2x2) becomes a 3x3 map of 9 states:

```
               Win+Left  <------------>  Win+Right
                  L            L+R            R
 Win+Up   T     [0]          [0,2]          [2]
   ^      T+B   [0,1]       [0,1,2,3]      [2,3]       (middle = all 4 zones)
   v      B     [1]          [1,3]          [3]
 Win+Down
```

Example: a window in zone 0, press Win+Right → top half (0+2) → zone 2. Press Win+Down → right half (2+3) → zone 3.

The LDN2416 (2x3) has 5x3 = 15 states: 6 single zones, 4 horizontal pairs, 3 vertical pairs, and 2 squares of 4 zones.

**Rules:**
- **Finding the current state:** Tessel doesn't trust stored state. On every keypress it finds the combination that best matches the window's current rectangle (by overlap). So it keeps working if you moved the window with the mouse. A per-window hint (`SetProp`) only breaks ties.
- **Window not snapped:** the first press snaps it to the single zone under the window's center.
- **At an edge** (configurable):
  - Left/Right: go to the next monitor's first or last column (default), wrap around, or stop.
  - Up at the top: maximize. Down at the bottom: pass through to Windows (minimize).
- **Optional Win+Alt+Arrow:** jump a whole cell (zone 0 → zone 2) in one press, skipping the pair in between. Win+Ctrl+Arrow is not used because Windows uses it to switch virtual desktops.
- **Settings:** max span per axis (default 2; set 3 to also allow a full row on the 2x3), and which spans are allowed.
- **Merged cells and canvas layouts:** Tessel builds a grid from every distinct zone edge. It only offers rectangles that are exactly a union of whole zones. This works for grid layouts with merged cells and for canvas layouts with no overlaps.
- Win+Shift+Arrow (move to another monitor) is left to Windows.

## 3. The program

- **Language:** C++20, plain Win32, no framework. The target is an exe of about 200 KB that uses about 3 MB of RAM, has a tray icon, and runs no services.
- **Layouts:** Tessel reads FancyZones' JSON files (`custom-layouts.json`, `applied-layouts.json`) directly. So you can keep using the FancyZones editor to design layouts: open PowerToys briefly, edit, close it. Tessel watches the folder and reloads when files change.
- **Monitors:** each entry in `applied-layouts` is matched to a real monitor by `monitor` + `monitor-instance` (for example `SDC419D` + `4&2ac30c55&0&UID8388688`), using `EnumDisplayDevices` device IDs and the current virtual desktop GUID.
- **Zone rectangles:** Tessel copies FancyZones' grid and spacing math so rectangles match exactly. Your laptop layout uses spacing 21 px. Note that `zone-count` in `applied-layouts` is stale for custom layouts and is ignored.
- **Settings:** `tessel.ini` next to the exe sets edge behavior, max span, Win+Alt jump, excluded apps, overlay flash, and autostart.
- **You must not run both.** Turn PowerToys off, or at least FancyZones' Override Windows Snap, because both install the same keyboard hook.

**Source files:**

```
src/main.cpp        tray icon, message loop, single-instance mutex
src/hook.cpp        WH_KEYBOARD_LL, Win+Arrow capture, Start-menu suppression
src/config.cpp      FancyZones JSON + tessel.ini loading, folder watcher
src/monitors.cpp    monitor enum, device-ID matching, work areas, DPI
src/layout.cpp      zones -> pixel rects (FancyZones-compatible math)
src/combos.cpp      edge grid, valid combinations, half-step navigation (pure, unit-tested)
src/snap.cpp        window filtering, restore/SetWindowPos, DWM frame correction
src/overlay.cpp     brief translucent flash of the target rectangle (optional)
third_party/json.h  small single-header JSON parser
tests/combos_test.cpp
```

**Windows to skip:** tool windows, popups and child windows, cloaked windows, fullscreen windows, windows that can't be resized, the desktop and taskbar, and excluded apps.

**Admin windows:** Windows blocks a normal program from moving elevated windows (such as Task Manager). To snap those, there's an optional "run elevated at logon" mode that uses Task Scheduler. Otherwise autostart uses the HKCU Run key.

## 4. Milestones

1. **Toolchain and skeleton.** Tray app, keyboard hook that catches Win+Arrow, no Start-menu flash, clean exit.
2. **Layouts and monitors.** Load your JSON and match both monitors. A `--dump` command prints each zone's rectangle; check them against what FancyZones produces.
3. **Core feature.** Build combinations, half-step navigation, and placement. Unit tests on your two real layouts.
4. **Polish.** Overlay flash, cross-monitor edges, `tessel.ini`, folder watching, exclusions, autostart.
5. **Optional: mouse drag snapping.** Shift-drag overlay, Ctrl or middle-click for multiple zones, two zones when hovering a shared edge. Without this step, closing PowerToys also removes mouse snapping. This is the largest piece of work (overlay window, `EVENT_SYSTEM_MOVESIZESTART/END`, mouse hook).

## 5. Testing

- **Unit:** generate combinations and navigate them on 2x2, 2x3, merged-cell, and canvas layouts; check edge rules and the overlap matching.
- **Manual:** Explorer, Chrome or Edge, Windows Terminal, Electron apps such as Claude, maximized windows, both monitors at different DPI, an elevated window, and checking that the Start menu doesn't open after Win+Arrow.

## 6. Prerequisites and open decisions

- **Compiler:** this machine has only the .NET 10 SDK and no C++ compiler. Recommended: install Visual Studio 2022 Build Tools with the "Desktop development with C++" workload:
  `winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"`
  A lighter option is llvm-mingw (about 300 MB, no Visual Studio).
- **Mouse snapping (milestone 5):** include it in v1, or ship keyboard-only first and keep PowerToys available for occasional drag snapping?
