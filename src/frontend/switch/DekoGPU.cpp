// See DekoGPU.h.

#include "DekoGPU.h"

#include <cstdio>

#include "NDS.h"
#include "DekoContext.h"
#include "SwitchLog.h"

namespace melonDS
{

// Shader files the backends load (romfs:/shaders/<name>.dksh).
// Must stay in sync with the UAM build list in CMakeLists.txt.
static const char* kRequiredShaders[] = {
    "FullscreenQuad_vsh",
    "ComposeBGOBJ_fsh",
    "ComposeBGOBJDirectBitmapOnly_fsh",
    "ComposeBGOBJShowBitmap_fsh",
    "BGText4bpp_fsh",
    "BGText4bppMosaic_fsh",
    "BGText8bpp_fsh",
    "BGText8bppMosaic_fsh",
    "BGAffine_fsh",
    "BGAffineMosaic_fsh",
    "BGExtendedBitmap8bpp_fsh",
    "BGExtendedBitmap8bppMosaic_fsh",
    "BGExtendedBitmapDirect_fsh",
    "BGExtendedBitmapDirectMosaic_fsh",
    "BGExtendedMixed_fsh",
    "BGExtendedMixedMosaic_fsh",
    "OBJRegular_vsh",
    "OBJAffine_vsh",
    "OBJ4bpp_fsh",
    "OBJ8bpp_fsh",
    "OBJBitmap_fsh",
    "OBJWindow4bpp_fsh",
    "OBJWindow8bpp_fsh",
    "InterpXSpansZBuffer",
    "InterpXSpansWBuffer",
    "BinCombined",
    "DepthBlendZBuffer",
    "DepthBlendWBuffer",
    "RasteriseNoTextureZBuffer",
    "RasteriseNoTextureZBufferToon",
    "RasteriseNoTextureZBufferHighlight",
    "RasteriseUseTextureDecalZBuffer",
    "RasteriseUseTextureModulateZBuffer",
    "RasteriseUseTextureToonZBuffer",
    "RasteriseUseTextureHighlightZBuffer",
    "RasteriseShadowMaskZBuffer",
    "RasteriseNoTextureWBuffer",
    "RasteriseNoTextureWBufferToon",
    "RasteriseNoTextureWBufferHighlight",
    "RasteriseUseTextureDecalWBuffer",
    "RasteriseUseTextureModulateWBuffer",
    "RasteriseUseTextureToonWBuffer",
    "RasteriseUseTextureHighlightWBuffer",
    "RasteriseShadowMaskWBuffer",
    "ClearCoarseBinMask",
    "ClearIndirectWorkCount",
    "CalculateWorkOffsets",
    "SortWork",
    "FinalPass",
    "FinalPassEdge",
    "FinalPassFog",
    "FinalPassEdgeFog",
    "FinalPassAA",
    "FinalPassEdgeAA",
    "FinalPassFogAA",
    "FinalPassEdgeFogAA",
};

DekoRenderer::DekoRenderer(NDS& nds) : Renderer(nds.GPU)
{
}

DekoRenderer::~DekoRenderer()
{
}

bool DekoRenderer::Init()
{
    SwitchLog::Log("deko renderer init");
    if (!DekoCtx::Init())
    {
        SwitchLog::Log("deko renderer init: context FAILED");
        return false;
    }

    for (const char* name : kRequiredShaders)
    {
        char path[256];
        snprintf(path, sizeof(path), "romfs:/shaders/%s.dksh", name);
        FILE* f = fopen(path, "rb");
        if (!f)
        {
            SwitchLog::Log("deko init FAILED: missing shader %s", path);
            return false;
        }
        fclose(f);
    }

    Backend2D = std::make_unique<Deko2D::DekoBackend>(GPU);
    Backend3D = std::make_unique<Deko3D::DekoBackend>(GPU);
    Backend3D->Backend2D = Backend2D.get();

    if (!Backend3D->Init())
    {
        printf("nekoDS: Deko 3D backend init failed\n");
        Backend3D.reset();
        Backend2D.reset();
        return false;
    }

    return true;
}

void DekoRenderer::Reset()
{
    if (Backend2D) Backend2D->Reset();
    if (Backend3D) Backend3D->Reset();
}

void DekoRenderer::Stop()
{
    if (Backend3D) Backend3D->DeInit();
    Backend3D.reset();
    Backend2D.reset();
    DekoCtx::DeInit();
}

void DekoRenderer::DrawScanline(u32 line)
{
    if (!Backend2D) return;
    Backend2D->DrawScanline(line, GPU.GPU2D_A);
    Backend2D->DrawScanline(line, GPU.GPU2D_B);
}

void DekoRenderer::DrawSprites(u32 line)
{
    if (!Backend2D) return;
    Backend2D->DrawSprites(line, GPU.GPU2D_A);
    Backend2D->DrawSprites(line, GPU.GPU2D_B);
}

void DekoRenderer::Start3DRendering()
{
    if (Backend3D) Backend3D->RenderFrame();
    if (DekoCtx::QueueInErrorState())
        SwitchLog::Log("deko: queue in ERROR state right after 3D RenderFrame");
}

void DekoRenderer::Finish3DRendering()
{
    // The 3D backend submits compute work at VCount 215; the 2D compose of
    // the next frame samples _3DFramebuffer, so fence it complete here.
    // (The donor overlapped this with present fences; we serialize.)
    if (Backend2D) DekoCtx::EmuQueue.waitIdle();
    if (DekoCtx::QueueInErrorState())
        SwitchLog::Log("deko: queue in ERROR state at VBlank (after 3D waitIdle)");
}

bool DekoRenderer::GetFramebuffers(void** top, void** bottom)
{
    *top = CPUFramebuffers[0];
    *bottom = CPUFramebuffers[1];
    return true;
}

void DekoRenderer::SwapBuffers()
{
    if (Backend2D)
    {
        if (DekoCtx::QueueInErrorState())
            SwitchLog::Log("deko: queue ALREADY in error state at SwapBuffers (fault is pre-readback)");
        Backend2D->EndFrameAndReadback(CPUFramebuffers[0], CPUFramebuffers[1]);
    }
}

} // namespace melonDS
