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

#include <string>
#include <vector>
#include <switch.h>

#include "SwitchSDL.h"

typedef struct {
    u32* texture;
    int size;
} Icon;

// Touch taps are reported as button A plus this out-of-band flag
// (pad buttons never use high bits).
static const u64 TOUCH_FLAG = (1ULL << 62);

extern PadState gPad;

extern ColorSetId systemTheme;
extern u32* fileIcon;
extern u32* folderIcon;

u32 rgbaToU32(u8 r, u8 g, u8 b, u8 a);

void initUI(SDL_Window* win, SDL_Renderer* ren);
void deinitUI();

void drawImage(u32* image, int imageWidth, int imageHeight, float x, float y, float width, float height);
void drawString(std::string str, float x, float y, int size, bool right, u32 color);
void drawLine(float x1, float y1, float x2, float y2, u32 color);
void setTextureFiltering(bool enabled);
void clearDisplay(u8 shade);
void refreshDisplay();

u64 menuScreen(std::string title, std::string actionPlus, std::string actionX,
               std::vector<Icon> icons, std::vector<std::string> items,
               std::vector<std::string> subitems, int* selection);
u64 messageScreen(std::string title, std::vector<std::string> text, bool exit);

std::vector<std::string> dirContents(std::string directory, std::string extension);