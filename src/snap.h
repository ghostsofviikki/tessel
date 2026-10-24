// Window filtering and placement.
#pragma once
#include "combos.h"
#include <string>

bool IsSnappable(HWND w, const std::vector<std::wstring>& excluded);
RECT VisualRect(HWND w);  // window rect without the invisible DWM resize border
void PlaceWindow(HWND w, const RECT& target);
bool IsSelfElevated();
std::wstring ProcessName(HWND w);  // e.g. notepad.exe
std::wstring ProcessPath(HWND w);  // full exe path
