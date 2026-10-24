// Settings window: layout gallery per monitor, zone color, startup and snapping options.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

void ShowSettings();
void SettingsRefresh();  // repaint after settings or layouts changed
void CloseSettings();
