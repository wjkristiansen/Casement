//================================================================================================
// Casement - cross-platform window management
//
// This header is platform-neutral: it never includes windows.h, xcb.h or wayland-client.h.
// Native handles are exposed as opaque pointers and integers in NativeWindow.
//================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace Casement
{

//------------------------------------------------------------------------------------------------
enum class Backend
{
    Auto,       // Win32 on Windows. On Linux, CASEMENT_BACKEND=wayland|x11 if set, else Wayland,
                // then X11; under WSLg, X11 first
    Win32,
    Wayland,
    X11,
};

const char *BackendName(Backend backend);

//------------------------------------------------------------------------------------------------
// Thrown when a backend cannot be initialized or a native call fails.
class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

//------------------------------------------------------------------------------------------------
// Native handles, for creating a graphics surface (VkSurfaceKHR, IDXGISwapChain, EGLSurface).
// Only the member matching 'backend' is valid.
struct NativeWindow
{
    Backend backend = Backend::Auto;

    struct
    {
        void *hInstance = nullptr;  // HINSTANCE
        void *hWnd = nullptr;       // HWND
    } win32;

    struct
    {
        void *connection = nullptr; // xcb_connection_t *
        uint32_t window = 0;        // xcb_window_t
    } x11;

    struct
    {
        void *display = nullptr;    // wl_display *
        void *surface = nullptr;    // wl_surface *
    } wayland;
};

//------------------------------------------------------------------------------------------------
// Physical key positions, named for a US layout. Independent of the active keyboard layout.
enum class Key : uint16_t
{
    Unknown,

    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Escape, Enter, Tab, Backspace, Space,
    Minus, Equal, LeftBracket, RightBracket, Backslash,
    Semicolon, Apostrophe, Grave, Comma, Period, Slash,

    CapsLock, LeftShift, RightShift, LeftCtrl, RightCtrl,
    LeftAlt, RightAlt, LeftSuper, RightSuper, Menu,

    Insert, Delete, Home, End, PageUp, PageDown,
    Left, Right, Up, Down,
    PrintScreen, ScrollLock, Pause, NumLock,

    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
    Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal, KeypadDivide, KeypadMultiply, KeypadSubtract, KeypadAdd, KeypadEnter,

    Count
};

const char *KeyName(Key key);

enum class MouseButton : uint8_t
{
    Left,
    Right,
    Middle,
    X1,
    X2,
};

//------------------------------------------------------------------------------------------------
struct WindowDesc
{
    std::string title = "Casement";     // UTF-8
    int width = 1280;                   // Client area size, in pixels
    int height = 720;
    bool resizable = true;
    bool fullscreen = false;

    // Fill the client area with this 0xRRGGBB color whenever it needs repainting. Leave unset for
    // windows a swapchain presents to: on Wayland the library then supplies the surface's buffers,
    // which conflicts with a swapchain.
    std::optional<uint32_t> backgroundColor;
};

//------------------------------------------------------------------------------------------------
// One size of a window icon. Pixels are row-major 0xAARRGGBB, not premultiplied.
struct IconImage
{
    int width = 0;
    int height = 0;
    const uint32_t *pPixels = nullptr;
};

class CWindow;

//------------------------------------------------------------------------------------------------
// Event callbacks, called from CDisplay::PumpEvents or CDisplay::WaitEvents on the thread that
// owns the display. Override what you need. Callbacks must not throw: they run inside native
// dispatch code (a WndProc, libwayland).
class IWindowEvents
{
public:
    virtual ~IWindowEvents() = default;

    // The user asked to close the window. Nothing is destroyed: release the CWindow to close it.
    virtual void OnCloseRequested(CWindow &) {}
    virtual void OnResized(CWindow &, int /*width*/, int /*height*/) {}
    virtual void OnFocusChanged(CWindow &, bool /*focused*/) {}

    virtual void OnKey(CWindow &, Key, bool /*pressed*/, bool /*repeat*/) {}

    // Cursor position in client-area pixels. Not sent while the mouse is captured.
    virtual void OnMouseMove(CWindow &, double /*x*/, double /*y*/) {}

    // Relative motion, sent only while the mouse is captured. Unaccelerated on Win32 and Wayland;
    // X11 currently reports core-protocol motion, which includes pointer acceleration.
    virtual void OnMouseDelta(CWindow &, double /*dx*/, double /*dy*/) {}

    virtual void OnMouseButton(CWindow &, MouseButton, bool /*pressed*/) {}

    // In notches: positive dy scrolls up (away from the user), positive dx scrolls right.
    virtual void OnMouseWheel(CWindow &, double /*dx*/, double /*dy*/) {}
};

//------------------------------------------------------------------------------------------------
// A top-level window. Created with CDisplay::OpenWindow; destroyed when released.
class CWindow
{
public:
    virtual ~CWindow() = default;

    CWindow(const CWindow &) = delete;
    CWindow &operator=(const CWindow &) = delete;

    // Windows open hidden. On Wayland a window appears once its first frame is presented.
    virtual void Show() = 0;

    virtual void SetTitle(const std::string &title) = 0;

    // Supply several sizes and each platform picks the closest it needs; count 0 restores the
    // default. On Win32 the default is the executable's own icon. Wayland has no window icons:
    // desktops take them from the application's .desktop file, so this does nothing there.
    virtual void SetIcon(const IconImage *pImages, size_t count) = 0;

    virtual void SetClientSize(int width, int height) = 0;
    virtual void GetClientSize(int &width, int &height) const = 0;

    virtual void SetFullscreen(bool fullscreen) = 0;
    virtual bool IsFullscreen() const = 0;

    virtual bool HasFocus() const = 0;

    // Hide the cursor, keep it inside the window, and report motion through OnMouseDelta.
    // Returns false if the platform cannot capture the mouse. On Win32 this registers raw mouse
    // input only if nothing else in the process has; an application that registers its own
    // should do so before capturing.
    virtual bool SetMouseCaptured(bool captured) = 0;
    virtual bool IsMouseCaptured() const = 0;

    virtual NativeWindow GetNativeWindow() const = 0;

    void SetEvents(IWindowEvents *pEvents) { m_pEvents = pEvents; }

    // Win32 only; other backends never call it. Sees each message for this window before
    // Casement does, with hWnd as the HWND. Return true to consume the message, with result as
    // the window procedure's return value. Let WM_INPUT through: DefWindowProc releases its data.
    using Win32MessageHook = std::function<bool(void *hWnd, uint32_t message, uintptr_t wParam, intptr_t lParam, intptr_t &result)>;
    void SetWin32MessageHook(Win32MessageHook hook) { m_Win32MessageHook = std::move(hook); }

protected:
    explicit CWindow(IWindowEvents *pEvents) : m_pEvents(pEvents) {}

    // Every backend dispatches through here, so a null event sink is always safe.
    IWindowEvents &Events() { return m_pEvents ? *m_pEvents : s_NullEvents; }

    const Win32MessageHook &GetWin32MessageHook() const { return m_Win32MessageHook; }

private:
    IWindowEvents *m_pEvents;
    Win32MessageHook m_Win32MessageHook;
    static inline IWindowEvents s_NullEvents;
};

//------------------------------------------------------------------------------------------------
// The connection to the windowing system. Create one per process; it must outlive its windows.
// All calls must come from the thread that created it.
class CDisplay
{
public:
    // Throws Error if the requested backend is unavailable or not compiled in.
    static std::unique_ptr<CDisplay> Create(Backend backend = Backend::Auto);

    virtual ~CDisplay() = default;

    CDisplay(const CDisplay &) = delete;
    CDisplay &operator=(const CDisplay &) = delete;

    virtual Backend GetBackend() const = 0;

    // pEvents may be null, and must outlive the window or be replaced with CWindow::SetEvents.
    virtual std::unique_ptr<CWindow> OpenWindow(const WindowDesc &desc, IWindowEvents *pEvents) = 0;

    // Dispatch every pending event without blocking.
    virtual void PumpEvents() = 0;

    // Block until at least one event arrives, then dispatch every pending event.
    virtual void WaitEvents() = 0;

protected:
    CDisplay() = default;
};

} // namespace Casement
