// DekoRenderer — nekoDS Renderer implementation backed by the ported
// melonDS-new deko3d GPU (GPU2D_Deko/GPU3D_Deko backends).
//
// The 2D/3D backends render entirely on the GPU into dk::Images owned by
// DekoCtx's device. SwapBuffers() copies both final screens to CPU RAM
// (BGRA u32, 256x192) so the SDL3 present path is unchanged from the
// software renderer. Like the donor, 3D runs at VCount 215 via
// Start3DRendering and is fenced complete in Finish3DRendering.

#ifndef NEKODS_DEKO_GPU_H
#define NEKODS_DEKO_GPU_H

#include "GPU.h"

#include <memory>

#include "GPU2D_Deko.h"
#include "GPU3D_Deko.h"

namespace melonDS
{

class DekoRenderer : public Renderer
{
public:
    explicit DekoRenderer(NDS& nds);
    ~DekoRenderer() override;

    bool Init() override;
    void Reset() override;
    void Stop() override;

    void SetRenderSettings(RendererSettings& settings) override {}

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;

    void Start3DRendering() override;
    void Finish3DRendering() override;
    void Restart3DRendering() override {}

    void VBlank() override {}
    void VBlankEnd() override {}

    void AllocCapture(u32 bank, u32 start, u32 len) override {}
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override {}

    bool GetFramebuffers(void** top, void** bottom) override;
    void SwapBuffers() override;

private:
    std::unique_ptr<Deko2D::DekoBackend> Backend2D;
    std::unique_ptr<Deko3D::DekoBackend> Backend3D;

    u32 CPUFramebuffers[2][256*192];
};

} // namespace melonDS

#endif
