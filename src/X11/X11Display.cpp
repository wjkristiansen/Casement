//================================================================================================
// Casement - X11 backend (xcb)
//================================================================================================

#include <xcb/xcb.h>

#include "../Backends.h"

#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Casement
{
namespace
{

struct FreeDeleter
{
    void operator()(void *p) const { std::free(p); }
};

template <class T>
using XcbPtr = std::unique_ptr<T, FreeDeleter>;

class CX11Window;

//------------------------------------------------------------------------------------------------
class CX11Display final : public CDisplay
{
public:
    CX11Display();
    ~CX11Display() override;

    Backend GetBackend() const override { return Backend::X11; }
    std::unique_ptr<CWindow> OpenWindow(const WindowDesc &desc, IWindowEvents *pEvents) override;
    void PumpEvents() override;
    void WaitEvents() override;

    xcb_connection_t *Connection() const { return m_pConnection; }
    xcb_screen_t *Screen() const { return m_pScreen; }
    xcb_cursor_t InvisibleCursor() const { return m_InvisibleCursor; }

    void AddWindow(xcb_window_t id, CX11Window *pWindow) { m_Windows[id] = pWindow; }
    void RemoveWindow(xcb_window_t id) { m_Windows.erase(id); }

    xcb_atom_t m_WmProtocols = XCB_ATOM_NONE;
    xcb_atom_t m_WmDeleteWindow = XCB_ATOM_NONE;
    xcb_atom_t m_NetWmName = XCB_ATOM_NONE;
    xcb_atom_t m_NetWmIcon = XCB_ATOM_NONE;
    xcb_atom_t m_NetWmState = XCB_ATOM_NONE;
    xcb_atom_t m_NetWmStateFullscreen = XCB_ATOM_NONE;
    xcb_atom_t m_Utf8String = XCB_ATOM_NONE;

private:
    xcb_atom_t InternAtom(const char *pName);
    xcb_generic_event_t *NextEvent(bool wait);
    void Dispatch(xcb_generic_event_t *pEvent);
    void CheckConnection();

    xcb_connection_t *m_pConnection = nullptr;
    xcb_screen_t *m_pScreen = nullptr;
    xcb_cursor_t m_InvisibleCursor = XCB_NONE;
    xcb_generic_event_t *m_pPending = nullptr; // Read ahead while detecting key repeat
    std::unordered_map<xcb_window_t, CX11Window *> m_Windows;
};

//------------------------------------------------------------------------------------------------
class CX11Window final : public CWindow
{
public:
    CX11Window(CX11Display &display, const WindowDesc &desc, IWindowEvents *pEvents);
    ~CX11Window() override;

    void Show() override;
    void SetTitle(const std::string &title) override;
    void SetIcon(const IconImage *pImages, size_t count) override;
    void SetClientSize(int width, int height) override;
    void GetClientSize(int &width, int &height) const override { width = m_Width; height = m_Height; }
    void SetFullscreen(bool fullscreen) override;
    bool IsFullscreen() const override { return m_Fullscreen; }
    bool HasFocus() const override { return m_Focused; }
    bool SetMouseCaptured(bool captured) override;
    bool IsMouseCaptured() const override { return m_Captured; }
    NativeWindow GetNativeWindow() const override;

    void OnKeyEvent(xcb_keycode_t keycode, bool pressed, bool repeat);
    void OnButtonEvent(xcb_button_t button, bool pressed);
    void OnMotionEvent(int x, int y);
    void OnFocusEvent(bool focused);
    void OnConfigureEvent(int width, int height);
    void OnMapEvent(bool mapped);
    void OnClientMessage(const xcb_client_message_event_t &event);

private:
    xcb_connection_t *Connection() const { return m_Display.Connection(); }
    void SetSizeHints(int width, int height);
    void Grab();
    void Ungrab();
    void WarpToCenter();

    CX11Display &m_Display;
    xcb_window_t m_Window = XCB_NONE;
    int m_Width;
    int m_Height;
    bool m_Resizable;
    bool m_Fullscreen = false;
    bool m_Mapped = false;
    bool m_Focused = false;
    bool m_Captured = false;
    bool m_Grabbed = false;
};

//------------------------------------------------------------------------------------------------
CX11Display::CX11Display()
{
    int screenIndex = 0;
    m_pConnection = xcb_connect(nullptr, &screenIndex);
    if (xcb_connection_has_error(m_pConnection))
    {
        xcb_disconnect(m_pConnection);
        throw Error("Casement: cannot connect to the X server");
    }

    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(m_pConnection));
    for (int i = 0; i < screenIndex; ++i)
        xcb_screen_next(&it);
    m_pScreen = it.data;

    m_WmProtocols = InternAtom("WM_PROTOCOLS");
    m_WmDeleteWindow = InternAtom("WM_DELETE_WINDOW");
    m_NetWmName = InternAtom("_NET_WM_NAME");
    m_NetWmIcon = InternAtom("_NET_WM_ICON");
    m_NetWmState = InternAtom("_NET_WM_STATE");
    m_NetWmStateFullscreen = InternAtom("_NET_WM_STATE_FULLSCREEN");
    m_Utf8String = InternAtom("UTF8_STRING");

    // A cursor whose 1x1 source and mask are both clear.
    xcb_pixmap_t pixmap = xcb_generate_id(m_pConnection);
    xcb_create_pixmap(m_pConnection, 1, pixmap, m_pScreen->root, 1, 1);
    xcb_gcontext_t gc = xcb_generate_id(m_pConnection);
    uint32_t foreground = 0;
    xcb_create_gc(m_pConnection, gc, pixmap, XCB_GC_FOREGROUND, &foreground);
    xcb_rectangle_t rect = { 0, 0, 1, 1 };
    xcb_poly_fill_rectangle(m_pConnection, pixmap, gc, 1, &rect);
    m_InvisibleCursor = xcb_generate_id(m_pConnection);
    xcb_create_cursor(m_pConnection, m_InvisibleCursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 0, 0);
    xcb_free_gc(m_pConnection, gc);
    xcb_free_pixmap(m_pConnection, pixmap);
}

//------------------------------------------------------------------------------------------------
CX11Display::~CX11Display()
{
    std::free(m_pPending);
    xcb_free_cursor(m_pConnection, m_InvisibleCursor);
    xcb_disconnect(m_pConnection);
}

//------------------------------------------------------------------------------------------------
xcb_atom_t CX11Display::InternAtom(const char *pName)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(m_pConnection, 0, static_cast<uint16_t>(std::strlen(pName)), pName);
    XcbPtr<xcb_intern_atom_reply_t> reply(xcb_intern_atom_reply(m_pConnection, cookie, nullptr));
    return reply ? reply->atom : xcb_atom_t(XCB_ATOM_NONE);
}

