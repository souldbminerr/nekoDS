// nekoDS Switch frontend — dynamic SDL3 binding.
// SDL3 lives in sdmc:/lib/sdl3.dnro (SDL-dlink) and is bound at launch
// via libnx-dlink dlopen/dlsym, mirroring qlaunch-ext loader + SDL smoke test.
// neko3d.dnro must be opened RTLD_GLOBAL first so SDL's deko3d renderer
// can resolve dk* from it.
#ifndef NEKODS_SWITCH_SDL_H
#define NEKODS_SWITCH_SDL_H

#include <switch.h>
#include <dlink/dlink.h>

#include "SwitchLog.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_pixels.h>

#include <cstdio>
#include <cstdlib>

// Linker-provided dynamic section anchor (global scope: the name must
// not be namespace-mangled).
extern char _DYNAMIC[];

namespace SwitchSDL
{

struct API
{
    // core
    bool (*Init)(SDL_InitFlags);
    void (*Quit)(void);
    const char* (*GetError)(void);
    void (*Delay)(Uint32);
    Uint64 (*GetTicks)(void);
    // properties
    SDL_PropertiesID (*CreateProperties)(void);
    bool (*SetPointerProperty)(SDL_PropertiesID, const char*, void*);
    bool (*SetNumberProperty)(SDL_PropertiesID, const char*, Sint64);
    void (*DestroyProperties)(SDL_PropertiesID);
    // window / renderer
    SDL_Window* (*CreateWindowWithProperties)(SDL_PropertiesID);
    void (*DestroyWindow)(SDL_Window*);
    SDL_Renderer* (*CreateRenderer)(SDL_Window*, const char*);
    void (*DestroyRenderer)(SDL_Renderer*);
    bool (*SetRenderDrawColor)(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8);
    bool (*RenderClear)(SDL_Renderer*);
    bool (*RenderPresent)(SDL_Renderer*);
    bool (*RenderFillRect)(SDL_Renderer*, const SDL_FRect*);
    bool (*RenderDebugText)(SDL_Renderer*, float, float, const char*);
    // texture
    SDL_Texture* (*CreateTexture)(SDL_Renderer*, SDL_PixelFormat, SDL_TextureAccess, int, int);
    void (*DestroyTexture)(SDL_Texture*);
    bool (*UpdateTexture)(SDL_Texture*, const SDL_Rect*, const void*, int);
    bool (*RenderTexture)(SDL_Renderer*, SDL_Texture*, const SDL_FRect*, const SDL_FRect*);
    bool (*SetTextureScaleMode)(SDL_Texture*, SDL_ScaleMode);
    // audio
    SDL_AudioStream* (*OpenAudioDeviceStream)(SDL_AudioDeviceID, const SDL_AudioSpec*, SDL_AudioStreamCallback, void*);
    bool (*PutAudioStreamData)(SDL_AudioStream*, const void*, int);
    int (*GetAudioStreamQueued)(SDL_AudioStream*);
    void (*PauseAudioStreamDevice)(SDL_AudioStream*);
    void (*ResumeAudioStreamDevice)(SDL_AudioStream*);
    void (*DestroyAudioStream)(SDL_AudioStream*);
};

inline void* GblNeko = nullptr;
inline void* GblSdl = nullptr;
inline API S = {};

#define NEKODS_SDL_BIND(h, field, sym)                                  \
    do {                                                                  \
        S.field = reinterpret_cast<decltype(S.field)>(dlsym((h), (sym))); \
        if (!S.field) {                                                   \
            printf("nekoDS: SDL bind fail %s: %s\n", (sym), dlerror());  \
            return false;                                                 \
        }                                                                 \
    } while (0)

inline bool BindAll(void* hsdl)
{
    NEKODS_SDL_BIND(hsdl, Init, "SDL_Init");
    NEKODS_SDL_BIND(hsdl, Quit, "SDL_Quit");
    NEKODS_SDL_BIND(hsdl, GetError, "SDL_GetError");
    NEKODS_SDL_BIND(hsdl, Delay, "SDL_Delay");
    NEKODS_SDL_BIND(hsdl, GetTicks, "SDL_GetTicks");
    NEKODS_SDL_BIND(hsdl, CreateProperties, "SDL_CreateProperties");
    NEKODS_SDL_BIND(hsdl, SetPointerProperty, "SDL_SetPointerProperty");
    NEKODS_SDL_BIND(hsdl, SetNumberProperty, "SDL_SetNumberProperty");
    NEKODS_SDL_BIND(hsdl, DestroyProperties, "SDL_DestroyProperties");
    NEKODS_SDL_BIND(hsdl, CreateWindowWithProperties, "SDL_CreateWindowWithProperties");
    NEKODS_SDL_BIND(hsdl, DestroyWindow, "SDL_DestroyWindow");
    NEKODS_SDL_BIND(hsdl, CreateRenderer, "SDL_CreateRenderer");
    NEKODS_SDL_BIND(hsdl, DestroyRenderer, "SDL_DestroyRenderer");
    NEKODS_SDL_BIND(hsdl, SetRenderDrawColor, "SDL_SetRenderDrawColor");
    NEKODS_SDL_BIND(hsdl, RenderClear, "SDL_RenderClear");
    NEKODS_SDL_BIND(hsdl, RenderPresent, "SDL_RenderPresent");
    NEKODS_SDL_BIND(hsdl, RenderFillRect, "SDL_RenderFillRect");
    NEKODS_SDL_BIND(hsdl, RenderDebugText, "SDL_RenderDebugText");
    NEKODS_SDL_BIND(hsdl, CreateTexture, "SDL_CreateTexture");
    NEKODS_SDL_BIND(hsdl, DestroyTexture, "SDL_DestroyTexture");
    NEKODS_SDL_BIND(hsdl, UpdateTexture, "SDL_UpdateTexture");
    NEKODS_SDL_BIND(hsdl, RenderTexture, "SDL_RenderTexture");
    NEKODS_SDL_BIND(hsdl, SetTextureScaleMode, "SDL_SetTextureScaleMode");
    NEKODS_SDL_BIND(hsdl, OpenAudioDeviceStream, "SDL_OpenAudioDeviceStream");
    NEKODS_SDL_BIND(hsdl, PutAudioStreamData, "SDL_PutAudioStreamData");
    NEKODS_SDL_BIND(hsdl, GetAudioStreamQueued, "SDL_GetAudioStreamQueued");
    NEKODS_SDL_BIND(hsdl, PauseAudioStreamDevice, "SDL_PauseAudioStreamDevice");
    NEKODS_SDL_BIND(hsdl, ResumeAudioStreamDevice, "SDL_ResumeAudioStreamDevice");
    NEKODS_SDL_BIND(hsdl, DestroyAudioStream, "SDL_DestroyAudioStream");
    return true;
}

// Expose every host dynamic symbol to dlink modules, bulk style
// (qlaunch-ext loader pattern). Needed in addition to
// dlink_register_host() for symbols the host links but keeps local.
inline Result ProvideAllHostSymbols()
{
    // _DYNAMIC is declared at global scope above (linker-provided).
    struct Dyn { int64_t tag; uint64_t val; };
    struct Sym { uint32_t name; uint8_t info; uint8_t other; uint16_t shndx; uint64_t value; uint64_t size; };

    uint8_t* probe = reinterpret_cast<uint8_t*>(&ProvideAllHostSymbols);
    MemoryInfo mi{};
    uint32_t pi = 0;
    Result rc = svcQueryMemory(&mi, &pi, reinterpret_cast<uint64_t>(probe));
    if (R_FAILED(rc)) return rc;
    uint64_t base = mi.addr;

    auto fix = [&](uint64_t v) { return v < base ? base + v : v; };
    uint32_t* hash = nullptr;
    const char* strtab = nullptr;
    Sym* symtab = nullptr;
    for (Dyn* d = reinterpret_cast<Dyn*>(_DYNAMIC); d->tag != 0; d++)
    {
        if (d->tag == 4) hash = reinterpret_cast<uint32_t*>(fix(d->val));
        else if (d->tag == 5) strtab = reinterpret_cast<const char*>(fix(d->val));
        else if (d->tag == 6) symtab = reinterpret_cast<Sym*>(fix(d->val));
    }
    if (!hash || !strtab || !symtab) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    uint32_t nchain = hash[1];
    const char** names = (const char**)malloc(nchain * sizeof(*names));
    void** addrs = (void**)malloc(nchain * sizeof(*addrs));
    if (!names || !addrs) { free(names); free(addrs); return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory); }
    uint32_t count = 0;
    for (uint32_t i = 1; i < nchain; i++)
    {
        Sym* s = &symtab[i];
        uint8_t bind = s->info >> 4, type = s->info & 0xF;
        if (s->shndx == 0 || (bind != 1 && bind != 2)) continue;
        if (type != 1 && type != 2) continue;
        const char* nm = strtab + s->name;
        if (!nm[0]) continue;
        names[count] = nm;
        addrs[count] = reinterpret_cast<void*>(fix(s->value));
        count++;
    }
    rc = dlink_provide_bulk(names, addrs, count);
    free(names); free(addrs);
    return rc;
}

