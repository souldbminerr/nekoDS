/*
    Copyright 2026 Souldbminer

    This file is part of nekoDS.

    nekoDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    nekoDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with nekoDS. If not, see http://www.gnu.org/licenses/.
*/

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
    if (Handle())
        return;
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