//------------------------------------------------------------------------------------------------
std::unique_ptr<CWindow> CX11Display::OpenWindow(const WindowDesc &desc, IWindowEvents *pEvents)
{
    return std::make_unique<CX11Window>(*this, desc, pEvents);
}

//------------------------------------------------------------------------------------------------
void CX11Display::CheckConnection()
{
    if (xcb_connection_has_error(m_pConnection))
        throw Error("Casement: lost the connection to the X server");
}

//------------------------------------------------------------------------------------------------
xcb_generic_event_t *CX11Display::NextEvent(bool wait)
{
    if (m_pPending)
    {
        xcb_generic_event_t *pEvent = m_pPending;
        m_pPending = nullptr;
        return pEvent;
    }
    return wait ? xcb_wait_for_event(m_pConnection) : xcb_poll_for_event(m_pConnection);
}

//------------------------------------------------------------------------------------------------
void CX11Display::PumpEvents()
{
    xcb_flush(m_pConnection);
    while (xcb_generic_event_t *pEvent = NextEvent(false))
    {
        Dispatch(pEvent);
        std::free(pEvent);
    }
    xcb_flush(m_pConnection);
    CheckConnection();
}

//------------------------------------------------------------------------------------------------
void CX11Display::WaitEvents()
{
    xcb_flush(m_pConnection);
    if (xcb_generic_event_t *pEvent = NextEvent(true))
    {
        Dispatch(pEvent);
        std::free(pEvent);
    }
    PumpEvents();
}

