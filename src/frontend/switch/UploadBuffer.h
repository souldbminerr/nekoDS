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

#include "DekoAPI.h"
#include "GpuMemHeap.h"

namespace melonDS {
    class UploadBuffer {
        public:
            UploadBuffer();

            DkGpuAddr UploadData(dk::CmdBuf cmdbuf, u32 size, u8* data);
            void UploadAndCopyData(dk::CmdBuf cmdbuf, DkGpuAddr dst, u8* data, u32 size);
            void UploadAndCopyTexture(dk::CmdBuf cmdbuf, dk::Image& image, u8* data, u32 x, u32 y, u32 width, u32 height, u32 pitch, u32 layer = 0);

            u32 LastFlushBuffer = 0;
        private:
            static const u32 SegmentSize = 16*1024*1024;
            GpuMemHeap::Allocation Buffers[3];
            dk::Fence Fences[3] = {};
            u32 CurBuffer = 0, CurOffset = 0;
    };
} // namespace melonDS
