#ifndef NEKODS_SWITCH_PLATFORMCONFIG_H
#define NEKODS_SWITCH_PLATFORMCONFIG_H

// Minimal Switch config, ported from melonDS old switch port
// (src/switch/PlatformConfig.h). Plain globals + tiny INI load/save,
// no Qt/TOML dependency.

namespace Config
{

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

#endif