//------------------------------------------------------------------------------------------------
void CX11Display::Dispatch(xcb_generic_event_t *pEvent)
{
    auto find = [this](xcb_window_t id) -> CX11Window *
    {
        auto it = m_Windows.find(id);
        return it == m_Windows.end() ? nullptr : it->second;
    };

    switch (pEvent->response_type & ~0x80)
    {
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
        {
            auto pKey = reinterpret_cast<xcb_key_press_event_t *>(pEvent);
            CX11Window *pWindow = find(pKey->event);
            if (!pWindow)
                break;

            bool pressed = (pEvent->response_type & ~0x80) == XCB_KEY_PRESS;
            if (!pressed)
            {
                // Auto-repeat arrives as a release and a press with the same timestamp.
                xcb_generic_event_t *pNext = xcb_poll_for_queued_event(m_pConnection);
                auto pNextKey = reinterpret_cast<xcb_key_press_event_t *>(pNext);
                if (pNext && (pNext->response_type & ~0x80) == XCB_KEY_PRESS &&
                    pNextKey->detail == pKey->detail && pNextKey->time == pKey->time && pNextKey->event == pKey->event)
                {
                    std::free(pNext);
                    pWindow->OnKeyEvent(pKey->detail, true, true);
                    break;
                }
                m_pPending = pNext;
            }
            pWindow->OnKeyEvent(pKey->detail, pressed, false);
        }
        break;

    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
        {
            auto pButton = reinterpret_cast<xcb_button_press_event_t *>(pEvent);
            if (CX11Window *pWindow = find(pButton->event))
                pWindow->OnButtonEvent(pButton->detail, (pEvent->response_type & ~0x80) == XCB_BUTTON_PRESS);
        }
        break;

    case XCB_MOTION_NOTIFY:
        {
            auto pMotion = reinterpret_cast<xcb_motion_notify_event_t *>(pEvent);
            if (CX11Window *pWindow = find(pMotion->event))
                pWindow->OnMotionEvent(pMotion->event_x, pMotion->event_y);
        }
        break;

    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
        {
            // Ignore the transient focus changes keyboard grabs cause, and pointer-only focus.
            auto pFocus = reinterpret_cast<xcb_focus_in_event_t *>(pEvent);
            if (pFocus->mode == XCB_NOTIFY_MODE_GRAB || pFocus->mode == XCB_NOTIFY_MODE_UNGRAB ||
                pFocus->detail == XCB_NOTIFY_DETAIL_POINTER)
                break;
            if (CX11Window *pWindow = find(pFocus->event))
                pWindow->OnFocusEvent((pEvent->response_type & ~0x80) == XCB_FOCUS_IN);
        }
        break;

    case XCB_CONFIGURE_NOTIFY:
        {
            auto pConfigure = reinterpret_cast<xcb_configure_notify_event_t *>(pEvent);
            if (CX11Window *pWindow = find(pConfigure->window))
                pWindow->OnConfigureEvent(pConfigure->width, pConfigure->height);
        }
        break;

    case XCB_MAP_NOTIFY:
        if (CX11Window *pWindow = find(reinterpret_cast<xcb_map_notify_event_t *>(pEvent)->window))
            pWindow->OnMapEvent(true);
        break;

    case XCB_UNMAP_NOTIFY:
        if (CX11Window *pWindow = find(reinterpret_cast<xcb_unmap_notify_event_t *>(pEvent)->window))
            pWindow->OnMapEvent(false);
        break;

    case XCB_CLIENT_MESSAGE:
        {
            auto pMessage = reinterpret_cast<xcb_client_message_event_t *>(pEvent);
            if (CX11Window *pWindow = find(pMessage->window))
                pWindow->OnClientMessage(*pMessage);
        }
        break;
    }
}

