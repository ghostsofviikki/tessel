// Full-screen grid layout editor: drag dividers, change the gap, add/remove rows and columns.
#pragma once
#include "layout.h"
#include <functional>

struct EditorRequest {
    MonCtx mon;                    // monitor being edited (copy)
    std::string uuid, name;        // empty uuid = new layout
    GridInfo grid;
    int spacing = 0;
    COLORREF color = RGB(0, 120, 215);  // zone color, saved with the layout
};

// Shows the Windows color dialog; returns true and updates color if the user picked one.
bool PickColor(HWND owner, COLORREF& color);

// onSave runs on the UI thread with the edited layout when the user saves.
void OpenEditor(HINSTANCE inst, const EditorRequest& req, std::function<void(const EditorRequest&)> onSave);
bool EditorOpen();
