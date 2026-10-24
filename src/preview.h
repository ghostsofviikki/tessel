// See-through view of every monitor's zones over the desktop.
#pragma once
#include "layout.h"

// Closes on a click, any key, switching away, or after a few seconds.
void ShowZonePreview(HINSTANCE inst, const std::vector<MonCtx>& mons, COLORREF color);