//------------------------------------------------------------------------------------------------
CX11Window::CX11Window(CX11Display &display, const WindowDesc &desc, IWindowEvents *pEvents) :
    CWindow(pEvents),
    m_Display(display),
    m_Width(desc.width),
    m_Height(desc.height),
    m_Resizable(desc.resizable)
{
    xcb_connection_t *c = Connection();
    xcb_screen_t *pScreen = display.Screen();

    // Value order follows the mask bits. Without a background pixel the server leaves exposed
    // areas alone, so a swapchain owns the window's contents.
    uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t values[2];
    uint32_t count = 0;
    if (desc.backgroundColor)
    {
        mask |= XCB_CW_BACK_PIXEL;
        values[count++] = *desc.backgroundColor; // 0xRRGGBB is the pixel value on 24-bit TrueColor
    }
    values[count++] = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE;

    m_Window = xcb_generate_id(c);
    xcb_void_cookie_t cookie = xcb_create_window_checked(c, XCB_COPY_FROM_PARENT, m_Window, pScreen->root,
        0, 0, static_cast<uint16_t>(desc.width), static_cast<uint16_t>(desc.height), 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, pScreen->root_visual, mask, values);
    if (XcbPtr<xcb_generic_error_t> error{ xcb_request_check(c, cookie) })
        throw Error("Casement: xcb_create_window failed (error " + std::to_string(error->error_code) + ")");

    xcb_change_property(c, XCB_PROP_MODE_REPLACE, m_Window, display.m_WmProtocols, XCB_ATOM_ATOM, 32, 1, &display.m_WmDeleteWindow);

    static constexpr char c_WmClass[] = "casement\0Casement";
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, m_Window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(c_WmClass), c_WmClass);

    SetTitle(desc.title);
    if (!m_Resizable)
        SetSizeHints(desc.width, desc.height);
    if (desc.fullscreen)
        SetFullscreen(true);

    display.AddWindow(m_Window, this);
    xcb_flush(c);
}

//------------------------------------------------------------------------------------------------
CX11Window::~CX11Window()
{
    m_Display.RemoveWindow(m_Window);
    Ungrab();
    xcb_destroy_window(Connection(), m_Window);
    xcb_flush(Connection());
}

//------------------------------------------------------------------------------------------------
void CX11Window::Show()
{
    xcb_map_window(Connection(), m_Window);
    xcb_flush(Connection());
}

//------------------------------------------------------------------------------------------------
void CX11Window::SetTitle(const std::string &title)
{
    auto length = static_cast<uint32_t>(title.size());
    xcb_change_property(Connection(), XCB_PROP_MODE_REPLACE, m_Window, m_Display.m_NetWmName, m_Display.m_Utf8String, 8, length, title.data());
    xcb_change_property(Connection(), XCB_PROP_MODE_REPLACE, m_Window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, length, title.data());
    xcb_flush(Connection());
}

//------------------------------------------------------------------------------------------------
void CX11Window::SetIcon(const IconImage *pImages, size_t count)
{
    // _NET_WM_ICON (EWMH): for each size, width and height, then 0xAARRGGBB pixels, as CARDINALs.
    std::vector<uint32_t> data;
    for (size_t i = 0; i < count; ++i)
    {
        const IconImage &image = pImages[i];
        if (image.width <= 0 || image.height <= 0 || image.pPixels == nullptr)
            continue;
        data.push_back(static_cast<uint32_t>(image.width));
        data.push_back(static_cast<uint32_t>(image.height));
        data.insert(data.end(), image.pPixels, image.pPixels + static_cast<size_t>(image.width) * static_cast<size_t>(image.height));
    }

    if (data.empty())
        xcb_delete_property(Connection(), m_Window, m_Display.m_NetWmIcon);
    else
        xcb_change_property(Connection(), XCB_PROP_MODE_REPLACE, m_Window, m_Display.m_NetWmIcon, XCB_ATOM_CARDINAL, 32,
            static_cast<uint32_t>(data.size()), data.data());
    xcb_flush(Connection());
}

