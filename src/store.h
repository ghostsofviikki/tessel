// Writes layout files in the FancyZones format, so they stay interchangeable with FancyZones.
#pragma once
#include "layout.h"

int ZoneCount(const GridInfo& g);
// Creates (empty uuid) or updates a custom grid layout and applies it to the monitor.
// On success uuid holds the layout's id.
bool SaveGridLayout(const std::wstring& dir, const MonCtx& m, const std::string& vd, std::string& uuid,
                    const std::string& name, const GridInfo& g, int spacing, std::string& err);
// Removes a custom layout. Fails if any monitor or virtual desktop still uses it.
bool DeleteCustomLayout(const std::wstring& dir, const std::string& uuid, std::string& err);
// Points the monitor at an existing custom layout, using that layout's own gap.
bool ApplyCustomLayout(const std::wstring& dir, const MonCtx& m, const std::string& vd, const std::string& uuid, std::string& err);
