// The app mark: four rounded boxes (58/42 columns, two equal rows) with wide gaps.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Fills a size x size top-down buffer of straight-alpha ARGB pixels.
void DrawMark(UINT32* px, int size, COLORREF color);
HICON CreateMarkIcon(int size, COLORREF color);
// Black on a light taskbar, white on a dark one.
COLORREF TaskbarMarkColor();
