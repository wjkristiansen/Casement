# Casement

Cross-platform window management for C++20. The public header, `include/Casement/Casement.h`,
includes no platform headers: native handles come back through `NativeWindow` as opaque
pointers, ready for `vkCreate*SurfaceKHR` or a DXGI swapchain.

| Backend | Platform | Native handle             |
|---------|----------|---------------------------|
| Win32   | Windows  | `HINSTANCE`, `HWND`       |
| Wayland | Linux    | `wl_display*`, `wl_surface*` |
| X11     | Linux    | `xcb_connection_t*`, `xcb_window_t` |

On Linux, `CDisplay::Create()` uses Wayland when `WAYLAND_DISPLAY` is set and falls back to X11.
Set `CASEMENT_BACKEND=wayland` or `CASEMENT_BACKEND=x11` to force one.

Under WSLg, X11 comes first. WSLg leaves a ghost of the shadow behind when a libdecor-decorated
Wayland window maximizes; ffplay (SDL2) shows it too. X11 windows there get their frames from
WSLg and don't.

## Use

```cpp
#include <Casement/Casement.h>

class CEvents : public Casement::IWindowEvents
{
public:
    bool m_Running = true;
    void OnCloseRequested(Casement::CWindow &) override { m_Running = false; }
};

int main()
{
    auto pDisplay = Casement::CDisplay::Create();

    Casement::WindowDesc desc;
    desc.title = "Hello";

    CEvents events;
    auto pWindow = pDisplay->OpenWindow(desc, &events);
    pWindow->Show();

    while (events.m_Running)
        pDisplay->PumpEvents();     // or WaitEvents() when nothing animates
}
```

`samples/Basic` logs every event: `C` toggles mouse capture, `F11` fullscreen, `Escape` quits.

## Build

```sh
cmake -S . -B build
cmake --build build
```

As a subproject, `add_subdirectory(Casement)` and link `Casement::Casement`.

Casement is a static library, and its interface uses standard library types (`std::string`,
`std::unique_ptr`, `std::optional`, `std::function`, and `Casement::Error` derives from
`std::runtime_error`). Don't pass Casement objects across a shared library boundary (a DLL or
`.so`) unless both sides are built with the same compiler, standard library, and ABI settings.

Turn a backend off with `-DCASEMENT_X11=OFF` or `-DCASEMENT_WAYLAND=OFF`.

Wayland compositors don't all draw title bars: GNOME and WSLg leave that to the application.
Casement uses [libdecor](https://gitlab.freedesktop.org/libdecor/libdecor) for those, and the
compositor's own decorations where it offers them. `-DCASEMENT_LIBDECOR=OFF` drops the
dependency, at the cost of frameless windows on such compositors.

## Gaps

- **Text input.** `OnKey` reports physical keys only. Text needs `WM_CHAR` on Win32 and
  xkbcommon on Linux.
- **Wayland key repeat.** Wayland leaves repeat to the client; Casement doesn't generate it yet.
- **X11 relative motion** is accelerated core-protocol motion. XInput2 raw events would match
  Win32 raw input.
- **High DPI.** Sizes are in pixels, with no per-monitor DPI awareness on Win32 and no buffer
  scale on Wayland.
- **Cursor shapes, multiple monitors, clipboard:** not yet.
