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

#include "UploadBuffer.h"
#include "DekoContext.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>


namespace melonDS {
    UploadBuffer::UploadBuffer() {
        for (u32 i = 0; i < 3; i++)
            Buffers[i] = DekoCtx::DataHeap->Alloc(SegmentSize, 64);
    }

    DkGpuAddr UploadBuffer::UploadData(dk::CmdBuf cmdbuf, u32 size, u8* data) {
        if (CurOffset + size > SegmentSize) {
            cmdbuf.signalFence(Fences[CurBuffer]);
            CurOffset = 0;

            CurBuffer++;
            if (CurBuffer == 3)
                CurBuffer = 0;
            
            LastFlushBuffer++;

            assert(LastFlushBuffer <= 3);
            Fences[CurBuffer].wait();
        }

        u8* srcCpu = DekoCtx::DataHeap->CpuAddr<u8>(Buffers[CurBuffer]) + CurOffset;
        DkGpuAddr srcGpu = DekoCtx::DataHeap->GpuAddr(Buffers[CurBuffer]) + CurOffset;
        // zero copy?
        memcpy(srcCpu, data, size);

        CurOffset += size + 63;
        CurOffset &= ~63;
        return srcGpu;
    }

    void UploadBuffer::UploadAndCopyData(dk::CmdBuf cmdbuf, DkGpuAddr dst, u8* data, u32 size) {
        cmdbuf.copyBuffer(UploadData(cmdbuf, size, data), dst, size);
    }

    void UploadBuffer::UploadAndCopyTexture(dk::CmdBuf cmdbuf, dk::Image& image,
        u8* data,
        u32 x, u32 y, u32 width, u32 height, u32 pitch, u32 layer) {

        DkCopyBuf src{ UploadData(cmdbuf, pitch*height, data) };
        dk::ImageView dst{ image };
        
        cmdbuf.copyBufferToImage(src, dst, { 0, 0, layer, width, height, 1 });
    }
} // namespace melonDS
