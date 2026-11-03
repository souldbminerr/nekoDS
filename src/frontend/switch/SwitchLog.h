// Switch file logging. printf() goes nowhere visible on hardware, so all
// diagnostics go to sdmc:/switch/nekoDS/nekoDS.log (appended, flushed per
// line so it survives crashes) plus a stdout mirror for nxlink sessions.
// Opened lazily on first use; safe to call before anything else is up.

#ifndef NEKODS_SWITCH_LOG_H
#define NEKODS_SWITCH_LOG_H

#include <cstdio>
#include <cstdarg>
#include <sys/stat.h>

namespace SwitchLog
{

inline FILE*& Handle()
{
    static FILE* f = nullptr;
    return f;
}

inline void Init()
{
    if (Handle()) return;
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/nekoDS", 0777);
    Handle() = fopen("sdmc:/switch/nekoDS/nekoDS.log", "w");
    if (Handle())
    {
        fputs("--- nekoDS boot ---\n", Handle());
        fflush(Handle());
    }
}

inline void Log(const char* fmt, ...)
{
    if (!Handle()) Init();
    va_list a1, a2;
    va_start(a1, fmt);
    va_copy(a2, a1);
    if (Handle())
    {
        vfprintf(Handle(), fmt, a1);
        fputc('\n', Handle());
        fflush(Handle());
    }
    vprintf(fmt, a2);
    putchar('\n');
    va_end(a1);
    va_end(a2);
}

} // namespace SwitchLog

#endif