// Full launch sequence: host symbols -> neko3d GLOBAL -> sdl3 GLOBAL -> bind.
inline bool LoadAtLaunch()
{
    dlink_set_library_dir(nullptr); // sdmc:/lib/
    if (R_FAILED(dlink_register_host()))
        printf("nekoDS: dlink_register_host failed\n");
    // TEMPORARY DIAGNOSIS: skip the bulk host-symbol provide to isolate the
    // 0x1ddf66 dlopen failure (smoke test does register_host only).
#if 0
    // best-effort bulk provide (qlaunch-ext); harmless if it fails
    ProvideAllHostSymbols();
#endif

    GblNeko = dlopen("neko3d", RTLD_GLOBAL);
    if (!GblNeko)
    {
        const char* err = dlerror();
        SwitchLog::Log("FATAL: dlopen neko3d failed (%s). need sdmc:/lib/neko3d.dnro", err ? err : "?");
        return false;
    }
    SwitchLog::Log("dlopen neko3d ok: %p", GblNeko);
    GblSdl = dlopen("sdl3", RTLD_GLOBAL);
    if (!GblSdl)
    {
        const char* err = dlerror();
        SwitchLog::Log("FATAL: dlopen sdl3 failed (%s). need sdmc:/lib/sdl3.dnro", err ? err : "?");
        return false;
    }
    SwitchLog::Log("dlopen sdl3 ok: %p", GblSdl);
    return BindAll(GblSdl);
}

inline void UnloadAll()
{
    if (GblSdl) { dlclose(GblSdl); GblSdl = nullptr; }
    if (GblNeko) { dlclose(GblNeko); GblNeko = nullptr; }
}

#undef NEKODS_SDL_BIND

} // namespace SwitchSDL

#endif
