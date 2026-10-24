# Tessel

A small tray app for zone snapping on Windows. **Win+Arrow** moves a window through zone *combinations* (single zones, pairs, and 2x2 blocks), not just one zone at a time, and drag + right-click snaps with the mouse. It started as a lighter replacement for PowerToys FancyZones, uses the same layout file format, and doesn't need PowerToys installed. See [PLAN.md](PLAN.md) for the original design.

## Layouts folder

Layouts are stored in `%LOCALAPPDATA%\Tessel\layouts` (`custom-layouts.json`, `applied-layouts.json`).
- **First run:** Tessel copies in any existing FancyZones layouts, once. After that it never touches PowerToys' folder, so you can uninstall PowerToys.
- **Sharing with FancyZones:** set `layouts_dir` in `tessel.ini` to `%LOCALAPPDATA%\Microsoft\PowerToys\FancyZones` to keep using the same files as FancyZones.

## Keys

| Keys | Action |
|---|---|
| Win+Left / Win+Right | Move half a zone sideways: zone → pair → next zone. At the edge, go to the next monitor. |
| Win+Up / Win+Down | Move half a zone vertically. Win+Up at the top maximizes; Win+Down at the bottom minimizes. |
| Win+Alt+Arrow | Jump one whole zone, skipping the pair in between |
| Win+Shift+Arrow, Win+Ctrl+Arrow | Left to Windows (move to another monitor, switch virtual desktop) |

On a 2x2 layout (zones `0 2 / 1 3`), the window moves around this map:

```
          L        L+R          R
T        [0]      [0,2]        [2]
T+B     [0,1]   [0,1,2,3]     [2,3]
B        [1]      [1,3]        [3]
```

If the foreground window isn't snappable, the keys go to Windows' normal Snap. That covers tool windows, windows that can't be resized, fullscreen windows, excluded apps, elevated windows (unless Tessel itself runs elevated), and windows on a monitor with no layout.

## Changing layouts and the gap

Press **Win+Shift+Z**, or click **Edit layout & gap** in Settings. This opens a full-screen editor on the monitor under the mouse:

| Action | How |
|---|---|
| Resize rows/columns | Drag the white handles. Hold Shift to snap to 5% steps. |
| Gap between zones | The − / + buttons, the +/− keys, or the mouse wheel (Shift = 5 px steps). 0 turns spacing off. |
| Add / remove a column or row | The buttons, or C / R (Shift+C / Shift+R removes). Adding splits the widest one; removing folds the last one into its neighbor. |
| Even out sizes | Equalize, or E |
| Zone color | The Color swatch, or K. The color is used for the editor and the snap highlight. |
| Save / cancel | Enter or Save / Esc, Cancel, or right-click |

Each zone shows its size in pixels and percent while you edit.

**Windows follow the layout.** When a layout changes (from this editor, the Settings gallery, an edit to the layout files, or a work-area change such as a taskbar resize), windows that were in a zone or combination move into the matching zones of the new layout:
- If only sizes or the gap changed, each window keeps the same zones.
- If rows or columns were added or removed, each window goes to the zones closest to where it was.

Maximized and minimized windows are left alone. Turn this off with `reflow_windows=0` in `tessel.ini`.

## Snapping with the mouse

1. Drag a window by its title bar.
2. While holding the left button, click the **right** button. The zones of the monitor under the pointer appear. The zone you'll snap to is filled in; the others are outlines.
3. Let go of the left button to snap there. Right-click again during the drag to hide the zones and drop the window normally.

What you point at decides the target:
- **Inside a zone:** that zone.
- **In the gap between two zones:** both zones.
- **Where four zones meet:** all four.

**Zone snapping** (Settings → Mouse) extends that two- and four-zone area the given number of pixels into the zones, so you don't have to hit the thin gap exactly. Default: 20 px at 100% scaling, so it feels the same on every monitor. With it off, only the gap itself counts. The ini keys are `drag_snap`, `snap_area` and `snap_area_px`.

## New windows

When a window opens, Tessel moves it into the grid about a quarter of a second later, after the app has placed it:
- **The app's last zone:** if the app was last in a zone or combination on that monitor's current layout, the new window goes back there. Snapping with Win+Arrow and dragging a window into a zone with the mouse both count.
- **Closest fit:** otherwise it goes to the zone or combination closest to the window's own size and position.

Dialogs, maximized windows, tool windows and excluded apps are left alone, and each window is adjusted only once, so moving it afterwards sticks. Both behaviors can be turned off in Settings (`snap_new_windows`, `remember_app_zones`). The per-app history is kept in `%LOCALAPPDATA%\Tessel\history.ini`.

## Settings

Left-click the tray icon, or right-click it and choose **Settings**. The tray menu has only **Settings** and **Exit**.

- **Layout gallery:** shows your layouts and ready-made templates (2 or 3 columns, 2 × 2, 2 × 3, wide center, main + stack, center + corners, 2 rows). Click one to use it on the monitor; with several monitors, pick the monitor with the tabs above the gallery.
  - Choosing a template saves it as one of your layouts, so you can edit it.
  - Right-click one of your layouts to use, edit or delete it.
  - Scroll the gallery with the mouse wheel.
- **Edit layout & gap / New layout** open the editor.
- **Show zones** lays a see-through view of every monitor's zones over the desktop, with zone numbers and sizes. Click or press any key to close it.
- **Appearance:** zone color, and the highlight while snapping.
- **Behavior:**
  - Turn Win+Arrow snapping on or off.
  - Start with Windows.
  - Move windows when the layout changes.
  - Win+Alt+Arrow jumps.
  - What happens at the screen edge.
  - The largest combination.

The window follows the Windows light/dark theme. The tray icon is black on a light taskbar and white on a dark one.

Changes are saved to the layout files in the layouts folder (above). The first save keeps the original files as `*.tessel-backup` in the same folder. If a layout is used on several monitors, changing its gap changes it on all of them. Adding or removing a row or column turns merged cells back into a plain grid. Free-form (canvas) layouts imported from FancyZones can be switched to, but only grid layouts can be edited here.

## Build

```bash
build.cmd
```

`build.cmd` creates `bin\tessel.exe` (about 530 KB, no runtime needed) and runs the unit tests. Use `build.cmd debug` for a debug build.

## Use

1. **If PowerToys is installed, don't run FancyZones alongside Tessel.** Turn FancyZones off in PowerToys, or uninstall PowerToys. Both handle Win+Arrow and window drags, and whichever started last gets the keys first.
2. Run `bin\tessel.exe`. Click its tray icon to open Settings.
3. To edit layouts, use the built-in editor (below). Tessel also reloads automatically when the layout files change on disk.

Settings are in `bin\tessel.ini`, which is created on first run and reloaded when saved. They cover edge behavior, the largest combination size (`max_span`), the Win+Alt jump, the overlay, and excluded apps.

`tessel.exe --dump` writes `bin\tessel-dump.txt` with each monitor's matched layout, zone rectangles, and combinations.

## Limits

- Supported layout types: custom grid, custom canvas, and the `columns` / `rows` templates. Other templates (`grid`, `priority-grid`, `focus`) fall back to Windows Snap.
- Elevated windows (Task Manager, admin terminals) can only be moved if Tessel runs elevated.
- No mouse drag snapping. That's milestone 5 in the plan.
