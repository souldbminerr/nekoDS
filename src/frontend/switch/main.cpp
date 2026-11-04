// nekoDS Switch frontend entry point.
// Ported from melonDS old switch port (src/switch/main.cpp):
// same UX (file browser, pause menu, settings, controls, hotkeys,
// screen layouts, overclock, audio resample), adapted to:
//  - nekoDS instance-based core (NDS object + SoftRenderer + ParseROM)
//  - SDL3 video/audio via dynamic sdl3.dnro (SDL-dlink, deko3d backend)
//  - dynamic neko3d.dnro + sdl3.dnro loaded at launch (libnx-dlink)
//  - adopted hbmenu NWindow (no manual VI layer management)

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <switch.h>

#include "SwitchSDL.h"
#include "SwitchLog.h"
#include "ui.h"
#include "PlatformConfig.h"
#include "DekoGPU.h"

// Extra Nv transfer memory for the second deko3d device (qlaunch-ext uses
// the same value for its deko3d stack).
extern "C" {
u32 __nx_nv_transfermem_size = 0x800000;
}

#include "Platform.h"
#include "NDS.h"
#include "GPU.h"
#include "GPU_Soft.h"
#include "SPU.h"
#include "Savestate.h"
#include "NDSCart.h"
#include "FreeBIOS.h"
#include "SPI_Firmware.h"
#include "Args.h"

using namespace melonDS;
using namespace SwitchSDL;

typedef struct
{
    std::vector<std::string> names;
    int* value;
} SettingValue;

static std::string romPath, sramPath, statePath, sramStatePath;
static std::string saveUserdata; // stable pointer for WriteNDSSave userdata

static NDS* Console = nullptr;
static bool ConsoleRunning = false;

static float topX, topY, topWidth, topHeight, botX, botY, botWidth, botHeight;

static ::Thread audioThread;
static ClkrstSession cpuSession;
static AppletHookCookie cookie;

PadState gPad;

static SDL_Window* Win = nullptr;
static SDL_Renderer* Ren = nullptr;
static SDL_Texture* TexTop = nullptr;
static SDL_Texture* TexBot = nullptr;
static SDL_AudioStream* AudioStream = nullptr;

static u8 hotkeyMask = 0;
static bool lidClosed = false;
static bool quitFlag = false;
static int frameNo = 0;
// true when the deko GPU is active: its readback buffers are already RGBA
// byte order. The soft renderer outputs BGRA u32s and needs a swizzle.
static bool UsingDeko = false;
static u32 SwizzleBuf[2][256*192];
static bool corePaused = true;
static bool audioStarted = false;
static bool ocStarted = false;
static bool ocUseClkrst = false;
// false when neko3d.dnro is missing/unbindable -> software renderer
static bool UseDeko = false;

static const int clockSpeeds[] = { 1020000000, 1224000000, 1581000000, 1785000000 };

static const std::vector<std::string> controlNames =
{
    "A Button", "B Button", "Select Button", "Start Button",
    "D-Pad Right", "D-Pad Left", "D-Pad Up", "D-Pad Down",
    "R Button", "L Button", "X Button", "Y Button",
    "Close/Open Lid", "Microphone", "Pause Menu"
};

// libnx hid button names for display (16 physical buttons; stick
// directions are analog and can't be bound here)
static const std::vector<std::string> controlValues =
{
    "A", "B", "X", "Y", "L-Stick", "R-Stick",
    "L", "R", "ZL", "ZR", "Plus", "Minus",
    "D-Left", "D-Up", "D-Right", "D-Down"
};
static const u64 controlBits[] =
{
    HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
    HidNpadButton_StickL, HidNpadButton_StickR,
    HidNpadButton_L, HidNpadButton_R,
    HidNpadButton_ZL, HidNpadButton_ZR,
    HidNpadButton_Plus, HidNpadButton_Minus,
    HidNpadButton_Left, HidNpadButton_Up,
    HidNpadButton_Right, HidNpadButton_Down
};

static const std::vector<std::string> settingNames =
{
    "Boot Game Directly", "Frameskip", "Audio Volume", "Microphone Input",
    "Separate Savefiles from Savestates", "Screen Rotation", "Mid-Screen Gap",
    "Screen Layout", "Screen Sizing", "Limit Framerate", "Switch Overclock"
};