//------------------------------------------------------------------------------------------------
void CX11Window::SetSizeHints(int width, int height)
{
    // WM_SIZE_HINTS (ICCCM 4.1.2.3) with PMinSize | PMaxSize set to a single size.
    uint32_t hints[18] = {};
    hints[0] = (1u << 4) | (1u << 5);
    hints[5] = hints[7] = static_cast<uint32_t>(width);
    hints[6] = hints[8] = static_cast<uint32_t>(height);
    xcb_change_property(Connection(), XCB_PROP_MODE_REPLACE, m_Window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, 18, hints);
}

//------------------------------------------------------------------------------------------------
void CX11Window::SetClientSize(int width, int height)
{
    if (!m_Resizable)
        SetSizeHints(width, height);

    uint32_t values[] = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    xcb_configure_window(Connection(), m_Window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    xcb_flush(Connection());
}

//------------------------------------------------------------------------------------------------
void CX11Window::SetFullscreen(bool fullscreen)
{
    if (fullscreen == m_Fullscreen)
        return;
    m_Fullscreen = fullscreen;

    xcb_connection_t *c = Connection();
    if (!m_Mapped)
    {
        // The window manager reads _NET_WM_STATE when the window is first mapped.
        if (fullscreen)
            xcb_change_property(c, XCB_PROP_MODE_REPLACE, m_Window, m_Display.m_NetWmState, XCB_ATOM_ATOM, 32, 1, &m_Display.m_NetWmStateFullscreen);
        else
            xcb_delete_property(c, m_Window, m_Display.m_NetWmState);
    }
    else
    {
        // Once mapped, state changes are requests to the window manager (EWMH _NET_WM_STATE).
        xcb_client_message_event_t event = {};
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.window = m_Window;
        event.type = m_Display.m_NetWmState;
        event.data.data32[0] = fullscreen ? 1 : 0; // _NET_WM_STATE_ADD / _NET_WM_STATE_REMOVE
        event.data.data32[1] = m_Display.m_NetWmStateFullscreen;
        event.data.data32[3] = 1; // Source: application
        xcb_send_event(c, 0, m_Display.Screen()->root,
            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
            reinterpret_cast<const char *>(&event));
    }
    xcb_flush(c);
}

//------------------------------------------------------------------------------------------------
bool CX11Window::SetMouseCaptured(bool captured)
{
    if (captured == m_Captured)
        return true;
    m_Captured = captured;

    uint32_t cursor = captured ? m_Display.InvisibleCursor() : xcb_cursor_t(XCB_CURSOR_NONE);
    xcb_change_window_attributes(Connection(), m_Window, XCB_CW_CURSOR, &cursor);

    if (captured)
        Grab();
    else
        Ungrab();
    xcb_flush(Connection());
    return true;
}

//------------------------------------------------------------------------------------------------
void CX11Window::Grab()
{
    if (m_Grabbed || !m_Mapped || !m_Focused)
        return;

    // Fails while another client holds a grab (a window manager mid-drag, say); motion retries.
    xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(Connection(), 0, m_Window,
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, m_Window, m_Display.InvisibleCursor(), XCB_CURRENT_TIME);
    XcbPtr<xcb_grab_pointer_reply_t> reply(xcb_grab_pointer_reply(Connection(), cookie, nullptr));
    m_Grabbed = reply && reply->status == XCB_GRAB_STATUS_SUCCESS;
    if (m_Grabbed)
        WarpToCenter();
}

//------------------------------------------------------------------------------------------------
void CX11Window::Ungrab()
{
    if (!m_Grabbed)
        return;
    xcb_ungrab_pointer(Connection(), XCB_CURRENT_TIME);
    m_Grabbed = false;
}

//------------------------------------------------------------------------------------------------
void CX11Window::WarpToCenter()
{
    xcb_warp_pointer(Connection(), XCB_NONE, m_Window, 0, 0, 0, 0,
        static_cast<int16_t>(m_Width / 2), static_cast<int16_t>(m_Height / 2));
    xcb_flush(Connection());
}

//------------------------------------------------------------------------------------------------
NativeWindow CX11Window::GetNativeWindow() const
{
    NativeWindow native;
    native.backend = Backend::X11;
    native.x11.connection = Connection();
    native.x11.window = m_Window;
    return native;
}

//------------------------------------------------------------------------------------------------
void CX11Window::OnKeyEvent(xcb_keycode_t keycode, bool pressed, bool repeat)
{
    Events().OnKey(*this, KeyFromEvdev(keycode >= 8 ? keycode - 8u : 0u), pressed, repeat);
}

//------------------------------------------------------------------------------------------------
void CX11Window::OnButtonEvent(xcb_button_t button, bool pressed)
{
    // Core protocol buttons: 1-3 left/middle/right, 4-7 wheel up/down/left/right, 8-9 back/forward.
    switch (button)
    {
    case 1: Events().OnMouseButton(*this, MouseButton::Left, pressed); break;
    case 2: Events().OnMouseButton(*this, MouseButton::Middle, pressed); break;
    case 3: Events().OnMouseButton(*this, MouseButton::Right, pressed); break;
    case 8: Events().OnMouseButton(*this, MouseButton::X1, pressed); break;
    case 9: Events().OnMouseButton(*this, MouseButton::X2, pressed); break;
    case 4: if (pressed) Events().OnMouseWheel(*this, 0.0, 1.0); break;
    case 5: if (pressed) Events().OnMouseWheel(*this, 0.0, -1.0); break;
    case 6: if (pressed) Events().OnMouseWheel(*this, -1.0, 0.0); break;
    case 7: if (pressed) Events().OnMouseWheel(*this, 1.0, 0.0); break;
    }
}

//------------------------------------------------------------------------------------------------
void CX11Window::OnMotionEvent(int x, int y)
{
    if (!m_Captured)
    {
        Events().OnMouseMove(*this, x, y);
        return;
    }

    if (!m_Grabbed)
    {
        Grab();
        return;
    }

    // Report the offset from the center and warp back; the warp's own motion event lands on the
    // center and is skipped.
    int dx = x - m_Width / 2;
    int dy = y - m_Height / 2;
    if (dx == 0 && dy == 0)
        return;
    Events().OnMouseDelta(*this, dx, dy);
    WarpToCenter();
}

//------------------------------------------------------------------------------------------------
void CX11Window::OnFocusEvent(bool focused)
{
    if (focused == m_Focused)
        return;
    m_Focused = focused;

    if (m_Captured)
    {
        if (focused)
            Grab();
        else
            Ungrab();
    }
    Events().OnFocusChanged(*this, focused);
}

//------------------------------------------------------------------------------------------------
void CX11Window::OnConfigureEvent(int width, int height)
{
    if (width == m_Width && height == m_Height)
        return;
    m_Width = width;
    m_Height = height;
    Events().OnResized(*this, width, height);
}

//------------------------------------------------------------------------------------------------
void CX11Window::OnMapEvent(bool mapped)
{
    m_Mapped = mapped;
    if (!mapped)
        Ungrab();
}

//------------------------------------------------------------------------------------------------
void CX11Window::OnClientMessage(const xcb_client_message_event_t &event)
{
    if (event.type == m_Display.m_WmProtocols && event.data.data32[0] == m_Display.m_WmDeleteWindow)
        Events().OnCloseRequested(*this);
}

} // namespace

//------------------------------------------------------------------------------------------------
std::unique_ptr<CDisplay> CreateX11Display()
{
    return std::make_unique<CX11Display>();
}

} // namespace Casement
