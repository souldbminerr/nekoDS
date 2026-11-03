#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <switch.h>

#include "PlatformConfig.h"

namespace Config
{

u64 Mapping[15];

int ScreenRotation;
int ScreenGap;
int ScreenLayout;
int ScreenSizing;

int LimitFPS;

int DirectBoot;
int SavestateRelocSRAM;

int AudioVolume;
int MicInputType;

char LastROMFolder[512];

int SwitchOverclock;

static const char* kPath = "sdmc:/switch/nekoDS/nekoDS.ini";

static void SetDefaults()
{
    Mapping[0]  = HidNpadButton_A;
    Mapping[1]  = HidNpadButton_B;
    Mapping[2]  = HidNpadButton_Minus;
    Mapping[3]  = HidNpadButton_Plus;
    Mapping[4]  = HidNpadButton_Right;
    Mapping[5]  = HidNpadButton_Left;
    Mapping[6]  = HidNpadButton_Up;
    Mapping[7]  = HidNpadButton_Down;
    Mapping[8]  = HidNpadButton_R;
    Mapping[9]  = HidNpadButton_L;
    Mapping[10] = HidNpadButton_X;
    Mapping[11] = HidNpadButton_Y;
    Mapping[12] = HidNpadButton_StickR;
    Mapping[13] = HidNpadButton_StickL;
    Mapping[14] = HidNpadButton_L | HidNpadButton_R;

    ScreenRotation = 0;
    ScreenGap = 0;
    ScreenLayout = 0;
    ScreenSizing = 0;
    LimitFPS = 1;
    DirectBoot = 1;
    SavestateRelocSRAM = 0;
    AudioVolume = 4;
    MicInputType = 0;
    strncpy(LastROMFolder, "sdmc:/", sizeof(LastROMFolder));
    SwitchOverclock = 0;
}

static void ParseLine(char* line)
{
    char* eq = strchr(line, '=');
    if (!eq) return;
    *eq = 0;
    const char* key = line;
    const char* val = eq + 1;
    // trim trailing newline
    size_t vl = strlen(val);
    while (vl && (val[vl-1] == '\n' || val[vl-1] == '\r')) vl--;
    char vbuf[512]; snprintf(vbuf, sizeof(vbuf), "%.*s", (int)vl, val);

    int ival = atoi(vbuf);
    #define SET_INT(name, var) if (!strcmp(key, name)) { var = ival; return; }
    #define SET_MAP(i, name) if (!strcmp(key, name)) { Mapping[i] = strtoull(vbuf, nullptr, 0); return; }
    SET_MAP(0,"Joy_A"); SET_MAP(1,"Joy_B"); SET_MAP(2,"Joy_Select");
    SET_MAP(3,"Joy_Start"); SET_MAP(4,"Joy_Right"); SET_MAP(5,"Joy_Left");
    SET_MAP(6,"Joy_Up"); SET_MAP(7,"Joy_Down"); SET_MAP(8,"Joy_R");
    SET_MAP(9,"Joy_L"); SET_MAP(10,"Joy_X"); SET_MAP(11,"Joy_Y");
    SET_MAP(12,"Joy_Lid"); SET_MAP(13,"Joy_Mic"); SET_MAP(14,"Joy_Menu");
    SET_INT("ScreenRotation", ScreenRotation);
    SET_INT("ScreenGap", ScreenGap);
    SET_INT("ScreenLayout", ScreenLayout);
    SET_INT("ScreenSizing", ScreenSizing);
    SET_INT("LimitFPS", LimitFPS);
    SET_INT("DirectBoot", DirectBoot);
    SET_INT("SavStaRelocSRAM", SavestateRelocSRAM);
    SET_INT("AudioVolume", AudioVolume);
    SET_INT("MicInputType", MicInputType);
    SET_INT("SwitchOverclock", SwitchOverclock);
    if (!strcmp(key, "LastROMFolder"))
        snprintf(LastROMFolder, sizeof(LastROMFolder), "%s", vbuf);
    #undef SET_INT
    #undef SET_MAP
}

void Load()
{
    SetDefaults();
    FILE* f = fopen(kPath, "r");
    if (!f) return;
    char line[600];
    while (fgets(line, sizeof(line), f))
        if (line[0] != '#' && line[0] != '\n' && line[0] != '\r')
            ParseLine(line);
    fclose(f);
}

void Save()
{
    // ensure dir exists
    mkdir("sdmc:/switch/nekoDS", 0777);
    FILE* f = fopen(kPath, "w");
    if (!f) return;
    const char* names[15] = {"Joy_A","Joy_B","Joy_Select","Joy_Start","Joy_Right",
        "Joy_Left","Joy_Up","Joy_Down","Joy_R","Joy_L","Joy_X","Joy_Y",
        "Joy_Lid","Joy_Mic","Joy_Menu"};
    for (int i = 0; i < 15; i++)
        fprintf(f, "%s=%llu\n", names[i], (unsigned long long)Mapping[i]);
    fprintf(f, "ScreenRotation=%d\nScreenGap=%d\nScreenLayout=%d\nScreenSizing=%d\n",
        ScreenRotation, ScreenGap, ScreenLayout, ScreenSizing);
    fprintf(f, "LimitFPS=%d\nDirectBoot=%d\nSavStaRelocSRAM=%d\n",
        LimitFPS, DirectBoot, SavestateRelocSRAM);
    fprintf(f, "AudioVolume=%d\nMicInputType=%d\n", AudioVolume, MicInputType);
    fprintf(f, "LastROMFolder=%s\nSwitchOverclock=%d\n", LastROMFolder, SwitchOverclock);
    fclose(f);
}

}
