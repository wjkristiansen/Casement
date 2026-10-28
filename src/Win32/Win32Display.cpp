//================================================================================================
// Casement - Win32 backend
//================================================================================================

#define UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>

#include "../Backends.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace Casement
{
namespace
{

constexpr wchar_t c_ClassName[] = L"CasementWindow";

//------------------------------------------------------------------------------------------------
std::wstring Widen(const std::string &text)
{
    if (text.empty())
        return {};

    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

//------------------------------------------------------------------------------------------------
[[noreturn]] void ThrowLastError(const char *pWhat)
{
    throw Error(std::string("Casement: ") + pWhat + " failed (error " + std::to_string(GetLastError()) + ")");
}

//------------------------------------------------------------------------------------------------
DWORD WindowedStyle(bool resizable)
{
    return resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
}

//------------------------------------------------------------------------------------------------
const IconImage *ClosestIcon(const IconImage *pImages, size_t count, int size)
{
    const IconImage *pBest = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        const IconImage &image = pImages[i];
        if (image.width <= 0 || image.height <= 0 || image.pPixels == nullptr)
            continue;
        if (!pBest || std::abs(image.width - size) < std::abs(pBest->width - size))
            pBest = &image;
    }
    return pBest;
}

//------------------------------------------------------------------------------------------------
HICON CreateIconFromImage(const IconImage &image)
{
    // 0xAARRGGBB in memory is B, G, R, A: a top-down 32-bit BGRA DIB.
    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(header);
    header.bV5Width = image.width;
    header.bV5Height = -image.height;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    void *pBits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hColor = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO *>(&header), DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (hColor == nullptr)
        return nullptr;
    std::memcpy(pBits, image.pPixels, static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);

    // The alpha channel defines transparency; the 1-bit mask is required but unused, so clear it.
    std::vector<BYTE> mask(static_cast<size_t>((image.width + 15) / 16 * 2) * static_cast<size_t>(image.height));
    HBITMAP hMask = CreateBitmap(image.width, image.height, 1, 1, mask.data());

    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmMask = hMask;
    info.hbmColor = hColor;
    HICON hIcon = CreateIconIndirect(&info);
    DeleteObject(hColor);
    DeleteObject(hMask);
    return hIcon;
}

//------------------------------------------------------------------------------------------------
bool IsRawMouseRegistered()
{
    UINT count = 0;
    GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count == 0 || GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT(-1))
        return false;

    for (const RAWINPUTDEVICE &device : devices)
    {
        if (device.usUsagePage == 0x01 && device.usUsage == 0x02) // HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE
            return true;
    }
    return false;
}

//------------------------------------------------------------------------------------------------
class CWin32Window final : public CWindow
{
public:
    CWin32Window(HINSTANCE hInstance, const WindowDesc &desc, IWindowEvents *pEvents);
    ~CWin32Window() override;

    void Show() override { ::ShowWindow(m_hWnd, SW_SHOW); }
    void SetTitle(const std::string &title) override { SetWindowTextW(m_hWnd, Widen(title).c_str()); }
    void SetIcon(const IconImage *pImages, size_t count) override;
    void SetClientSize(int width, int height) override;
    void GetClientSize(int &width, int &height) const override;
    void SetFullscreen(bool fullscreen) override;
    bool IsFullscreen() const override { return m_Fullscreen; }
    bool HasFocus() const override { return m_Focused; }
    bool SetMouseCaptured(bool captured) override;
    bool IsMouseCaptured() const override { return m_Captured; }
    NativeWindow GetNativeWindow() const override;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    void OnKeyMessage(WPARAM wParam, LPARAM lParam);
    void OnButtonMessage(MouseButton button, bool pressed, WPARAM wParam);
    void OnRawInput(LPARAM lParam);
    void ClipToClientArea();
    void DestroyIcons();

    HINSTANCE m_hInstance;
    HWND m_hWnd = nullptr;
    HBRUSH m_hBackground = nullptr;
    HICON m_hIcon = nullptr;        // Set with SetIcon; null means the class icon
    HICON m_hSmallIcon = nullptr;
    DWORD m_Style;
    RECT m_WindowedRect = {};
    bool m_Fullscreen = false;
    bool m_Captured = false;
    bool m_OwnsRawMouse = false;    // Casement registered raw mouse input, so it unregisters it
    bool m_Focused = false;
    bool m_Dispatching = false; // Events are held back during construction and destruction
    std::vector<BYTE> m_RawInput;
};

//------------------------------------------------------------------------------------------------
CWin32Window::CWin32Window(HINSTANCE hInstance, const WindowDesc &desc, IWindowEvents *pEvents) :
    CWindow(pEvents),
    m_hInstance(hInstance),
    m_Style(WindowedStyle(desc.resizable))
{
    if (desc.backgroundColor)
    {
        uint32_t rgb = *desc.backgroundColor;
        m_hBackground = CreateSolidBrush(RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
    }

    RECT rect = { 0, 0, desc.width, desc.height };
    AdjustWindowRectEx(&rect, m_Style, FALSE, 0);

    HWND hWnd = CreateWindowExW(0, c_ClassName, Widen(desc.title).c_str(), m_Style,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, m_hInstance, this);
    if (hWnd == nullptr)
    {
        if (m_hBackground)
            DeleteObject(m_hBackground);
        ThrowLastError("CreateWindowExW");
    }

    if (desc.fullscreen)
        SetFullscreen(true);

    m_Dispatching = true;
}

//------------------------------------------------------------------------------------------------
CWin32Window::~CWin32Window()
{
    m_Dispatching = false;
    SetMouseCaptured(false);
    if (m_hWnd)
        DestroyWindow(m_hWnd);
    DestroyIcons();
    if (m_hBackground)
        DeleteObject(m_hBackground);
}

//------------------------------------------------------------------------------------------------
void CWin32Window::SetIcon(const IconImage *pImages, size_t count)
{
    HICON hIcon = nullptr;
    HICON hSmallIcon = nullptr;
    if (const IconImage *pImage = ClosestIcon(pImages, count, GetSystemMetrics(SM_CXICON)))
        hIcon = CreateIconFromImage(*pImage);
    if (const IconImage *pImage = ClosestIcon(pImages, count, GetSystemMetrics(SM_CXSMICON)))
        hSmallIcon = CreateIconFromImage(*pImage);

    // A null icon falls back to the window class's.
    SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
    SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmallIcon));

    DestroyIcons();
    m_hIcon = hIcon;
    m_hSmallIcon = hSmallIcon;
}

