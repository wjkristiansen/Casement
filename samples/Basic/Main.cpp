//================================================================================================
// CasementBasic - opens a window and logs its events
//
//   Escape   release the mouse, or quit
//   C        toggle mouse capture
//   F11      toggle fullscreen
//================================================================================================

#include <Casement/Casement.h>

#include <cstdio>

using namespace Casement;

//------------------------------------------------------------------------------------------------
class CSampleEvents : public IWindowEvents
{
public:
    bool m_Running = true;

    void OnCloseRequested(CWindow &) override
    {
        std::printf("Close requested\n");
        m_Running = false;
    }

    void OnResized(CWindow &, int width, int height) override
    {
        std::printf("Resized to %d x %d\n", width, height);
    }

    void OnFocusChanged(CWindow &, bool focused) override
    {
        std::printf("Focus %s\n", focused ? "gained" : "lost");
    }

    void OnKey(CWindow &window, Key key, bool pressed, bool repeat) override
    {
        std::printf("Key %s %s%s\n", KeyName(key), pressed ? "down" : "up", repeat ? " (repeat)" : "");
        if (!pressed || repeat)
            return;

        switch (key)
        {
        case Key::Escape:
            if (window.IsMouseCaptured())
                window.SetMouseCaptured(false);
            else
                m_Running = false;
            break;

        case Key::C:
            if (!window.SetMouseCaptured(!window.IsMouseCaptured()))
                std::printf("Mouse capture is not supported here\n");
            break;

        case Key::F11:
            window.SetFullscreen(!window.IsFullscreen());
            break;

        default:
            break;
        }
    }

    void OnMouseDelta(CWindow &, double dx, double dy) override
    {
        std::printf("Mouse delta %+.1f %+.1f\n", dx, dy);
    }

    void OnMouseButton(CWindow &, MouseButton button, bool pressed) override
    {
        static const char *const s_Names[] = { "Left", "Right", "Middle", "X1", "X2" };
        std::printf("Mouse %s %s\n", s_Names[static_cast<int>(button)], pressed ? "down" : "up");
    }

    void OnMouseWheel(CWindow &, double dx, double dy) override
    {
        std::printf("Wheel %+.2f %+.2f\n", dx, dy);
    }
};

//------------------------------------------------------------------------------------------------
int main()
{
    try
    {
        auto pDisplay = CDisplay::Create();
        std::printf("Backend: %s\n", BackendName(pDisplay->GetBackend()));

        WindowDesc desc;
        desc.title = "Casement Basic";
        desc.backgroundColor = 0x203060;

        CSampleEvents events;
        auto pWindow = pDisplay->OpenWindow(desc, &events);
        pWindow->Show();

        while (events.m_Running)
            pDisplay->WaitEvents();

        return 0;
    }
    catch (const Error &e)
    {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