static int Frameskip = 0;

static const std::vector<SettingValue> settingValues =
{
    { { "Off", "On" },                                                              &Config::DirectBoot         },
    { { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" },                          &Frameskip                   },
    { { "0%", "25%", "50%", "75%", "100%" },                                        &Config::AudioVolume        },
    { { "None", "Microphone", "White Noise" },                                      &Config::MicInputType       },
    { { "Off", "On" },                                                              &Config::SavestateRelocSRAM },
    { { "0", "90", "180", "270" },                                                  &Config::ScreenRotation     },
    { { "0 Pixels", "1 Pixel", "8 Pixels", "64 Pixels", "90 Pixels", "128 Pixels" }, &Config::ScreenGap          },
    { { "Natural", "Vertical", "Horizontal" },                                       &Config::ScreenLayout       },
    { { "Even", "Emphasize Top", "Emphasize Bottom" },                               &Config::ScreenSizing       },
    { { "Off", "On" },                                                              &Config::LimitFPS           },
    { { "1020 MHz", "1224 MHz", "1581 MHz", "1785 MHz" },                            &Config::SwitchOverclock    }
};

static const std::vector<std::string> pauseNames =
{
    "Resume", "Save State", "Load State", "Settings", "File Browser"
};

static u32* romIcon(std::string filename)
{
    FILE* rom = fopen(filename.c_str(), "rb");
    if (!rom) return nullptr;

    u32 offset;
    fseek(rom, 0x68, SEEK_SET);
    if (fread(&offset, sizeof(u32), 1, rom) != 1) { fclose(rom); return nullptr; }

    u8 data[512];
    fseek(rom, 0x20 + offset, SEEK_SET);
    if (fread(data, 1, 512, rom) != 512) { fclose(rom); return nullptr; }

    u16 palette[16];
    fseek(rom, 0x220 + offset, SEEK_SET);
    if (fread(palette, sizeof(u16), 16, rom) != 16) { fclose(rom); return nullptr; }
    fclose(rom);

    u8 indexes[1024];
    for (int i = 0; i < 512; i++)
    {
        indexes[i * 2] = data[i] & 0x0F;
        indexes[i * 2 + 1] = data[i] >> 4;
    }

    u32 tiles[32 * 32];
    for (int i = 0; i < 1024; i++)
    {
        u8 r = ((palette[indexes[i]] >> 0)  & 0x1F) * 255 / 31;
        u8 g = ((palette[indexes[i]] >> 5)  & 0x1F) * 255 / 31;
        u8 b = ((palette[indexes[i]] >> 10) & 0x1F) * 255 / 31;
        tiles[i] = rgbaToU32(r, g, b, 255);
    }

    u32* tex = new u32[32 * 32];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
            for (int k = 0; k < 4; k++)
                memcpy(&tex[256 * i + 32 * j + 8 * k], &tiles[256 * i + 8 * j + 64 * k], 8 * sizeof(u32));
    return tex;
}

static void swapValues(float* a, float* b) { float t = *a; *a = *b; *b = t; }

static void setScreenLayout()
{
    int gapSizes[] = { 0, 1, 8, 64, 90, 128 };
    float gap = (float)gapSizes[Config::ScreenGap];
    int layout = Config::ScreenLayout;
    if (layout == 0)
        layout = (Config::ScreenRotation % 2 == 0) ? 1 : 2;

    if (layout == 1)
    {
        if (Config::ScreenSizing == 0)
        {
            topHeight = botHeight = 360 - gap / 2;
            if (Config::ScreenRotation % 2 == 0) topWidth = botWidth = topHeight * 4 / 3;
            else topWidth = botWidth = topHeight * 3 / 4;
        }
        else if (Config::ScreenSizing == 1)
        {
            if (Config::ScreenRotation % 2 == 0) { botWidth = 256; botHeight = 192; topHeight = 720 - botHeight - gap; topWidth = topHeight * 4 / 3; }
            else { botWidth = 192; botHeight = 256; topHeight = 720 - botHeight - gap; topWidth = topHeight * 3 / 4; }
        }
        else
        {
            if (Config::ScreenRotation % 2 == 0) { topWidth = 256; topHeight = 192; botHeight = 720 - topHeight - gap; botWidth = botHeight * 4 / 3; }
            else { botWidth = 192; botHeight = 256; topHeight = 720 - botHeight - gap; topWidth = topHeight * 3 / 4; }
        }
        topX = 640 - topWidth / 2; botX = 640 - botWidth / 2;
        topY = 0; botY = 720 - botHeight;
    }
    else
    {
        if (Config::ScreenRotation % 2 == 0)
        {
            topWidth = botWidth = 640 - gap / 2;
            topHeight = botHeight = topWidth * 3 / 4;
            topX = 0; botX = 1280 - topWidth;
        }
        else
        {
            topHeight = botHeight = 720;
            topWidth = botWidth = topHeight * 3 / 4;
            topX = 640 - topWidth - gap / 2; botX = 640 + gap / 2;
        }
        topY = botY = 360 - topHeight / 2;

        if (Config::ScreenSizing == 1)
        {
            if (Config::ScreenRotation % 2 == 0)
            {
                botWidth = 256; botHeight = 192;
                topWidth = 1280 - botWidth - gap;
                if (topWidth > 960) topWidth = 960;
                topHeight = topWidth * 3 / 4;
                topX = 640 - (botWidth + topWidth + gap) / 2;
                botX = topX + topWidth + gap;
                topY = 360 - topHeight / 2; botY = topY + topHeight - botHeight;
            }
            else { botWidth = 192; botHeight = 256; topX += (topWidth - botWidth) / 2; botX += (topWidth - botWidth) / 2; botY = 720 - botHeight; }
        }
        else if (Config::ScreenSizing == 2)
        {
            if (Config::ScreenRotation % 2 == 0)
            {
                topWidth = 256; topHeight = 192;
                botWidth = 1280 - topWidth - gap;
                if (botWidth > 960) botWidth = 960;
                botHeight = botWidth * 3 / 4;
                topX = 640 - (botWidth + topWidth + gap) / 2;
                botX = topX + topWidth + gap;
                botY = 360 - botHeight / 2; topY = botY + botHeight - topHeight;
            }
            else { topWidth = 192; topHeight = 256; topX += (botWidth - topWidth) / 2; botX -= (botWidth - topWidth) / 2; topY = 720 - topHeight; }
        }
    }

    if (Config::ScreenRotation == 1 || Config::ScreenRotation == 2)
    {
        swapValues(&topX, &botX); swapValues(&topY, &botY);
        swapValues(&topWidth, &botWidth); swapValues(&topHeight, &botHeight);
    }
}

static void onAppletHook(AppletHookType hook, void* param)
{
    (void)param;
    if (hook == AppletHookType_OnOperationMode || hook == AppletHookType_OnPerformanceMode)
    {
        if (R_FAILED(pcvSetClockRate(PcvModule_CpuBus, clockSpeeds[Config::SwitchOverclock])))
            clkrstSetClockRate(&cpuSession, clockSpeeds[Config::SwitchOverclock]);
    }
}

// ---- savestates (buffer-based Savestate + Platform file IO) ----
static bool saveStateFile(const std::string& path)
{
    Savestate state;
    if (state.Error) return false;
    Console->DoSavestate(&state);
    if (state.Error) return false;
    Platform::FileHandle* f = Platform::OpenFile(path, Platform::FileMode::Write);
    if (!f) return false;
    bool ok = Platform::FileWrite(state.Buffer(), state.Length(), 1, f) != 0;
    Platform::CloseFile(f);
    return ok;
}

static bool loadStateFile(const std::string& path)
{
    Platform::FileHandle* f = Platform::OpenFile(path, Platform::FileMode::Read);
    if (!f) return false;
    u64 len = Platform::FileLength(f);
    if (!len || len > 64 * 1024 * 1024) { Platform::CloseFile(f); return false; }
    std::vector<u8> buf((size_t)len);
    if (Platform::FileRead(buf.data(), 1, (u64)buf.size(), f) == 0) { Platform::CloseFile(f); return false; }
    Platform::CloseFile(f);
    Savestate state(buf.data(), (u32)buf.size(), false);
    if (state.Error) return false;
    if (!Console->DoSavestate(&state) || state.Error) return false;
    return true;
}

/* copied from MelonDS gheovgos */
static void audioOutput(void* args)
{
    (void)args;

    constexpr int SamplesPerBuffer = 1024;
    constexpr int Channels = 2;
    constexpr int BufferCount = 2;

    constexpr int BufferBytes =
        SamplesPerBuffer * Channels * sizeof(s16);

    constexpr int MaxQueuedBytes = BufferBytes * BufferCount;

    s16 buffer[SamplesPerBuffer * Channels];

    // Start SDL playback.
    S.ResumeAudioStreamDevice(AudioStream);

    while (!quitFlag)
    {
        if (!ConsoleRunning || !Console)
        {
            svcSleepThread(10000000);
            continue;
        }

        // Maintain a maximum of two buffers queued.
        int queued = S.GetAudioStreamQueued(AudioStream);

        if (queued >= BufferBytes)
        {
            svcSleepThread(2000000);
            continue;
        }

        int nSamples = Console->SPU.ReadOutput(
            buffer,
            SamplesPerBuffer
        );

        if (nSamples <= 0)
        {
            svcSleepThread(1000000);
            continue;
        }

        // Pad incomplete buffers with the last stereo sample.
        if (nSamples < SamplesPerBuffer)
        {
            u32 last = ((u32*)buffer)[nSamples - 1];

            for (int i = nSamples; i < SamplesPerBuffer; i++)
                ((u32*)buffer)[i] = last;
        }

        // Apply volume.
        for (int i = 0; i < SamplesPerBuffer * Channels; i++)
        {
            int32_t sample =
                (int32_t)buffer[i] * Config::AudioVolume / 4;

            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;

            buffer[i] = (s16)sample;
        }

        // Submit the complete buffer.
        S.PutAudioStreamData(
            AudioStream,
            buffer,
            BufferBytes
        );
    }
}

// ---- core lifecycle ----
static bool loadROMIntoConsole()
{
    FILE* f = fopen(romPath.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 512 * 1024 * 1024) { fclose(f); return false; }
    auto data = std::make_unique<u8[]>(len);
    if (fread(data.get(), 1, len, f) != (size_t)len) { fclose(f); return false; }
    fclose(f);

    // load .sav alongside ROM if present
    std::unique_ptr<u8[]> savedata;
    u32 savelen = 0;
    FILE* sf = fopen(sramPath.c_str(), "rb");
    if (sf)
    {
        fseek(sf, 0, SEEK_END); long sl = ftell(sf); fseek(sf, 0, SEEK_SET);
        if (sl > 0 && sl < 64 * 1024 * 1024)
        {
            savedata = std::make_unique<u8[]>(sl);
            if (fread(savedata.get(), 1, sl, sf) == (size_t)sl) savelen = sl;
            else savedata.reset();
        }
        fclose(sf);
    }

    NDSCart::NDSCartArgs cartargs;
    cartargs.SRAM = std::move(savedata);
    cartargs.SRAMLength = savelen;
    saveUserdata = sramPath;
    auto cart = NDSCart::ParseROM(std::move(data), (u32)len, (void*)saveUserdata.c_str(), std::move(cartargs));
    if (!cart) return false;
    Console->SetNDSCart(std::move(cart));
    return true;
}

static std::unique_ptr<ARM9BIOSImage> loadARM9BIOS()
{
    FILE* f = fopen("sdmc:/switch/nekoDS/bios9.bin", "rb");
    if (f)
    {
        auto img = std::make_unique<ARM9BIOSImage>();
        if (fread(img->data(), 1, img->size(), f) == img->size()) { fclose(f); return img; }
        fclose(f);
    }
    return std::make_unique<ARM9BIOSImage>(FreeBIOSGetNtrArm9());
}

static std::unique_ptr<ARM7BIOSImage> loadARM7BIOS()
{
    FILE* f = fopen("sdmc:/switch/nekoDS/bios7.bin", "rb");
    if (f)
    {
        auto img = std::make_unique<ARM7BIOSImage>();
        if (fread(img->data(), 1, img->size(), f) == img->size()) { fclose(f); return img; }
        fclose(f);
    }
    return std::make_unique<ARM7BIOSImage>(FreeBIOSGetNtrArm7());
}

static Firmware loadFirmware()
{
    FILE* f = fopen("sdmc:/switch/nekoDS/firmware.bin", "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        if (len > 0 && len <= 1024 * 1024)
        {
            std::vector<u8> buf(len);
            if (fread(buf.data(), 1, len, f) == (size_t)len)
            {
                fclose(f);
                return Firmware(buf.data(), (u32)len);
            }
        }
        fclose(f);
    }
    return Firmware(0); // generated firmware
}

static bool startCore(bool reset)
{
    setScreenLayout();
    appletLockExit();
    appletHook(&cookie, onAppletHook, nullptr);

    if (reset)
    {
        sramPath = romPath.substr(0, romPath.rfind(".")) + ".sav";
        statePath = romPath.substr(0, romPath.rfind(".")) + ".mln";
        sramStatePath = statePath + ".sav";

        SwitchLog::Log("booting ROM: %s", romPath.c_str());

        NDSArgs args;
        args.ARM9BIOS = loadARM9BIOS();
        args.ARM7BIOS = loadARM7BIOS();
        args.Firmware = loadFirmware();
        args.OutputSampleRate = 48000.0;
        delete Console;
        Console = new NDS(std::move(args), nullptr);
        if (UseDeko)
        {
            // GPU::SetRenderer runs Init itself and falls back to the
            // software renderer if deko init fails (e.g. no shaders).
            Console->SetRenderer(std::make_unique<DekoRenderer>(*Console));
            UsingDeko = (dynamic_cast<DekoRenderer*>(&Console->GetRenderer()) != nullptr);
            if (UsingDeko)
                SwitchLog::Log("using deko3d GPU");
            else
                SwitchLog::Log("deko init failed, software fallback");
        }
        else
        {
            Console->SetRenderer(std::make_unique<SoftRenderer>(*Console));
            UsingDeko = false;
            SwitchLog::Log("using software renderer");
        }
        Console->Reset();
        if (!loadROMIntoConsole())
        {
            SwitchLog::Log("FAILED to load ROM: %s", romPath.c_str());
            messageScreen("nekoDS", {"Failed to load ROM:", romPath}, false);
            ConsoleRunning = false;
            delete Console; Console = nullptr;
            appletUnhook(&cookie);
            appletUnlockExit();
            return false;
        }
        SwitchLog::Log("ROM loaded");
        if (Config::DirectBoot || Console->NeedsDirectBoot())
            Console->SetupDirectBoot(romPath);
        Console->Start();
        ConsoleRunning = true;
    }

    quitFlag = false;
    if (Config::AudioVolume > 0 && AudioStream)
    {
        S.ResumeAudioStreamDevice(AudioStream);
        threadCreate(&audioThread, audioOutput, nullptr, nullptr, 0x8000, 0x30, 1);
        threadStart(&audioThread);
        audioStarted = true;
    }
    if (Config::SwitchOverclock > 0)
    {
        pcvInitialize();
        if (R_FAILED(pcvSetClockRate(PcvModule_CpuBus, clockSpeeds[Config::SwitchOverclock])))
        {
            clkrstInitialize();
            clkrstOpenSession(&cpuSession, PcvModuleId_CpuBus, 0);
            clkrstSetClockRate(&cpuSession, clockSpeeds[Config::SwitchOverclock]);
            ocUseClkrst = true;
        }
        ocStarted = true;
    }
    corePaused = false;
    return true;
}

static void pauseCore()
{
    if (corePaused) return;
    corePaused = true;
    quitFlag = true;
    if (audioStarted)
    {
        threadWaitForExit(&audioThread);
        threadClose(&audioThread);
        audioStarted = false;
    }
    if (AudioStream) S.PauseAudioStreamDevice(AudioStream);
    if (ocStarted)
    {
        if (R_FAILED(pcvSetClockRate(PcvModule_CpuBus, clockSpeeds[0])) && ocUseClkrst)
            clkrstSetClockRate(&cpuSession, clockSpeeds[0]);
        if (ocUseClkrst) { clkrstExit(); ocUseClkrst = false; }
        pcvExit();
        ocStarted = false;
    }
    appletUnhook(&cookie);
    appletUnlockExit();
}

// ---- menus (ported from old switch main.cpp) ----
static void controlsMenu()
{
    int selection = 0;
    while (true)
    {
        std::vector<std::string> subs;
        for (int i = 0; i < 15; i++)
        {
            if (Config::Mapping[i] == 0) subs.push_back("None");
            else
            {
                std::string s; int count = 0;
                for (size_t j = 0; j < 16; j++)
                    if (Config::Mapping[i] & controlBits[j])
                    {
                        count++;
                        if (count < 5) s += controlValues[j] + ", ";
                        else { s += "..."; break; }
                    }
                if (!s.empty() && count < 5) s = s.substr(0, s.size() - 2);
                subs.push_back(s);
            }
        }
        u64 pressed = menuScreen("Controls", "", "Clear", {}, controlNames, subs, &selection);
        if (pressed & HidNpadButton_A)
        {
            pressed = 0;
            while (pressed == 0)
            {
                messageScreen("Controls", {"Press a button for: " + controlNames[selection]}, false);
                padUpdate(&gPad);
                pressed = padGetButtonsDown(&gPad);
                svcSleepThread(16000000);
            }
            // store raw libnx button mask
            Config::Mapping[selection] = pressed;
        }
        else if (pressed & HidNpadButton_B) return;
        else if ((pressed & HidNpadButton_X) && !(pressed & TOUCH_FLAG)) Config::Mapping[selection] = 0;
    }
}

static void settingsMenu()
{
    int selection = 0;
    while (true)
    {
        std::vector<std::string> subs;
        for (size_t i = 0; i < settingNames.size(); i++)
            subs.push_back(settingValues[i].names[*settingValues[i].value]);
        u64 pressed = menuScreen("Settings", "", "Controls", {}, settingNames, subs, &selection);
        if (pressed & HidNpadButton_A)
        {
            (*settingValues[selection].value)++;
            if (*settingValues[selection].value >= (int)settingValues[selection].names.size())
                *settingValues[selection].value = 0;
        }
        else if (pressed & HidNpadButton_B) { Config::Save(); return; }
        else if (pressed & HidNpadButton_X) controlsMenu();
    }
}

static bool fileBrowser()
{
    int selection = 0;
    romPath = "sdmc:/";
    while (true)
    {
        std::vector<std::string> files = dirContents(romPath, ".nds");
        std::vector<Icon> icons;
        for (size_t i = 0; i < files.size(); i++)
        {
            if (files[i].find(".nds", files[i].length() > 4 ? files[i].length() - 4 : 0) != std::string::npos)
                icons.push_back({romIcon(romPath + "/" + files[i]), 32});
            else
                icons.push_back({folderIcon, 64});
        }
        u64 pressed = menuScreen("nekoDS", "Exit", "Settings", icons, files, {}, &selection);
        for (auto& ic : icons)
            if (ic.texture != (u32*)folderIcon && ic.texture != (u32*)fileIcon)
                delete[] ic.texture;
        if (pressed & HidNpadButton_A && !files.empty())
        {
            if (!romPath.empty() && romPath.back() == '/')
                romPath.pop_back();
            romPath += "/" + files[selection];
            selection = 0;
            if (romPath.find(".nds", romPath.length() - 4) != std::string::npos) break;
        }
        else if (pressed & HidNpadButton_B && romPath != "sdmc:/")
        {
            romPath = romPath.substr(0, romPath.rfind("/"));
            selection = 0;
        }
        else if (pressed & HidNpadButton_X) settingsMenu();
        else if (pressed & HidNpadButton_Plus) return false;
    }
    std::string folder = romPath.substr(0, romPath.rfind("/"));
    snprintf(Config::LastROMFolder, sizeof(Config::LastROMFolder), "%s", folder.c_str());
    return true;
}

static bool pauseMenu()
{
    pauseCore();
    int selection = 0;
    while (true)
    {
        u64 pressed = menuScreen("nekoDS", "", "", {}, pauseNames, {}, &selection);
        if (pressed & HidNpadButton_A)
        {
            if (selection == 0) break;
            else if (selection == 1 || selection == 2)
            {
                bool ok = (selection == 1) ? saveStateFile(statePath) : loadStateFile(statePath);
                (void)ok;
                if (Config::SavestateRelocSRAM)
                {
                    // relocate .sav next to state like the old port
                }
                break;
            }
            else if (selection == 3) settingsMenu();
            else
            {
                ConsoleRunning = false;
                if (Console) Console->GPU.Stop();
                delete Console; Console = nullptr;
                if (fileBrowser())
                {
                    hotkeyMask &= ~BIT(2);
                    if (startCore(true)) return true;
                }
                return false;
            }
        }
        else if (pressed & HidNpadButton_B) break;
    }
    hotkeyMask &= ~BIT(2);
    startCore(false);
    return true;
}

// ---- frame presentation ----
static void drawScreens()
{
    void* top = nullptr; void* bottom = nullptr;
    bool have = Console && ConsoleRunning && Console->GPU.GetFramebuffers(&top, &bottom);

    S.SetRenderDrawColor(Ren, 0, 0, 0, 255);
    S.RenderClear(Ren);

    if (have && top && bottom)
    {
        // SDL-dlink only uploads RGBA8888 correctly (direct memcpy); its
        // ARGB/ABGR/BGRA converters assume big-endian byte order.
        const void* upTop = top;
        const void* upBot = bottom;
        S.UpdateTexture(TexTop, nullptr, upTop, 256 * 4);
        S.UpdateTexture(TexBot, nullptr, upBot, 256 * 4);
        // Layout math matches the old port; rotation is baked into which
        // rect is top/bottom via setScreenLayout() swapping.
        SDL_FRect dTop = {topX, topY, topWidth, topHeight};
        SDL_FRect dBot = {botX, botY, botWidth, botHeight};
        S.RenderTexture(Ren, TexTop, nullptr, &dTop);
        S.RenderTexture(Ren, TexBot, nullptr, &dBot);
    }
    S.RenderPresent(Ren);
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    SwitchLog::Init();
    SwitchLog::Log("nekoDS boot");

    if (!SwitchSDL::LoadAtLaunch())
    {
        SwitchLog::Log("FATAL: dynamic lib load failed (need sdmc:/lib/{neko3d,sdl3}.dnro)");
        return 1;
    }
    SwitchLog::Log("dlink: neko3d + sdl3 loaded");

    // Bind the dk* dispatch table for the ported deko GPU. If neko3d
    // fails to bind, the emulator still works via the soft renderer.
    UseDeko = DekoLoadDispatch(SwitchSDL::GblNeko);
    if (!UseDeko)
        SwitchLog::Log("neko3d bind failed (%s), software fallback", dlerror());
    else
        SwitchLog::Log("deko dispatch bound");

    if (!S.Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        SwitchLog::Log("FATAL: SDL_Init: %s", S.GetError());
        return 1;
    }
    SwitchLog::Log("SDL_Init ok");

    // Adopt hbmenu's NWindow for the deko3d swapchain (SDL-dlink has no
    // EGL fallback: EGL support is compiled out, adopted NWindow required).
    SDL_PropertiesID props = S.CreateProperties();
    S.SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_SWITCH_NWINDOW_POINTER, nwindowGetDefault());
    S.SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 1280);
    S.SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 720);
    Win = S.CreateWindowWithProperties(props);
    S.DestroyProperties(props);
    if (!Win) { SwitchLog::Log("FATAL: window: %s", S.GetError()); return 1; }
    SwitchLog::Log("window ok");

    Ren = S.CreateRenderer(Win, "deko3d");
    if (!Ren) { SwitchLog::Log("FATAL: renderer: %s", S.GetError()); return 1; }
    SwitchLog::Log("deko3d renderer ok");

    TexTop = S.CreateTexture(Ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
    TexBot = S.CreateTexture(Ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
    if (!TexTop || !TexBot) { 
        SwitchLog::Log("FATAL: Couldnt create texture(s) with error %s", S.GetError());
        return 1; 
    }
    
    S.SetTextureScaleMode(TexTop, SDL_SCALEMODE_LINEAR);
    S.SetTextureScaleMode(TexBot, SDL_SCALEMODE_LINEAR);

    SDL_AudioSpec StreamConfiguration = {};
    StreamConfiguration.freq = 48000;
    StreamConfiguration.format = SDL_AUDIO_S16; 
    StreamConfiguration.channels = 2;
    AudioStream = S.OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &StreamConfiguration, nullptr, nullptr);

    Result romfsRc = romfsInit();
    SwitchLog::Log("romfs init: 0x%X", romfsRc);

    initUI(Win, Ren);
    Config::Load();
    SwitchLog::Log("config loaded, last ROM folder: %s", Config::LastROMFolder);

    mkdir("sdmc:/switch/nekoDS", 0777);

    // libnx sub-service init: the old hidScanInput() did this automatically.
    // We use padGetButtons*/hidGetTouchScreenStates directly, so init them.
    hidInitializeNpad();
    hidInitializeTouchScreen();
    // NOTE: padConfigureInput is required before init, otherwise no
    // controller style is attached and padUpdate reads nothing.
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&gPad);
    SwitchLog::Log("pad init ok");

    bool booted = false;
    while (!booted)
    {
        if (!fileBrowser()) goto done;
        SwitchLog::Log("browser selected: %s", romPath.c_str());
        booted = startCore(true);
    }
    SwitchLog::Log("entering main loop");

    while (appletMainLoop())
    {
        auto start = std::chrono::steady_clock::now();

        padUpdate(&gPad);
        u64 pressed = padGetButtonsDown(&gPad);
        u64 released = padGetButtonsUp(&gPad);
        u64 held = padGetButtons(&gPad);

        u32 keyMask = 0xFFF;
        for (int i = 0; i < 12; i++)
            if (held & Config::Mapping[i])
                keyMask &= ~(1u << i);
        if (Console && ConsoleRunning) Console->SetKeyMask(keyMask);

        for (int i = 12; i < 15; i++)
        {
            if (pressed & Config::Mapping[i]) hotkeyMask |= BIT(i - 12);
            else if (released & Config::Mapping[i]) hotkeyMask &= ~BIT(i - 12);
        }

        if (hotkeyMask & BIT(0))
        {
            lidClosed = !lidClosed;
            if (Console) Console->SetLidClosed(lidClosed);
            hotkeyMask &= ~BIT(0);
        }
        else if (hotkeyMask & BIT(2))
        {
            if (!pauseMenu()) break;
        }

        HidTouchScreenState ts;
        if (hidGetTouchScreenStates(&ts, 1) > 0 && ts.count > 0)
        {
            float tx = (float)ts.touches[0].x, ty = (float)ts.touches[0].y;
            if (tx > botX && tx < botX + botWidth && ty > botY && ty < botY + botHeight)
            {
                int x = 0, y = 0;
                if (Config::ScreenRotation == 0)      { x = (tx - botX) * 256 / botWidth; y = (ty - botY) * 192 / botHeight; }
                else if (Config::ScreenRotation == 1) { x = (ty - botY) * 256 / botHeight; y = 192 - (tx - botX) * 192 / botWidth; }
                else if (Config::ScreenRotation == 2) { x = 256 - (tx - botX) * 256 / botWidth; y = 192 - (ty - botY) * 192 / botHeight; }
                else                                  { x = 256 - (ty - botY) * 256 / botHeight; y = (tx - botX) * 192 / botWidth; }
                if (Console) Console->TouchScreen((u16)x, (u16)y);
            }
        }
        else if (Console) Console->ReleaseScreen();

        if (Console && ConsoleRunning)
        {
            if (frameNo == 0) SwitchLog::Log("first frame");
            try
            {
                for (int f = 0; f <= Frameskip; f++)
                    Console->RunFrame();
            }
            catch (const std::exception& e)
            {
                SwitchLog::Log("EXCEPTION in RunFrame: %s", e.what());
                break;
            }
            catch (...)
            {
                SwitchLog::Log("UNKNOWN EXCEPTION in RunFrame");
                break;
            }
            if (++frameNo % 600 == 0) SwitchLog::Log("emu frame %d", frameNo);
        }
        drawScreens();

        if (Config::LimitFPS)
        {
            std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            double remain = 1.0 / 60 - elapsed.count();
            if (remain > 0) S.Delay((Uint32)(remain * 1000));
        }
    }

    quitFlag = true;
    pauseCore();
    ConsoleRunning = false;
    if (Console) Console->GPU.Stop();
    delete Console; Console = nullptr;

done:
    deinitUI();
    if (AudioStream) S.DestroyAudioStream(AudioStream);
    if (TexTop) S.DestroyTexture(TexTop);
    if (TexBot) S.DestroyTexture(TexBot);
    if (Ren) S.DestroyRenderer(Ren);
    if (Win) S.DestroyWindow(Win);
    S.Quit();
    SwitchSDL::UnloadAll();
    return 0;
}
