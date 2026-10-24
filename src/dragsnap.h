// Mouse snapping: while dragging a window, the right button shows the zones; dropping snaps
// the window to the zone, gap (two zones) or corner (four zones) under the cursor.
#pragma once
#include "layout.h"
#include <functional>

// onSnap places the window and records it; it is called when a drop lands on a zone.
void DragSnapInit(HINSTANCE inst, std::function<void(HWND, const MonCtx&, int combo)> onSnap);
void DragSnapStart(HWND w);  // EVENT_SYSTEM_MOVESIZESTART
void DragSnapEnd(HWND w);    // EVENT_SYSTEM_MOVESIZEEND
// Test entry: runs a drag of w with the zones on, pointing at p, without real mouse input.
// keepOpen leaves the zones on screen (call again with keepOpen = false to drop).
void DragSnapTest(HWND w, POINT p, bool keepOpen);
// Pixels the two-zone/four-zone area reaches from a gap into the zones (0 = gap only), at this monitor's DPI.
int SnapReach(const MonCtx& m);
