#pragma once
#include <windows.h>

enum class Kind { Mouse, Keyboard, Headset, Speaker, Gamepad, Other, App };

// Renders a tray icon: a small device glyph on top and the battery percentage below.
// percent < 0 draws only the glyph. Caller owns the returned HICON (DestroyIcon).
// Number color: charging = green; critical (< 15%) = red; with `colorLevels` also
// normal (>= 30%) = blue and low (15..29%) = orange, otherwise neutral.
HICON MakeTrayIcon(Kind kind, int percent, bool charging, bool colorLevels);

// True when the taskbar uses the light theme (icons must be dark).
bool TaskbarIsLight();
