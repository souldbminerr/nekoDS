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

#pragma once

namespace Config {

    extern u64 Mapping[15];

    extern int ScreenRotation; // 0..3
    extern int ScreenGap;
    extern int ScreenLayout;   // 0=natural, 1=vertical, 2=horizontal
    extern int ScreenSizing;   // 0=even, 1=emphTop, 2=emphBot

    extern int LimitFPS;

    extern int DirectBoot;
    extern int SavestateRelocSRAM;

    extern int AudioVolume;    // 0..4
    extern int MicInputType;   // 0=none, 1=mic, 2=noise

    extern char LastROMFolder[512];

    extern int SwitchOverclock; // 0..3

    void Load();
    void Save();
} // namespace Config