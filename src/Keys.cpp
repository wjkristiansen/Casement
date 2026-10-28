//================================================================================================
// Casement - key names and native key code translation
//================================================================================================

#include "Backends.h"

#include <array>

namespace Casement
{

//------------------------------------------------------------------------------------------------
static constexpr const char *s_KeyNames[] =
{
    "Unknown",

    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",

    "Escape", "Enter", "Tab", "Backspace", "Space",
    "Minus", "Equal", "LeftBracket", "RightBracket", "Backslash",
    "Semicolon", "Apostrophe", "Grave", "Comma", "Period", "Slash",

    "CapsLock", "LeftShift", "RightShift", "LeftCtrl", "RightCtrl",
    "LeftAlt", "RightAlt", "LeftSuper", "RightSuper", "Menu",

    "Insert", "Delete", "Home", "End", "PageUp", "PageDown",
    "Left", "Right", "Up", "Down",
    "PrintScreen", "ScrollLock", "Pause", "NumLock",

    "Keypad0", "Keypad1", "Keypad2", "Keypad3", "Keypad4",
    "Keypad5", "Keypad6", "Keypad7", "Keypad8", "Keypad9",
    "KeypadDecimal", "KeypadDivide", "KeypadMultiply", "KeypadSubtract", "KeypadAdd", "KeypadEnter",
};

static_assert(std::size(s_KeyNames) == static_cast<size_t>(Key::Count), "s_KeyNames is out of sync with Key");

const char *KeyName(Key key)
{
    auto index = static_cast<size_t>(key);
    return index < std::size(s_KeyNames) ? s_KeyNames[index] : "Unknown";
}

//------------------------------------------------------------------------------------------------
// Scan code set 1 without the E0 prefix. Linux evdev codes 1-88 use the same values.
static constexpr std::array<Key, 0x59> s_BaseScanCodes =
{
    Key::Unknown,       Key::Escape,        Key::Num1,          Key::Num2,          // 0x00
    Key::Num3,          Key::Num4,          Key::Num5,          Key::Num6,
    Key::Num7,          Key::Num8,          Key::Num9,          Key::Num0,          // 0x08
    Key::Minus,         Key::Equal,         Key::Backspace,     Key::Tab,
    Key::Q,             Key::W,             Key::E,             Key::R,             // 0x10
    Key::T,             Key::Y,             Key::U,             Key::I,
    Key::O,             Key::P,             Key::LeftBracket,   Key::RightBracket,  // 0x18
    Key::Enter,         Key::LeftCtrl,      Key::A,             Key::S,
    Key::D,             Key::F,             Key::G,             Key::H,             // 0x20
    Key::J,             Key::K,             Key::L,             Key::Semicolon,
    Key::Apostrophe,    Key::Grave,         Key::LeftShift,     Key::Backslash,     // 0x28
    Key::Z,             Key::X,             Key::C,             Key::V,
    Key::B,             Key::N,             Key::M,             Key::Comma,         // 0x30
    Key::Period,        Key::Slash,         Key::RightShift,    Key::KeypadMultiply,
    Key::LeftAlt,       Key::Space,         Key::CapsLock,      Key::F1,            // 0x38
    Key::F2,            Key::F3,            Key::F4,            Key::F5,
    Key::F6,            Key::F7,            Key::F8,            Key::F9,            // 0x40
    Key::F10,           Key::NumLock,       Key::ScrollLock,    Key::Keypad7,
    Key::Keypad8,       Key::Keypad9,       Key::KeypadSubtract,Key::Keypad4,       // 0x48
    Key::Keypad5,       Key::Keypad6,       Key::KeypadAdd,     Key::Keypad1,
    Key::Keypad2,       Key::Keypad3,       Key::Keypad0,       Key::KeypadDecimal, // 0x50
    Key::Unknown,       Key::Unknown,       Key::Unknown,       Key::F11,
    Key::F12,                                                                       // 0x58
};

//------------------------------------------------------------------------------------------------
Key KeyFromScanCode(uint32_t scanCode, bool extended)
{
    if (!extended)
        return scanCode < s_BaseScanCodes.size() ? s_BaseScanCodes[scanCode] : Key::Unknown;

    switch (scanCode)
    {
    case 0x1C: return Key::KeypadEnter;
    case 0x1D: return Key::RightCtrl;
    case 0x35: return Key::KeypadDivide;
    case 0x37: return Key::PrintScreen;
    case 0x38: return Key::RightAlt;
    case 0x47: return Key::Home;
    case 0x48: return Key::Up;
    case 0x49: return Key::PageUp;
    case 0x4B: return Key::Left;
    case 0x4D: return Key::Right;
    case 0x4F: return Key::End;
    case 0x50: return Key::Down;
    case 0x51: return Key::PageDown;
    case 0x52: return Key::Insert;
    case 0x53: return Key::Delete;
    case 0x5B: return Key::LeftSuper;
    case 0x5C: return Key::RightSuper;
    case 0x5D: return Key::Menu;
    default:   return Key::Unknown;
    }
}

//------------------------------------------------------------------------------------------------
Key KeyFromEvdev(uint32_t code)
{
    if (code < s_BaseScanCodes.size())
        return s_BaseScanCodes[code];

    switch (code)
    {
    case 96:  return Key::KeypadEnter;      // KEY_KPENTER
    case 97:  return Key::RightCtrl;        // KEY_RIGHTCTRL
    case 98:  return Key::KeypadDivide;     // KEY_KPSLASH
    case 99:  return Key::PrintScreen;      // KEY_SYSRQ
    case 100: return Key::RightAlt;         // KEY_RIGHTALT
    case 102: return Key::Home;             // KEY_HOME
    case 103: return Key::Up;               // KEY_UP
    case 104: return Key::PageUp;           // KEY_PAGEUP
    case 105: return Key::Left;             // KEY_LEFT
    case 106: return Key::Right;            // KEY_RIGHT
    case 107: return Key::End;              // KEY_END
    case 108: return Key::Down;             // KEY_DOWN
    case 109: return Key::PageDown;         // KEY_PAGEDOWN
    case 110: return Key::Insert;           // KEY_INSERT
    case 111: return Key::Delete;           // KEY_DELETE
    case 119: return Key::Pause;            // KEY_PAUSE
    case 125: return Key::LeftSuper;        // KEY_LEFTMETA
    case 126: return Key::RightSuper;       // KEY_RIGHTMETA
    case 127: return Key::Menu;             // KEY_COMPOSE
    default:  return Key::Unknown;
    }
}

} // namespace Casement
