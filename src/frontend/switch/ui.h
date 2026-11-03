// nekoDS Switch UI — ported from melonDS old switch port (ui.h).
// Rendering backend swapped from EGL/OpenGL to the dynamically loaded
// SDL3 deko3d renderer (sdl3.dnro). Menu flow (menuScreen/messageScreen)
// and theming are unchanged.

#ifndef NEKODS_SWITCH_UI_H
#define NEKODS_SWITCH_UI_H

#include <string>
#include <vector>
#include <switch.h>

#include "SwitchSDL.h"

typedef struct
{
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

#endif
