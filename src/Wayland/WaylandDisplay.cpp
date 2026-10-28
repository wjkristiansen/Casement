//================================================================================================
// Casement - Wayland backend
//================================================================================================

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"

#if CASEMENT_HAS_LIBDECOR
#include <libdecor.h>
#endif

#include "../Backends.h"

#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <iterator>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Casement
{
namespace
{

class CWaylandWindow;

// Marks the wl_surfaces Casement creates; libdecor's decoration surfaces carry other user data.
const char *const s_SurfaceTag = "casement";

//------------------------------------------------------------------------------------------------
class CWaylandDisplay final : public CDisplay
{
public:
    CWaylandDisplay();
    ~CWaylandDisplay() override;

    Backend GetBackend() const override { return Backend::Wayland; }
    std::unique_ptr<CWindow> OpenWindow(const WindowDesc &desc, IWindowEvents *pEvents) override;
    void PumpEvents() override { Dispatch(0); }
    void WaitEvents() override { Dispatch(-1); }

private:
    friend class CWaylandWindow;

    void Destroy();
    void Dispatch(int timeoutMs);
    void CheckError();
    void RemoveWindow(CWaylandWindow *pWindow);
    void UpdateCursor();
    static CWaylandWindow *WindowFromSurface(wl_surface *pSurface);

    // wl_registry
    static void OnGlobal(void *pData, wl_registry *pRegistry, uint32_t name, const char *pInterface, uint32_t version);
    static void OnGlobalRemove(void *, wl_registry *, uint32_t) {}

#if CASEMENT_HAS_LIBDECOR
    static void OnDecorError(libdecor *, libdecor_error, const char *pMessage);
#endif

    // xdg_wm_base
    static void OnPing(void *, xdg_wm_base *pWmBase, uint32_t serial) { xdg_wm_base_pong(pWmBase, serial); }

    // wl_seat
    static void OnSeatCapabilities(void *pData, wl_seat *pSeat, uint32_t capabilities);
    static void OnSeatName(void *, wl_seat *, const char *) {}

    // wl_pointer
    static void OnPointerEnter(void *pData, wl_pointer *, uint32_t serial, wl_surface *pSurface, wl_fixed_t x, wl_fixed_t y);
    static void OnPointerLeave(void *pData, wl_pointer *, uint32_t serial, wl_surface *pSurface);
    static void OnPointerMotion(void *pData, wl_pointer *, uint32_t time, wl_fixed_t x, wl_fixed_t y);
    static void OnPointerButton(void *pData, wl_pointer *, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
    static void OnPointerAxis(void *pData, wl_pointer *, uint32_t time, uint32_t axis, wl_fixed_t value);
    static void OnPointerFrame(void *pData, wl_pointer *);
    static void OnPointerAxisSource(void *, wl_pointer *, uint32_t) {}
    static void OnPointerAxisStop(void *, wl_pointer *, uint32_t, uint32_t) {}
    static void OnPointerAxisDiscrete(void *pData, wl_pointer *, uint32_t axis, int32_t discrete);

    // zwp_relative_pointer_v1
    static void OnRelativeMotion(void *pData, zwp_relative_pointer_v1 *, uint32_t, uint32_t,
        wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dxUnaccel, wl_fixed_t dyUnaccel);

    // wl_keyboard
    static void OnKeymap(void *, wl_keyboard *, uint32_t, int32_t fd, uint32_t) { close(fd); }
    static void OnKeyboardEnter(void *pData, wl_keyboard *, uint32_t serial, wl_surface *pSurface, wl_array *keys);
    static void OnKeyboardLeave(void *pData, wl_keyboard *, uint32_t serial, wl_surface *pSurface);
    static void OnKey(void *pData, wl_keyboard *, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void OnModifiers(void *, wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void OnRepeatInfo(void *, wl_keyboard *, int32_t, int32_t) {}

    wl_display *m_pDisplay = nullptr;
    wl_registry *m_pRegistry = nullptr;
#if CASEMENT_HAS_LIBDECOR
    libdecor *m_pDecor = nullptr;       // Null when libdecor fails to start; windows go undecorated
#endif
    wl_compositor *m_pCompositor = nullptr;
    wl_shm *m_pShm = nullptr;
    xdg_wm_base *m_pWmBase = nullptr;
    zxdg_decoration_manager_v1 *m_pDecorationManager = nullptr;
    zwp_relative_pointer_manager_v1 *m_pRelativePointerManager = nullptr;
    zwp_pointer_constraints_v1 *m_pPointerConstraints = nullptr;

    wl_seat *m_pSeat = nullptr;
    uint32_t m_SeatVersion = 0;
    wl_pointer *m_pPointer = nullptr;
    zwp_relative_pointer_v1 *m_pRelativePointer = nullptr;
    wl_keyboard *m_pKeyboard = nullptr;

    wl_cursor_theme *m_pCursorTheme = nullptr;
    wl_cursor_image *m_pCursorImage = nullptr;
    wl_surface *m_pCursorSurface = nullptr;

    CWaylandWindow *m_pPointerWindow = nullptr;
    CWaylandWindow *m_pKeyboardWindow = nullptr;
    uint32_t m_PointerSerial = 0;

    // Scroll accumulated over one wl_pointer.frame, indexed by WL_POINTER_AXIS_*.
    double m_AxisValue[2] = {};
    int32_t m_AxisDiscrete[2] = {};
};

//------------------------------------------------------------------------------------------------
class CWaylandWindow final : public CWindow
{
public:
    CWaylandWindow(CWaylandDisplay &display, const WindowDesc &desc, IWindowEvents *pEvents);
    ~CWaylandWindow() override;

    void Show() override;
    void SetTitle(const std::string &title) override;
    void SetIcon(const IconImage *, size_t) override {} // Desktops take icons from the app's .desktop file
    void SetClientSize(int width, int height) override;
    void GetClientSize(int &width, int &height) const override { width = m_Width; height = m_Height; }
    void SetFullscreen(bool fullscreen) override;
    bool IsFullscreen() const override { return m_Fullscreen; }
    bool HasFocus() const override { return m_Focused; }
    bool SetMouseCaptured(bool captured) override;
    bool IsMouseCaptured() const override { return m_Captured; }
    NativeWindow GetNativeWindow() const override;

private:
    friend class CWaylandDisplay;

    void CreateToplevel(const WindowDesc &desc);
    void Destroy();
    void SetSizeLimits(int width, int height);
    void ApplyConfigure(int width, int height, bool fullscreen, bool floating, int &appliedWidth, int &appliedHeight);
    bool Resize(int width, int height);
    bool DrawBackground();
    void SetFocused(bool focused);

#if CASEMENT_HAS_LIBDECOR
    void CreateFrame(const WindowDesc &desc);
    void CommitFrame(int width, int height, libdecor_configuration *pConfiguration);

    static void OnFrameConfigure(libdecor_frame *, libdecor_configuration *pConfiguration, void *pData);
    static void OnFrameClose(libdecor_frame *, void *pData);
    static void OnFrameCommit(libdecor_frame *, void *pData) { wl_surface_commit(static_cast<CWaylandWindow *>(pData)->m_pSurface); }
    static void OnFrameDismissPopup(libdecor_frame *, const char *, void *) {}
#endif

    // xdg_surface / xdg_toplevel
    static void OnSurfaceConfigure(void *pData, xdg_surface *pSurface, uint32_t serial);
    static void OnToplevelConfigure(void *pData, xdg_toplevel *, int32_t width, int32_t height, wl_array *states);
    static void OnToplevelClose(void *pData, xdg_toplevel *);

    // zxdg_toplevel_decoration_v1
    static void OnDecorationConfigure(void *, zxdg_toplevel_decoration_v1 *, uint32_t) {}

    // zwp_locked_pointer_v1
    static void OnLocked(void *pData, zwp_locked_pointer_v1 *) { static_cast<CWaylandWindow *>(pData)->m_Locked = true; }
    static void OnUnlocked(void *pData, zwp_locked_pointer_v1 *) { static_cast<CWaylandWindow *>(pData)->m_Locked = false; }

    // wl_buffer
    static void OnBufferRelease(void *, wl_buffer *pBuffer) { wl_buffer_destroy(pBuffer); }

    CWaylandDisplay &m_Display;
    wl_surface *m_pSurface = nullptr;
    xdg_surface *m_pXdgSurface = nullptr;
    xdg_toplevel *m_pToplevel = nullptr;
    zxdg_toplevel_decoration_v1 *m_pDecoration = nullptr;
    zwp_locked_pointer_v1 *m_pLockedPointer = nullptr;
#if CASEMENT_HAS_LIBDECOR
    libdecor_frame *m_pFrame = nullptr; // Replaces m_pXdgSurface and m_pToplevel when set
#endif

    std::optional<uint32_t> m_BackgroundColor;
    int m_Width;
    int m_Height;
    int m_WindowedWidth;    // Restored when the compositor leaves the size to us
    int m_WindowedHeight;
    bool m_Resizable;

    // Most recent xdg_toplevel.configure, applied on xdg_surface.configure.
    int m_PendingWidth = 0;
    int m_PendingHeight = 0;
    bool m_PendingFullscreen = false;
    bool m_PendingFloating = true;     // Not maximized, fullscreen or tiled

    bool m_Configured = false;
    bool m_Shown = false;
    bool m_Fullscreen = false;
    bool m_Focused = false;
    bool m_Captured = false;
    bool m_Locked = false;
    bool m_Dispatching = false; // Events are held back during construction
};

//------------------------------------------------------------------------------------------------
CWaylandDisplay::CWaylandDisplay()
{
    m_pDisplay = wl_display_connect(nullptr);
    if (m_pDisplay == nullptr)
        throw Error("Casement: cannot connect to the Wayland compositor");

    static const wl_registry_listener s_RegistryListener =
    {
        .global = OnGlobal,
        .global_remove = OnGlobalRemove,
    };
    m_pRegistry = wl_display_get_registry(m_pDisplay);
    wl_registry_add_listener(m_pRegistry, &s_RegistryListener, this);

    // The first round trip announces globals; the second delivers the seat's capabilities.
    wl_display_roundtrip(m_pDisplay);
    wl_display_roundtrip(m_pDisplay);

    if (!m_pCompositor || !m_pShm || !m_pWmBase)
    {
        const char *pMissing = !m_pCompositor ? "wl_compositor" : !m_pShm ? "wl_shm" : "xdg_wm_base";
        Destroy();
        throw Error(std::string("Casement: the Wayland compositor does not offer ") + pMissing);
    }

    m_pCursorTheme = wl_cursor_theme_load(nullptr, 24, m_pShm);
    if (m_pCursorTheme)
    {
        wl_cursor *pCursor = wl_cursor_theme_get_cursor(m_pCursorTheme, "default");
        if (!pCursor)
            pCursor = wl_cursor_theme_get_cursor(m_pCursorTheme, "left_ptr");
        if (pCursor && pCursor->image_count > 0)
        {
            m_pCursorImage = pCursor->images[0];
            m_pCursorSurface = wl_compositor_create_surface(m_pCompositor);
            wl_surface_attach(m_pCursorSurface, wl_cursor_image_get_buffer(m_pCursorImage), 0, 0);
            wl_surface_damage(m_pCursorSurface, 0, 0, static_cast<int32_t>(m_pCursorImage->width), static_cast<int32_t>(m_pCursorImage->height));
            wl_surface_commit(m_pCursorSurface);
        }
    }

#if CASEMENT_HAS_LIBDECOR
    // libdecor uses server-side decorations where the compositor offers them and draws its own
    // frame where it doesn't (GNOME, Weston, WSLg).
    // Zero-initialized so libdecor's reserved slots stay null.
    static libdecor_interface s_DecorInterface = []
    {
        libdecor_interface iface = {};
        iface.error = OnDecorError;
        return iface;
    }();
    m_pDecor = libdecor_new(m_pDisplay, &s_DecorInterface);
#endif
}

#if CASEMENT_HAS_LIBDECOR
//------------------------------------------------------------------------------------------------
void CWaylandDisplay::OnDecorError(libdecor *, libdecor_error, const char *pMessage)
{
    // Callbacks can't throw through libdecor; report and carry on.
    std::fprintf(stderr, "Casement: libdecor: %s\n", pMessage);
}
#endif

//------------------------------------------------------------------------------------------------
CWaylandDisplay::~CWaylandDisplay()
{
    Destroy();
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::Destroy()
{
#if CASEMENT_HAS_LIBDECOR
    if (m_pDecor) libdecor_unref(m_pDecor);
    m_pDecor = nullptr;
#endif
    if (m_pCursorSurface) wl_surface_destroy(m_pCursorSurface);
    if (m_pCursorTheme) wl_cursor_theme_destroy(m_pCursorTheme);
    if (m_pRelativePointer) zwp_relative_pointer_v1_destroy(m_pRelativePointer);
    if (m_pPointer) wl_pointer_destroy(m_pPointer);
    if (m_pKeyboard) wl_keyboard_destroy(m_pKeyboard);
    if (m_pSeat) wl_seat_destroy(m_pSeat);
    if (m_pPointerConstraints) zwp_pointer_constraints_v1_destroy(m_pPointerConstraints);
    if (m_pRelativePointerManager) zwp_relative_pointer_manager_v1_destroy(m_pRelativePointerManager);
    if (m_pDecorationManager) zxdg_decoration_manager_v1_destroy(m_pDecorationManager);
    if (m_pWmBase) xdg_wm_base_destroy(m_pWmBase);
    if (m_pShm) wl_shm_destroy(m_pShm);
    if (m_pCompositor) wl_compositor_destroy(m_pCompositor);
    if (m_pRegistry) wl_registry_destroy(m_pRegistry);
    if (m_pDisplay) wl_display_disconnect(m_pDisplay);

    m_pCursorSurface = nullptr; m_pCursorTheme = nullptr; m_pRelativePointer = nullptr;
    m_pPointer = nullptr; m_pKeyboard = nullptr; m_pSeat = nullptr; m_pPointerConstraints = nullptr;
    m_pRelativePointerManager = nullptr; m_pDecorationManager = nullptr; m_pWmBase = nullptr;
    m_pShm = nullptr; m_pCompositor = nullptr; m_pRegistry = nullptr; m_pDisplay = nullptr;
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::OnGlobal(void *pData, wl_registry *pRegistry, uint32_t name, const char *pInterface, uint32_t version)
{
    auto pThis = static_cast<CWaylandDisplay *>(pData);
    auto bind = [&](const wl_interface *pType, uint32_t maxVersion)
    {
        return wl_registry_bind(pRegistry, name, pType, std::min(version, maxVersion));
    };

    if (std::strcmp(pInterface, wl_compositor_interface.name) == 0)
    {
        pThis->m_pCompositor = static_cast<wl_compositor *>(bind(&wl_compositor_interface, 4));
    }
    else if (std::strcmp(pInterface, wl_shm_interface.name) == 0)
    {
        pThis->m_pShm = static_cast<wl_shm *>(bind(&wl_shm_interface, 1));
    }
    else if (std::strcmp(pInterface, xdg_wm_base_interface.name) == 0)
    {
        static const xdg_wm_base_listener s_Listener = { .ping = OnPing };
        pThis->m_pWmBase = static_cast<xdg_wm_base *>(bind(&xdg_wm_base_interface, 2));
        xdg_wm_base_add_listener(pThis->m_pWmBase, &s_Listener, pThis);
    }
    else if (std::strcmp(pInterface, wl_seat_interface.name) == 0 && pThis->m_pSeat == nullptr)
    {
        static const wl_seat_listener s_Listener =
        {
            .capabilities = OnSeatCapabilities,
            .name = OnSeatName,
        };
        pThis->m_SeatVersion = std::min(version, 5u);
        pThis->m_pSeat = static_cast<wl_seat *>(bind(&wl_seat_interface, 5));
        wl_seat_add_listener(pThis->m_pSeat, &s_Listener, pThis);
    }
    else if (std::strcmp(pInterface, zxdg_decoration_manager_v1_interface.name) == 0)
    {
        pThis->m_pDecorationManager = static_cast<zxdg_decoration_manager_v1 *>(bind(&zxdg_decoration_manager_v1_interface, 1));
    }
    else if (std::strcmp(pInterface, zwp_relative_pointer_manager_v1_interface.name) == 0)
    {
        pThis->m_pRelativePointerManager = static_cast<zwp_relative_pointer_manager_v1 *>(bind(&zwp_relative_pointer_manager_v1_interface, 1));
    }
    else if (std::strcmp(pInterface, zwp_pointer_constraints_v1_interface.name) == 0)
    {
        pThis->m_pPointerConstraints = static_cast<zwp_pointer_constraints_v1 *>(bind(&zwp_pointer_constraints_v1_interface, 1));
    }
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::OnSeatCapabilities(void *pData, wl_seat *pSeat, uint32_t capabilities)
{
    auto pThis = static_cast<CWaylandDisplay *>(pData);

    bool hasPointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (hasPointer && !pThis->m_pPointer)
    {
        static const wl_pointer_listener s_Listener =
        {
            .enter = OnPointerEnter,
            .leave = OnPointerLeave,
            .motion = OnPointerMotion,
            .button = OnPointerButton,
            .axis = OnPointerAxis,
            .frame = OnPointerFrame,
            .axis_source = OnPointerAxisSource,
            .axis_stop = OnPointerAxisStop,
            .axis_discrete = OnPointerAxisDiscrete,
            .axis_value120 = nullptr,           // wl_seat version 8, not bound
            .axis_relative_direction = nullptr, // wl_seat version 9, not bound
        };
        pThis->m_pPointer = wl_seat_get_pointer(pSeat);
        wl_pointer_add_listener(pThis->m_pPointer, &s_Listener, pThis);

        if (pThis->m_pRelativePointerManager)
        {
            static const zwp_relative_pointer_v1_listener s_RelativeListener = { .relative_motion = OnRelativeMotion };
            pThis->m_pRelativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(pThis->m_pRelativePointerManager, pThis->m_pPointer);
            zwp_relative_pointer_v1_add_listener(pThis->m_pRelativePointer, &s_RelativeListener, pThis);
        }
    }
    else if (!hasPointer && pThis->m_pPointer)
    {
        if (pThis->m_pRelativePointer)
            zwp_relative_pointer_v1_destroy(pThis->m_pRelativePointer);
        wl_pointer_destroy(pThis->m_pPointer);
        pThis->m_pRelativePointer = nullptr;
        pThis->m_pPointer = nullptr;
        pThis->m_pPointerWindow = nullptr;
    }

    bool hasKeyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (hasKeyboard && !pThis->m_pKeyboard)
    {
        static const wl_keyboard_listener s_Listener =
        {
            .keymap = OnKeymap,
            .enter = OnKeyboardEnter,
            .leave = OnKeyboardLeave,
            .key = OnKey,
            .modifiers = OnModifiers,
            .repeat_info = OnRepeatInfo,
        };
        pThis->m_pKeyboard = wl_seat_get_keyboard(pSeat);
        wl_keyboard_add_listener(pThis->m_pKeyboard, &s_Listener, pThis);
    }
    else if (!hasKeyboard && pThis->m_pKeyboard)
    {
        wl_keyboard_destroy(pThis->m_pKeyboard);
        pThis->m_pKeyboard = nullptr;
        if (pThis->m_pKeyboardWindow)
            pThis->m_pKeyboardWindow->SetFocused(false);
        pThis->m_pKeyboardWindow = nullptr;
    }
}

//------------------------------------------------------------------------------------------------
CWaylandWindow *CWaylandDisplay::WindowFromSurface(wl_surface *pSurface)
{
    // Null when the surface was destroyed after the event was sent, or belongs to libdecor.
    auto pProxy = reinterpret_cast<wl_proxy *>(pSurface);
    if (!pProxy || wl_proxy_get_tag(pProxy) != &s_SurfaceTag)
        return nullptr;
    return static_cast<CWaylandWindow *>(wl_proxy_get_user_data(pProxy));
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::UpdateCursor()
{
    if (!m_pPointer || !m_pPointerWindow)
        return;

    if (m_pPointerWindow->m_Captured || !m_pCursorImage)
        wl_pointer_set_cursor(m_pPointer, m_PointerSerial, nullptr, 0, 0);
    else
        wl_pointer_set_cursor(m_pPointer, m_PointerSerial, m_pCursorSurface,
            static_cast<int32_t>(m_pCursorImage->hotspot_x), static_cast<int32_t>(m_pCursorImage->hotspot_y));
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::OnPointerEnter(void *pData, wl_pointer *, uint32_t serial, wl_surface *pSurface, wl_fixed_t x, wl_fixed_t y)
{
    auto pThis = static_cast<CWaylandDisplay *>(pData);
    pThis->m_pPointerWindow = WindowFromSurface(pSurface);
    pThis->m_PointerSerial = serial;
    pThis->UpdateCursor();
    OnPointerMotion(pData, nullptr, 0, x, y);
}

void CWaylandDisplay::OnPointerLeave(void *pData, wl_pointer *, uint32_t, wl_surface *)
{
    static_cast<CWaylandDisplay *>(pData)->m_pPointerWindow = nullptr;
}

void CWaylandDisplay::OnPointerMotion(void *pData, wl_pointer *, uint32_t, wl_fixed_t x, wl_fixed_t y)
{
    CWaylandWindow *pWindow = static_cast<CWaylandDisplay *>(pData)->m_pPointerWindow;
    if (pWindow && !pWindow->m_Captured)
        pWindow->Events().OnMouseMove(*pWindow, wl_fixed_to_double(x), wl_fixed_to_double(y));
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::OnPointerButton(void *pData, wl_pointer *, uint32_t, uint32_t, uint32_t button, uint32_t state)
{
    CWaylandWindow *pWindow = static_cast<CWaylandDisplay *>(pData)->m_pPointerWindow;
    if (!pWindow)
        return;

    MouseButton mouseButton;
    switch (button)
    {
    case BTN_LEFT:   mouseButton = MouseButton::Left; break;
    case BTN_RIGHT:  mouseButton = MouseButton::Right; break;
    case BTN_MIDDLE: mouseButton = MouseButton::Middle; break;
    case BTN_SIDE:   mouseButton = MouseButton::X1; break;
    case BTN_EXTRA:  mouseButton = MouseButton::X2; break;
    default:         return;
    }
    pWindow->Events().OnMouseButton(*pWindow, mouseButton, state == WL_POINTER_BUTTON_STATE_PRESSED);
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::OnPointerAxis(void *pData, wl_pointer *, uint32_t, uint32_t axis, wl_fixed_t value)
{
    auto pThis = static_cast<CWaylandDisplay *>(pData);
    if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        return;
    pThis->m_AxisValue[axis] += wl_fixed_to_double(value);

    // Before wl_seat version 5 there are no frame events to group axis events.
    if (pThis->m_SeatVersion < 5)
        OnPointerFrame(pData, nullptr);
}

void CWaylandDisplay::OnPointerAxisDiscrete(void *pData, wl_pointer *, uint32_t axis, int32_t discrete)
{
    if (axis <= WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        static_cast<CWaylandDisplay *>(pData)->m_AxisDiscrete[axis] += discrete;
}

void CWaylandDisplay::OnPointerFrame(void *pData, wl_pointer *)
{
    auto pThis = static_cast<CWaylandDisplay *>(pData);

    // Prefer wheel notches; otherwise (touchpads) treat 10 surface units as one notch.
    auto notches = [pThis](int axis)
    {
        return pThis->m_AxisDiscrete[axis] != 0 ? double(pThis->m_AxisDiscrete[axis]) : pThis->m_AxisValue[axis] / 10.0;
    };
    double dx = notches(WL_POINTER_AXIS_HORIZONTAL_SCROLL);
    double dy = -notches(WL_POINTER_AXIS_VERTICAL_SCROLL); // Wayland scrolls down for positive values

    std::fill(std::begin(pThis->m_AxisValue), std::end(pThis->m_AxisValue), 0.0);
    std::fill(std::begin(pThis->m_AxisDiscrete), std::end(pThis->m_AxisDiscrete), 0);

    CWaylandWindow *pWindow = pThis->m_pPointerWindow;
    if (pWindow && (dx != 0.0 || dy != 0.0))
        pWindow->Events().OnMouseWheel(*pWindow, dx, dy);
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::OnRelativeMotion(void *pData, zwp_relative_pointer_v1 *, uint32_t, uint32_t,
    wl_fixed_t, wl_fixed_t, wl_fixed_t dxUnaccel, wl_fixed_t dyUnaccel)
{
    CWaylandWindow *pWindow = static_cast<CWaylandDisplay *>(pData)->m_pPointerWindow;
    if (pWindow && pWindow->m_Captured && pWindow->m_Locked)
        pWindow->Events().OnMouseDelta(*pWindow, wl_fixed_to_double(dxUnaccel), wl_fixed_to_double(dyUnaccel));
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::OnKeyboardEnter(void *pData, wl_keyboard *, uint32_t, wl_surface *pSurface, wl_array *)
{
    auto pThis = static_cast<CWaylandDisplay *>(pData);
    pThis->m_pKeyboardWindow = WindowFromSurface(pSurface);
    if (pThis->m_pKeyboardWindow)
        pThis->m_pKeyboardWindow->SetFocused(true);
}

void CWaylandDisplay::OnKeyboardLeave(void *pData, wl_keyboard *, uint32_t, wl_surface *)
{
    auto pThis = static_cast<CWaylandDisplay *>(pData);
    if (pThis->m_pKeyboardWindow)
        pThis->m_pKeyboardWindow->SetFocused(false);
    pThis->m_pKeyboardWindow = nullptr;
}

void CWaylandDisplay::OnKey(void *pData, wl_keyboard *, uint32_t, uint32_t, uint32_t key, uint32_t state)
{
    // TODO: Wayland leaves key repeat to clients (wl_keyboard.repeat_info); none is generated yet.
    CWaylandWindow *pWindow = static_cast<CWaylandDisplay *>(pData)->m_pKeyboardWindow;
    if (pWindow)
        pWindow->Events().OnKey(*pWindow, KeyFromEvdev(key), state == WL_KEYBOARD_KEY_STATE_PRESSED, false);
}

//------------------------------------------------------------------------------------------------
std::unique_ptr<CWindow> CWaylandDisplay::OpenWindow(const WindowDesc &desc, IWindowEvents *pEvents)
{
    return std::make_unique<CWaylandWindow>(*this, desc, pEvents);
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::RemoveWindow(CWaylandWindow *pWindow)
{
    if (m_pPointerWindow == pWindow)
        m_pPointerWindow = nullptr;
    if (m_pKeyboardWindow == pWindow)
        m_pKeyboardWindow = nullptr;
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::CheckError()
{
    if (int error = wl_display_get_error(m_pDisplay))
        throw Error("Casement: Wayland connection failed (" + std::string(std::strerror(error)) + ")");
}

//------------------------------------------------------------------------------------------------
void CWaylandDisplay::Dispatch(int timeoutMs)
{
#if CASEMENT_HAS_LIBDECOR
    // libdecor's plugin dispatches the display, and its own toolkit events with it.
    if (m_pDecor)
    {
        libdecor_dispatch(m_pDecor, timeoutMs);
        wl_display_flush(m_pDisplay);
        CheckError();
        return;
    }
#endif

    // The documented wl_display_prepare_read pattern, with a timeout so PumpEvents never blocks.
    while (wl_display_prepare_read(m_pDisplay) != 0)
        wl_display_dispatch_pending(m_pDisplay);

    // EAGAIN means the socket is full; the remainder goes out on the next call.
    if (wl_display_flush(m_pDisplay) < 0 && errno != EAGAIN)
    {
        wl_display_cancel_read(m_pDisplay);
        CheckError();
        return;
    }

    pollfd fd = { wl_display_get_fd(m_pDisplay), POLLIN, 0 };
    if (poll(&fd, 1, timeoutMs) > 0)
        wl_display_read_events(m_pDisplay); // Also reports a hung-up socket as a display error
    else
        wl_display_cancel_read(m_pDisplay);

    wl_display_dispatch_pending(m_pDisplay);
    wl_display_flush(m_pDisplay);
    CheckError();
}

//------------------------------------------------------------------------------------------------
CWaylandWindow::CWaylandWindow(CWaylandDisplay &display, const WindowDesc &desc, IWindowEvents *pEvents) :
    CWindow(pEvents),
    m_Display(display),
    m_BackgroundColor(desc.backgroundColor),
    m_Width(desc.width),
    m_Height(desc.height),
    m_WindowedWidth(desc.width),
    m_WindowedHeight(desc.height),
    m_Resizable(desc.resizable)
{
    m_pSurface = wl_compositor_create_surface(display.m_pCompositor);
    wl_surface_set_user_data(m_pSurface, this);
    wl_proxy_set_tag(reinterpret_cast<wl_proxy *>(m_pSurface), &s_SurfaceTag);

#if CASEMENT_HAS_LIBDECOR
    if (display.m_pDecor)
        CreateFrame(desc);
    else
#endif
        CreateToplevel(desc);

    // A surface takes no buffers until its first configure is acknowledged, so wait for it here:
    // a swapchain may present as soon as this returns.
    while (!m_Configured)
    {
        int result;
#if CASEMENT_HAS_LIBDECOR
        if (m_pFrame)
            result = libdecor_dispatch(display.m_pDecor, -1);
        else
#endif
            result = wl_display_dispatch(display.m_pDisplay);

        if (result < 0 || wl_display_get_error(display.m_pDisplay))
        {
            Destroy();
            display.CheckError();
            throw Error("Casement: Wayland connection failed while opening a window");
        }
    }

    m_Dispatching = true;
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::CreateToplevel(const WindowDesc &desc)
{
    static const xdg_surface_listener s_SurfaceListener = { .configure = OnSurfaceConfigure };
    m_pXdgSurface = xdg_wm_base_get_xdg_surface(m_Display.m_pWmBase, m_pSurface);
    xdg_surface_add_listener(m_pXdgSurface, &s_SurfaceListener, this);

    static const xdg_toplevel_listener s_ToplevelListener =
    {
        .configure = OnToplevelConfigure,
        .close = OnToplevelClose,
        .configure_bounds = nullptr,    // xdg_wm_base version 4, not bound
        .wm_capabilities = nullptr,     // xdg_wm_base version 5, not bound
    };
    m_pToplevel = xdg_surface_get_toplevel(m_pXdgSurface);
    xdg_toplevel_add_listener(m_pToplevel, &s_ToplevelListener, this);
    xdg_toplevel_set_app_id(m_pToplevel, "casement");
    SetTitle(desc.title);

    if (!m_Resizable)
        SetSizeLimits(m_Width, m_Height);
    if (desc.fullscreen)
        xdg_toplevel_set_fullscreen(m_pToplevel, nullptr);

    // Without libdecor, compositors lacking xdg-decoration (GNOME, WSLg) show no title bar.
    if (m_Display.m_pDecorationManager)
    {
        static const zxdg_toplevel_decoration_v1_listener s_DecorationListener = { .configure = OnDecorationConfigure };
        m_pDecoration = zxdg_decoration_manager_v1_get_toplevel_decoration(m_Display.m_pDecorationManager, m_pToplevel);
        zxdg_toplevel_decoration_v1_add_listener(m_pDecoration, &s_DecorationListener, this);
        zxdg_toplevel_decoration_v1_set_mode(m_pDecoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    wl_surface_commit(m_pSurface);
}

#if CASEMENT_HAS_LIBDECOR
//------------------------------------------------------------------------------------------------
void CWaylandWindow::CreateFrame(const WindowDesc &desc)
{
    static libdecor_frame_interface s_FrameInterface = []
    {
        libdecor_frame_interface iface = {};
        iface.configure = OnFrameConfigure;
        iface.close = OnFrameClose;
        iface.commit = OnFrameCommit;
        iface.dismiss_popup = OnFrameDismissPopup;
        return iface;
    }();
    m_pFrame = libdecor_decorate(m_Display.m_pDecor, m_pSurface, &s_FrameInterface, this);
    libdecor_frame_set_app_id(m_pFrame, "casement");
    SetTitle(desc.title);

    if (!m_Resizable)
    {
        SetSizeLimits(m_Width, m_Height);
        libdecor_frame_unset_capabilities(m_pFrame, LIBDECOR_ACTION_RESIZE);
    }
    if (desc.fullscreen)
        libdecor_frame_set_fullscreen(m_pFrame, nullptr);

    libdecor_frame_map(m_pFrame); // Makes the initial commit
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::CommitFrame(int width, int height, libdecor_configuration *pConfiguration)
{
    libdecor_state *pState = libdecor_state_new(width, height);
    libdecor_frame_commit(m_pFrame, pState, pConfiguration);
    libdecor_state_free(pState);
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::OnFrameConfigure(libdecor_frame *pFrame, libdecor_configuration *pConfiguration, void *pData)
{
    auto pThis = static_cast<CWaylandWindow *>(pData);

    int width = 0;
    int height = 0;
    libdecor_configuration_get_content_size(pConfiguration, pFrame, &width, &height);

    libdecor_window_state state = LIBDECOR_WINDOW_STATE_NONE;
    libdecor_configuration_get_window_state(pConfiguration, &state);
    bool fullscreen = (state & LIBDECOR_WINDOW_STATE_FULLSCREEN) != 0;
    bool floating = (state & (LIBDECOR_WINDOW_STATE_MAXIMIZED | LIBDECOR_WINDOW_STATE_FULLSCREEN |
        LIBDECOR_WINDOW_STATE_TILED_LEFT | LIBDECOR_WINDOW_STATE_TILED_RIGHT |
        LIBDECOR_WINDOW_STATE_TILED_TOP | LIBDECOR_WINDOW_STATE_TILED_BOTTOM)) == 0;

    // libdecor sizes its frame from the committed state, so commit before the surface changes.
    int appliedWidth;
    int appliedHeight;
    pThis->ApplyConfigure(width, height, fullscreen, floating, appliedWidth, appliedHeight);
    pThis->CommitFrame(appliedWidth, appliedHeight, pConfiguration);
    pThis->m_Configured = true;

    pThis->Resize(appliedWidth, appliedHeight);
    if (!pThis->DrawBackground())
        wl_surface_commit(pThis->m_pSurface);
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::OnFrameClose(libdecor_frame *, void *pData)
{
    auto pThis = static_cast<CWaylandWindow *>(pData);
    if (pThis->m_Dispatching)
        pThis->Events().OnCloseRequested(*pThis);
}
#endif

//------------------------------------------------------------------------------------------------
CWaylandWindow::~CWaylandWindow()
{
    Destroy();
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::Destroy()
{
    m_Dispatching = false;
    m_Display.RemoveWindow(this);
    if (m_pLockedPointer) zwp_locked_pointer_v1_destroy(m_pLockedPointer);
#if CASEMENT_HAS_LIBDECOR
    if (m_pFrame) libdecor_frame_unref(m_pFrame);
    m_pFrame = nullptr;
#endif
    if (m_pDecoration) zxdg_toplevel_decoration_v1_destroy(m_pDecoration);
    if (m_pToplevel) xdg_toplevel_destroy(m_pToplevel);
    if (m_pXdgSurface) xdg_surface_destroy(m_pXdgSurface);
    if (m_pSurface) wl_surface_destroy(m_pSurface);
    wl_display_flush(m_Display.m_pDisplay);

    m_pLockedPointer = nullptr; m_pDecoration = nullptr; m_pToplevel = nullptr;
    m_pXdgSurface = nullptr; m_pSurface = nullptr;
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::Show()
{
    m_Shown = true;
    DrawBackground();
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::SetTitle(const std::string &title)
{
#if CASEMENT_HAS_LIBDECOR
    if (m_pFrame)
    {
        libdecor_frame_set_title(m_pFrame, title.c_str());
        return;
    }
#endif
    xdg_toplevel_set_title(m_pToplevel, title.c_str());
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::SetSizeLimits(int width, int height)
{
#if CASEMENT_HAS_LIBDECOR
    if (m_pFrame)
    {
        libdecor_frame_set_min_content_size(m_pFrame, width, height);
        libdecor_frame_set_max_content_size(m_pFrame, width, height);
        return;
    }
#endif
    xdg_toplevel_set_min_size(m_pToplevel, width, height);
    xdg_toplevel_set_max_size(m_pToplevel, width, height);
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::SetClientSize(int width, int height)
{
    // Outside fullscreen and maximized states a Wayland client picks its own size; the next
    // buffer it presents makes the change.
    if (!m_Resizable)
        SetSizeLimits(width, height);
    m_WindowedWidth = width;
    m_WindowedHeight = height;
    if (m_Fullscreen || !Resize(width, height))
        return;

#if CASEMENT_HAS_LIBDECOR
    if (m_pFrame)
        CommitFrame(width, height, nullptr);
#endif
    DrawBackground();
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::SetFullscreen(bool fullscreen)
{
    // IsFullscreen changes when the compositor's configure arrives.
#if CASEMENT_HAS_LIBDECOR
    if (m_pFrame)
    {
        if (fullscreen)
            libdecor_frame_set_fullscreen(m_pFrame, nullptr);
        else
            libdecor_frame_unset_fullscreen(m_pFrame);
        return;
    }
#endif
    if (fullscreen)
        xdg_toplevel_set_fullscreen(m_pToplevel, nullptr);
    else
        xdg_toplevel_unset_fullscreen(m_pToplevel);
}

//------------------------------------------------------------------------------------------------
bool CWaylandWindow::SetMouseCaptured(bool captured)
{
    if (captured == m_Captured)
        return true;

    if (captured && (!m_Display.m_pPointerConstraints || !m_Display.m_pRelativePointer))
        return false; // The compositor lacks pointer-constraints or relative-pointer

    m_Captured = captured;
    if (captured)
    {
        static const zwp_locked_pointer_v1_listener s_Listener =
        {
            .locked = OnLocked,
            .unlocked = OnUnlocked,
        };
        // Persistent: the compositor re-locks whenever the window regains pointer focus.
        m_pLockedPointer = zwp_pointer_constraints_v1_lock_pointer(m_Display.m_pPointerConstraints,
            m_pSurface, m_Display.m_pPointer, nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        zwp_locked_pointer_v1_add_listener(m_pLockedPointer, &s_Listener, this);
    }
    else
    {
        zwp_locked_pointer_v1_destroy(m_pLockedPointer);
        m_pLockedPointer = nullptr;
        m_Locked = false;
    }

    if (m_Display.m_pPointerWindow == this)
        m_Display.UpdateCursor();
    wl_display_flush(m_Display.m_pDisplay);
    return true;
}

//------------------------------------------------------------------------------------------------
NativeWindow CWaylandWindow::GetNativeWindow() const
{
    NativeWindow native;
    native.backend = Backend::Wayland;
    native.wayland.display = m_Display.m_pDisplay;
    native.wayland.surface = m_pSurface;
    return native;
}

//------------------------------------------------------------------------------------------------
bool CWaylandWindow::Resize(int width, int height)
{
    if (width == m_Width && height == m_Height)
        return false;
    m_Width = width;
    m_Height = height;
    if (m_Dispatching)
        Events().OnResized(*this, width, height);
    return true;
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::ApplyConfigure(int width, int height, bool fullscreen, bool floating, int &appliedWidth, int &appliedHeight)
{
    m_Fullscreen = fullscreen;

    // A zero size leaves the choice to the client: keep the windowed size.
    if (width <= 0 || height <= 0)
    {
        width = m_WindowedWidth;
        height = m_WindowedHeight;
    }
    else if (floating)
    {
        // Only a floating size is worth restoring; maximized and tiled sizes come back from the compositor.
        m_WindowedWidth = width;
        m_WindowedHeight = height;
    }
    appliedWidth = width;
    appliedHeight = height;
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::SetFocused(bool focused)
{
    if (focused == m_Focused)
        return;
    m_Focused = focused;
    if (m_Dispatching)
        Events().OnFocusChanged(*this, focused);
}

//------------------------------------------------------------------------------------------------
bool CWaylandWindow::DrawBackground()
{
    if (!m_Shown || !m_BackgroundColor || m_Width <= 0 || m_Height <= 0)
        return false;

    int stride = m_Width * 4;
    size_t size = static_cast<size_t>(stride) * static_cast<size_t>(m_Height);

    int fd = memfd_create("casement-background", MFD_CLOEXEC);
    if (fd < 0)
        return false;
    if (ftruncate(fd, static_cast<off_t>(size)) != 0)
    {
        close(fd);
        return false;
    }

    void *pPixels = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pPixels == MAP_FAILED)
    {
        close(fd);
        return false;
    }
    std::fill_n(static_cast<uint32_t *>(pPixels), size / 4, 0xFF000000u | *m_BackgroundColor);
    munmap(pPixels, size);

    wl_shm_pool *pPool = wl_shm_create_pool(m_Display.m_pShm, fd, static_cast<int32_t>(size));
    wl_buffer *pBuffer = wl_shm_pool_create_buffer(pPool, 0, m_Width, m_Height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pPool);
    close(fd);

    static const wl_buffer_listener s_Listener = { .release = OnBufferRelease };
    wl_buffer_add_listener(pBuffer, &s_Listener, nullptr);

    wl_surface_attach(m_pSurface, pBuffer, 0, 0);
    wl_surface_damage(m_pSurface, 0, 0, m_Width, m_Height);
    wl_surface_commit(m_pSurface);
    return true;
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::OnToplevelConfigure(void *pData, xdg_toplevel *, int32_t width, int32_t height, wl_array *pStates)
{
    auto pThis = static_cast<CWaylandWindow *>(pData);
    pThis->m_PendingWidth = width;
    pThis->m_PendingHeight = height;
    pThis->m_PendingFullscreen = false;
    pThis->m_PendingFloating = true;

    auto pState = static_cast<const uint32_t *>(pStates->data);
    for (size_t i = 0; i < pStates->size / sizeof(uint32_t); ++i)
    {
        switch (pState[i])
        {
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
            pThis->m_PendingFullscreen = true;
            pThis->m_PendingFloating = false;
            break;
        case XDG_TOPLEVEL_STATE_MAXIMIZED:
        case XDG_TOPLEVEL_STATE_TILED_LEFT:
        case XDG_TOPLEVEL_STATE_TILED_RIGHT:
        case XDG_TOPLEVEL_STATE_TILED_TOP:
        case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
            pThis->m_PendingFloating = false;
            break;
        default:
            break;
        }
    }
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::OnSurfaceConfigure(void *pData, xdg_surface *pSurface, uint32_t serial)
{
    auto pThis = static_cast<CWaylandWindow *>(pData);
    xdg_surface_ack_configure(pSurface, serial);
    pThis->m_Configured = true;

    int width;
    int height;
    pThis->ApplyConfigure(pThis->m_PendingWidth, pThis->m_PendingHeight, pThis->m_PendingFullscreen, pThis->m_PendingFloating, width, height);
    if (pThis->Resize(width, height))
        pThis->DrawBackground();
}

//------------------------------------------------------------------------------------------------
void CWaylandWindow::OnToplevelClose(void *pData, xdg_toplevel *)
{
    auto pThis = static_cast<CWaylandWindow *>(pData);
    if (pThis->m_Dispatching)
        pThis->Events().OnCloseRequested(*pThis);
}

} // namespace

//------------------------------------------------------------------------------------------------
std::unique_ptr<CDisplay> CreateWaylandDisplay()
{
    return std::make_unique<CWaylandDisplay>();
}

} // namespace Casement
