// See DekoContext.h. Adapted from melonDS-new src/frontend/switch/Gfx.cpp
// (Init device/queue/heaps + LoadShader), minus swapchain/presentation,
// system-font and UI code which SDL3 handles in this port.

#include "DekoContext.h"
#include "SwitchLog.h"
#include "SwitchLog.h"

#include <cstdio>
#include <cstring>

using namespace melonDS;
namespace DekoCtx
{

dk::Device Device;
dk::Queue EmuQueue;
dk::CmdBuf EmuCmdBuf;

std::optional<GpuMemHeap> TextureHeap;
std::optional<GpuMemHeap> ShaderCodeHeap;
std::optional<GpuMemHeap> DataHeap;

static CmdMemRing<2>* ReadbackCmdMem = nullptr;
static GpuMemHeap::Allocation ReadbackStaging;

struct DkshHeader
{
    uint32_t magic; // DKSH_MAGIC
    uint32_t header_sz; // sizeof(DkshHeader)
    uint32_t control_sz;
    uint32_t code_sz;
    uint32_t programs_off;
    uint32_t num_programs;
};

static void DebugOutput(void* userData, const char* context, DkResult result, const char* message)
{
    (void)userData;
    SwitchLog::Log("deko %s: %s (%d)", context, message, result);
}

bool Init()
{
    // Idempotent: a previous renderer may still own the device (its Stop
    // runs at console teardown). Backends are per-renderer and re-created.
    if (Device)
        return true;

    SwitchLog::Log("deko: creating device");
    Device = dk::DeviceMaker{}.setCbDebug(DebugOutput).create();
    if (!Device)
    {
        SwitchLog::Log("deko: device create FAILED");
        return false;
    }

    EmuQueue = dk::QueueMaker{Device}
        .setFlags(DkQueueFlags_HighPrio | DkQueueFlags_Graphics | DkQueueFlags_Compute | DkQueueFlags_DisableZcull)
        .setCommandMemorySize(DK_QUEUE_MIN_CMDMEM_SIZE * 4)
        .setFlushThreshold(DK_QUEUE_MIN_CMDMEM_SIZE)
        .create();
    if (!EmuQueue)
    {
        Device.destroy();
        Device = nullptr;
        return false;
    }

    TextureHeap.emplace(Device, 1024*1024*120, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 1024*8);
    ShaderCodeHeap.emplace(Device, 1024*1024*12,
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 64);
    DataHeap.emplace(Device, 1024*1024*256, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 128);

    EmuCmdBuf = dk::CmdBufMaker{Device}.create();
    if (!EmuCmdBuf)
    {
        TextureHeap.reset();
        ShaderCodeHeap.reset();
        DataHeap.reset();
        EmuQueue.destroy();
        EmuQueue = nullptr;
        Device.destroy();
        Device = nullptr;
        return false;
    }

    ReadbackCmdMem = new CmdMemRing<2>(*DataHeap, 0x10000);
    ReadbackStaging = DataHeap->Alloc(256*192*4, DK_MEMBLOCK_ALIGNMENT);
    SwitchLog::Log("deko: context ready");
    return true;
}

void DeInit()
{
    if (!Device)
        return;
    if (EmuQueue)
        EmuQueue.waitIdle();

    if (DataHeap && ReadbackCmdMem)
    {
        DataHeap->Free(ReadbackStaging);
        delete ReadbackCmdMem;
        ReadbackCmdMem = nullptr;
    }

    if (EmuCmdBuf)
        EmuCmdBuf.destroy();

    TextureHeap.reset();
    ShaderCodeHeap.reset();
    DataHeap.reset();

    if (EmuQueue)
        EmuQueue.destroy();
    EmuQueue = nullptr;
    EmuCmdBuf = nullptr;
    Device.destroy();
    Device = nullptr;
}

bool LoadShader(const char* path, dk::Shader& out)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        SwitchLog::Log("missing shader %s", path);
        return false;
    }

    DkshHeader header;
    if (fread(&header, sizeof(DkshHeader), 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    rewind(f);
    u8* ctrlmem = new u8[header.control_sz];
    if (fread(ctrlmem, header.control_sz, 1, f) != 1)
    {
        delete[] ctrlmem;
        fclose(f);
        return false;
    }

    GpuMemHeap::Allocation data = ShaderCodeHeap->Alloc(header.code_sz, DK_SHADER_CODE_ALIGNMENT);
    if (fread(ShaderCodeHeap->CpuAddr<void>(data), header.code_sz, 1, f) != 1)
    {
        delete[] ctrlmem;
        fclose(f);
        return false;
    }
    fclose(f);

    dk::ShaderMaker{ShaderCodeHeap->MemBlock, data.Offset}
        .setControl(ctrlmem)
        .setProgramId(0)
        .initialize(out);

    delete[] ctrlmem;
    return true;
}

void ReadbackImage(dk::Image& image, void* dstCpu, u32 width, u32 height){
    ReadbackCmdMem->Begin(EmuCmdBuf);
    EmuCmdBuf.barrier(DkBarrier_Full, DkInvalidateFlags_Image);
    // same value convention as donor DoCapture (GPU2D_Deko.cpp)
    EmuCmdBuf.copyImageToBuffer(dk::ImageView{image}, {0, 0, 0, width, height, 1},
        {DataHeap->GpuAddr(ReadbackStaging)});
    EmuQueue.submitCommands(ReadbackCmdMem->End(EmuCmdBuf));
    EmuQueue.waitIdle();

    memcpy(dstCpu, DataHeap->CpuAddr<void>(ReadbackStaging), width * height * 4);
}

bool QueueInErrorState()
{
    return DekoDk.dkQueueIsInErrorState(EmuQueue) != 0;
}

} // namespace DekoCtx