//------------------------------------------------------------------------------------------------
void CWin32Window::DestroyIcons()
{
    if (m_hIcon)
        DestroyIcon(m_hIcon);
    if (m_hSmallIcon)
        DestroyIcon(m_hSmallIcon);
    m_hIcon = nullptr;
    m_hSmallIcon = nullptr;
}

//------------------------------------------------------------------------------------------------
void CWin32Window::SetClientSize(int width, int height)
{
    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, m_Style, FALSE, 0);

    if (m_Fullscreen)
    {
        // Applied when leaving fullscreen.
        m_WindowedRect.right = m_WindowedRect.left + (rect.right - rect.left);
        m_WindowedRect.bottom = m_WindowedRect.top + (rect.bottom - rect.top);
        return;
    }

    SetWindowPos(m_hWnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

//------------------------------------------------------------------------------------------------
void CWin32Window::GetClientSize(int &width, int &height) const
{
    RECT rect = {};
    GetClientRect(m_hWnd, &rect);
    width = rect.right;
    height = rect.bottom;
}

//------------------------------------------------------------------------------------------------
void CWin32Window::SetFullscreen(bool fullscreen)
{
    if (fullscreen == m_Fullscreen)
        return;

    DWORD visible = GetWindowLongW(m_hWnd, GWL_STYLE) & WS_VISIBLE;

    if (fullscreen)
    {
        GetWindowRect(m_hWnd, &m_WindowedRect);

        MONITORINFO monitor = { sizeof(monitor) };
        GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT &rc = monitor.rcMonitor;

        SetWindowLongW(m_hWnd, GWL_STYLE, WS_POPUP | visible);
        SetWindowPos(m_hWnd, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
    else
    {
        const RECT &rc = m_WindowedRect;
        SetWindowLongW(m_hWnd, GWL_STYLE, m_Style | visible);
        SetWindowPos(m_hWnd, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }

    m_Fullscreen = fullscreen;
}

//------------------------------------------------------------------------------------------------
bool CWin32Window::SetMouseCaptured(bool captured)
{
    if (captured == m_Captured)
        return true;

    // Raw input registration is per process: leave one the application made alone, and remove
    // only Casement's own.
    bool registerMouse = captured && !IsRawMouseRegistered();
    bool unregisterMouse = !captured && m_OwnsRawMouse;
    if (registerMouse || unregisterMouse)
    {
        RAWINPUTDEVICE device = {};
        device.usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
        device.usUsage = 0x02;      // HID_USAGE_GENERIC_MOUSE
        device.dwFlags = registerMouse ? 0 : RIDEV_REMOVE;
        device.hwndTarget = registerMouse ? m_hWnd : nullptr;
        if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
            return false;
        m_OwnsRawMouse = registerMouse;
    }

    m_Captured = captured;

    if (captured)
    {
        if (m_Focused)
            ClipToClientArea();
        SetCursor(nullptr);
    }
    else
    {
        ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    return true;
}

//------------------------------------------------------------------------------------------------
void CWin32Window::ClipToClientArea()
{
    RECT rect = {};
    GetClientRect(m_hWnd, &rect);
    MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<POINT *>(&rect), 2);
    ClipCursor(&rect);
}

//------------------------------------------------------------------------------------------------
NativeWindow CWin32Window::GetNativeWindow() const
{
    NativeWindow native;
    native.backend = Backend::Win32;
    native.win32.hInstance = m_hInstance;
    native.win32.hWnd = m_hWnd;
    return native;
}

//------------------------------------------------------------------------------------------------
LRESULT CALLBACK CWin32Window::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto pCreate = reinterpret_cast<CREATESTRUCTW *>(lParam);
        auto pWindow = static_cast<CWin32Window *>(pCreate->lpCreateParams);
        pWindow->m_hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
    }

    auto pWindow = reinterpret_cast<CWin32Window *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (pWindow == nullptr)
        return DefWindowProcW(hWnd, message, wParam, lParam);

    if (message == WM_NCDESTROY)
    {
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        pWindow->m_hWnd = nullptr;
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    return pWindow->WindowProc(message, wParam, lParam);
}

//------------------------------------------------------------------------------------------------
LRESULT CWin32Window::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (m_Dispatching)
    {
        if (const Win32MessageHook &hook = GetWin32MessageHook())
        {
            intptr_t result = 0;
            if (hook(m_hWnd, message, wParam, lParam, result))
                return result;
        }
    }

    switch (message)
    {
    case WM_CLOSE:
        if (m_Dispatching)
            Events().OnCloseRequested(*this);
        return 0;

    case WM_SIZE:
        if (m_Captured && m_Focused)
            ClipToClientArea();
        if (m_Dispatching && wParam != SIZE_MINIMIZED)
            Events().OnResized(*this, LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_MOVE:
        if (m_Captured && m_Focused)
            ClipToClientArea();
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        m_Focused = (message == WM_SETFOCUS);
        if (m_Captured)
        {
            if (m_Focused)
                ClipToClientArea();
            else
                ClipCursor(nullptr);
        }
        if (m_Dispatching)
            Events().OnFocusChanged(*this, m_Focused);
        return 0;

    case WM_SETCURSOR:
        if (m_Captured && LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(nullptr);
            return TRUE;
        }
        break;

    case WM_ERASEBKGND:
        if (m_hBackground)
        {
            RECT rect = {};
            GetClientRect(m_hWnd, &rect);
            FillRect(reinterpret_cast<HDC>(wParam), &rect, m_hBackground);
        }
        return TRUE; // Without a background color the swapchain owns the client area

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(m_hWnd, &ps);
            EndPaint(m_hWnd, &ps);
        }
        return 0;

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        OnKeyMessage(wParam, lParam);
        if (message == WM_SYSKEYDOWN && wParam == VK_F4)
            break; // Let Alt+F4 become WM_CLOSE
        return 0; // Keep Alt and F10 from entering menu mode

    case WM_SYSCHAR:
        if (wParam == VK_SPACE)
            break; // Alt+Space opens the system menu
        return 0;

    case WM_MOUSEMOVE:
        if (m_Dispatching && !m_Captured)
            Events().OnMouseMove(*this, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONDOWN: OnButtonMessage(MouseButton::Left, true, wParam); return 0;
    case WM_LBUTTONUP:   OnButtonMessage(MouseButton::Left, false, wParam); return 0;
    case WM_RBUTTONDOWN: OnButtonMessage(MouseButton::Right, true, wParam); return 0;
    case WM_RBUTTONUP:   OnButtonMessage(MouseButton::Right, false, wParam); return 0;
    case WM_MBUTTONDOWN: OnButtonMessage(MouseButton::Middle, true, wParam); return 0;
    case WM_MBUTTONUP:   OnButtonMessage(MouseButton::Middle, false, wParam); return 0;

    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        OnButtonMessage(GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2,
            message == WM_XBUTTONDOWN, wParam);
        return TRUE;

    case WM_MOUSEWHEEL:
        if (m_Dispatching)
            Events().OnMouseWheel(*this, 0.0, double(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
        return 0;

    case WM_MOUSEHWHEEL:
        if (m_Dispatching)
            Events().OnMouseWheel(*this, double(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA, 0.0);
        return 0;

    case WM_INPUT:
        OnRawInput(lParam);
        break; // DefWindowProc releases the input buffer
    }

    return DefWindowProcW(m_hWnd, message, wParam, lParam);
}

//------------------------------------------------------------------------------------------------
void CWin32Window::OnKeyMessage(WPARAM wParam, LPARAM lParam)
{
    if (!m_Dispatching)
        return;

    auto flags = static_cast<uint32_t>(lParam >> 16);
    bool pressed = (flags & KF_UP) == 0;
    bool repeat = pressed && (flags & KF_REPEAT) != 0;

    // Pause and NumLock report each other's scan codes.
    Key key;
    if (wParam == VK_PAUSE)
        key = Key::Pause;
    else if (wParam == VK_NUMLOCK)
        key = Key::NumLock;
    else
        key = KeyFromScanCode(flags & 0xFF, (flags & KF_EXTENDED) != 0);

    Events().OnKey(*this, key, pressed, repeat);
}

//------------------------------------------------------------------------------------------------
void CWin32Window::OnButtonMessage(MouseButton button, bool pressed, WPARAM wParam)
{
    // Hold Win32 capture while any button is down so releases outside the window still arrive.
    constexpr WORD c_AnyButton = MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2;
    if (pressed)
        SetCapture(m_hWnd);
    else if ((GET_KEYSTATE_WPARAM(wParam) & c_AnyButton) == 0)
        ReleaseCapture();

    if (m_Dispatching)
        Events().OnMouseButton(*this, button, pressed);
}

//------------------------------------------------------------------------------------------------
void CWin32Window::OnRawInput(LPARAM lParam)
{
    if (!m_Dispatching || !m_Captured)
        return;

    UINT size = 0;
    auto hRawInput = reinterpret_cast<HRAWINPUT>(lParam);
    GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    m_RawInput.resize(size);
    if (GetRawInputData(hRawInput, RID_INPUT, m_RawInput.data(), &size, sizeof(RAWINPUTHEADER)) != size)
        return;

    auto pInput = reinterpret_cast<const RAWINPUT *>(m_RawInput.data());
    if (pInput->header.dwType != RIM_TYPEMOUSE || (pInput->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
        return;

    Events().OnMouseDelta(*this, pInput->data.mouse.lLastX, pInput->data.mouse.lLastY);
}

//------------------------------------------------------------------------------------------------
class CWin32Display final : public CDisplay
{
public:
    CWin32Display();
    ~CWin32Display() override;

    Backend GetBackend() const override { return Backend::Win32; }

    std::unique_ptr<CWindow> OpenWindow(const WindowDesc &desc, IWindowEvents *pEvents) override
    {
        return std::make_unique<CWin32Window>(m_hInstance, desc, pEvents);
    }

    void PumpEvents() override;
    void WaitEvents() override;

private:
    HINSTANCE m_hInstance = nullptr;
    HICON m_hIcon = nullptr;        // Extracted from the executable; destroyed with the display
    HICON m_hSmallIcon = nullptr;
};

//------------------------------------------------------------------------------------------------
CWin32Display::CWin32Display()
{
    // The module containing this code, which is not the .exe when Casement is linked into a DLL.
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CWin32Window::WndProc), &m_hInstance);

    // The executable's first icon resource, which is the one Explorer shows.
    // Copied out, as a string name isn't documented to outlive the enumeration.
    struct IconName
    {
        bool found = false;
        WORD id = 0;
        std::wstring text;
    } name;
    HMODULE hExe = GetModuleHandleW(nullptr);
    EnumResourceNamesW(hExe, RT_GROUP_ICON,
        [](HMODULE, LPCWSTR, LPWSTR pName, LONG_PTR pData) -> BOOL
        {
            auto pIconName = reinterpret_cast<IconName *>(pData);
            pIconName->found = true;
            if (IS_INTRESOURCE(pName))
                pIconName->id = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(pName));
            else
                pIconName->text = pName;
            return FALSE; // Stop at the first
        },
        reinterpret_cast<LONG_PTR>(&name));
    if (name.found)
    {
        LPCWSTR pIconName = name.text.empty() ? MAKEINTRESOURCEW(name.id) : name.text.c_str();
        m_hIcon = static_cast<HICON>(LoadImageW(hExe, pIconName, IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
        m_hSmallIcon = static_cast<HICON>(LoadImageW(hExe, pIconName, IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    }

    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = CWin32Window::WndProc;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = m_hIcon ? m_hIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcex.hIconSm = m_hSmallIcon;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.lpszClassName = c_ClassName;
    if (RegisterClassExW(&wcex) == 0)
    {
        DWORD error = GetLastError();
        if (m_hIcon) DestroyIcon(m_hIcon);
        if (m_hSmallIcon) DestroyIcon(m_hSmallIcon);
        SetLastError(error);
        ThrowLastError("RegisterClassExW");
    }
}

//------------------------------------------------------------------------------------------------
CWin32Display::~CWin32Display()
{
    UnregisterClassW(c_ClassName, m_hInstance);
    if (m_hIcon)
        DestroyIcon(m_hIcon);
    if (m_hSmallIcon)
        DestroyIcon(m_hSmallIcon);
}

//------------------------------------------------------------------------------------------------
void CWin32Display::PumpEvents()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

//------------------------------------------------------------------------------------------------
void CWin32Display::WaitEvents()
{
    WaitMessage();
    PumpEvents();
}

} // namespace

//------------------------------------------------------------------------------------------------
std::unique_ptr<CDisplay> CreateWin32Display()
{
    return std::make_unique<CWin32Display>();
}

} // namespace Casement
