// Process resources for the QA suite (EmberQaTests --qa-full, memory stability). Own file so <windows.h> stays away
// from the JUCE headers (like EmberBenchTime.cpp).
#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <psapi.h>
 #pragma comment (lib, "psapi.lib")

void emberQaSysInfo (double& privateMB, double& workingSetMB, int& handles, int& gdiObjects, int& userObjects)
{
    PROCESS_MEMORY_COUNTERS_EX pmc {};
    pmc.cb = sizeof (pmc);
    privateMB = workingSetMB = 0.0;
    if (GetProcessMemoryInfo (GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*> (&pmc), sizeof (pmc)))
    {
        privateMB = (double) pmc.PrivateUsage / (1024.0 * 1024.0);
        workingSetMB = (double) pmc.WorkingSetSize / (1024.0 * 1024.0);
    }
    DWORD h = 0;
    handles = GetProcessHandleCount (GetCurrentProcess(), &h) ? (int) h : -1;
    gdiObjects = (int) GetGuiResources (GetCurrentProcess(), GR_GDIOBJECTS);
    userObjects = (int) GetGuiResources (GetCurrentProcess(), GR_USEROBJECTS);
}
#else
void emberQaSysInfo (double& privateMB, double& workingSetMB, int& handles, int& gdiObjects, int& userObjects)
{
    privateMB = workingSetMB = 0.0;
    handles = gdiObjects = userObjects = -1;
}
#endif
