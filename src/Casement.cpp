//================================================================================================
// Casement - backend selection
//================================================================================================

#include "Backends.h"

#include <cstdlib>
#include <cstring>

#if !CASEMENT_HAS_WIN32
#include <unistd.h>
#endif

namespace Casement
{

//------------------------------------------------------------------------------------------------
const char *BackendName(Backend backend)
{
    switch (backend)
    {
    case Backend::Auto:    return "Auto";
    case Backend::Win32:   return "Win32";
    case Backend::Wayland: return "Wayland";
    case Backend::X11:     return "X11";
    }
    return "Unknown";
}

//------------------------------------------------------------------------------------------------
static std::unique_ptr<CDisplay> CreateBackend(Backend backend)
{
    switch (backend)
    {
#if CASEMENT_HAS_WIN32
    case Backend::Win32:   return CreateWin32Display();
#endif
#if CASEMENT_HAS_WAYLAND
    case Backend::Wayland: return CreateWaylandDisplay();
#endif
#if CASEMENT_HAS_X11
    case Backend::X11:     return CreateX11Display();
#endif
    default:
        throw Error(std::string("Casement: backend not compiled in: ") + BackendName(backend));
    }
}

#if !CASEMENT_HAS_WIN32
//------------------------------------------------------------------------------------------------
static Backend BackendFromEnvironment()
{
    const char *pName = std::getenv("CASEMENT_BACKEND");
    if (pName == nullptr)
        return Backend::Auto;
    if (std::strcmp(pName, "wayland") == 0)
        return Backend::Wayland;
    if (std::strcmp(pName, "x11") == 0)
        return Backend::X11;
    return Backend::Auto;
}

#if CASEMENT_HAS_X11
//------------------------------------------------------------------------------------------------
// WSLg forwards Linux windows to the Windows desktop. Its compositor leaves a ghost of libdecor's
// shadow behind when a decorated Wayland window maximizes, and draws X11 window frames itself.
static bool IsWslg()
{
    return std::getenv("WSL_DISTRO_NAME") != nullptr && access("/mnt/wslg", F_OK) == 0;
}
#endif
#endif

//------------------------------------------------------------------------------------------------
std::unique_ptr<CDisplay> CDisplay::Create(Backend backend)
{
    if (backend != Backend::Auto)
        return CreateBackend(backend);

#if CASEMENT_HAS_WIN32
    return CreateWin32Display();
#else
    backend = BackendFromEnvironment();
    if (backend != Backend::Auto)
        return CreateBackend(backend);

    struct Candidate
    {
        const char *pVariable;  // Must be set for the backend to be tried
        std::unique_ptr<CDisplay> (*pCreate)();
    };
    Candidate candidates[2];
    size_t count = 0;

#if CASEMENT_HAS_X11
    bool preferX11 = IsWslg();
    if (preferX11)
        candidates[count++] = { "DISPLAY", CreateX11Display };
#endif
#if CASEMENT_HAS_WAYLAND
    candidates[count++] = { "WAYLAND_DISPLAY", CreateWaylandDisplay };
#endif
#if CASEMENT_HAS_X11
    if (!preferX11)
        candidates[count++] = { "DISPLAY", CreateX11Display };
#endif

    std::string failures;
    for (size_t i = 0; i < count; ++i)
    {
        if (std::getenv(candidates[i].pVariable) == nullptr)
            continue;
        try
        {
            return candidates[i].pCreate();
        }
        catch (const Error &e)
        {
            failures += std::string(e.what()) + "; ";
        }
    }

    throw Error("Casement: no usable windowing backend (" + failures + "is WAYLAND_DISPLAY or DISPLAY set?)");
#endif
}

} // namespace Casement
