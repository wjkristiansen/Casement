//================================================================================================
// Casement - backend factories and shared helpers (internal)
//================================================================================================

#pragma once

#include <Casement/Casement.h>

namespace Casement
{

std::unique_ptr<CDisplay> CreateWin32Display();
std::unique_ptr<CDisplay> CreateX11Display();
std::unique_ptr<CDisplay> CreateWaylandDisplay();

// Linux evdev key code (linux/input-event-codes.h). X11 key codes are evdev codes plus 8.
Key KeyFromEvdev(uint32_t code);

// PC scan code set 1, as found in bits 16-24 of a WM_KEYDOWN lParam.
Key KeyFromScanCode(uint32_t scanCode, bool extended);

} // namespace Casement
