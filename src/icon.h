#pragma once
#include <windows.h>

enum class Kind { Mouse, Keyboard, Headset, Other, App };

// Renders a tray icon: a small device glyph on top and the battery percentage below.
// percent < 0 draws only the glyph. Caller owns the returned HICON (DestroyIcon).
HICON MakeTrayIcon(Kind kind, int percent, bool charging, int lowThreshold);

// True when the taskbar uses the light theme (icons must be dark).
bool TaskbarIsLight();
