// CPU time of the calling thread (for EmberTests --bench). Own file so <windows.h> stays away from the JUCE headers.
#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>

double emberThreadCpuMs()
{
    FILETIME c, e, k, u;
    if (! GetThreadTimes (GetCurrentThread(), &c, &e, &k, &u))
        return 0.0;
    const auto toMs = [] (FILETIME f) { return (double) ((ULONGLONG) f.dwHighDateTime << 32 | f.dwLowDateTime) / 10000.0; };
    return toMs (k) + toMs (u);
}

double emberProcessCpuMs()
{
    FILETIME c, e, k, u;
    if (! GetProcessTimes (GetCurrentProcess(), &c, &e, &k, &u))
        return 0.0;
    const auto toMs = [] (FILETIME f) { return (double) ((ULONGLONG) f.dwHighDateTime << 32 | f.dwLowDateTime) / 10000.0; };
    return toMs (k) + toMs (u);
}
#else
 #include <ctime>
double emberThreadCpuMs()
{
    timespec t {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1.0e6;
}
double emberProcessCpuMs()
{
    timespec t {};
    clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1.0e6;
}
#endif
