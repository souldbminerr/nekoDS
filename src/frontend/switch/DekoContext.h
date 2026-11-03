// DekoContext — minimal deko3d host context for the ported Deko GPU.
//
// Replaces the subset of melonDS-new's frontend Gfx the renderers need:
// device, emu queue/cmdbuf, texture/shader-code/data heaps, offline-UAM
// shader loading and CPU readback of the final framebuffers.
//
// Unlike melonDS-new this creates NO swapchain/presentation queue —
// presentation stays with SDL3's deko3d renderer (sdl3.dnro owns its own
// device). All dk* calls route through the dynamic dispatch table
// (DekoAPI.h) bound to neko3d.dnro at launch.

#ifndef NEKODS_DEKO_CONTEXT_H
#define NEKODS_DEKO_CONTEXT_H

#include <optional>

#include "DekoAPI.h"
#include "GpuMemHeap.h"
#include "CmdMemRing.h"

namespace DekoCtx
{

extern dk::Device Device;
extern dk::Queue EmuQueue;
extern dk::CmdBuf EmuCmdBuf;

extern std::optional<melonDS::GpuMemHeap> TextureHeap;
extern std::optional<melonDS::GpuMemHeap> ShaderCodeHeap;
extern std::optional<melonDS::GpuMemHeap> DataHeap;

// Creates device, queue, cmdbuf and heaps. Shaders are NOT loaded here.
bool Init();
// Waits for the queue to idle, frees heaps, destroys queue/device.
void DeInit();

// Loads a UAM-built .dksh (control+code sections) into shader-code heap.
// Returns false (no assert) when the file is missing or invalid.
bool LoadShader(const char* path, dk::Shader& out);

// CPU-visible copy of a WxH RGBA8 image. Blocks until the GPU is idle.
void ReadbackImage(dk::Image& image, void* dstCpu, u32 width, u32 height);

// True if the emu queue entered error state (GPU fault). Used to localize
// faults between the 2D compose, 3D dispatches and the readback copy.
bool QueueInErrorState();

} // namespace DekoCtx

#endif
