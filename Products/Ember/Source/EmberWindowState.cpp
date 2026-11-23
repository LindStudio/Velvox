// Is the host's window minimised or hidden? (LiveMascotView pauses Ember then.) A plugin editor is a child window inside
// the host's window: JUCE's isShowing() stays true for it when the host is minimised or hides the plugin window, so the
// top-level window is asked here. Own file so <windows.h> stays away from the JUCE headers.
#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>

namespace ember
{
    bool emberHostWindowHidden (void* nativeHandle)
    {
        const auto hwnd = static_cast<HWND> (nativeHandle);
        if (hwnd == nullptr || ! IsWindow (hwnd))
            return false;
        if (! IsWindowVisible (hwnd))      // this window or one of its parents hidden
            return true;
        const auto root = GetAncestor (hwnd, GA_ROOT);
        return root != nullptr && IsIconic (root) != FALSE;
    }
}
#else
namespace ember
{
    bool emberHostWindowHidden (void*) { return false; }
}
#endif
