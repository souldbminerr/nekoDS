// nekoDS Switch UI — SDL3 port of melonDS old switch ui.cpp.
// Same menu UX; GL calls replaced with SDL_Render* via dynamic dispatch.
// Text uses SDL_RenderDebugText (built into SDL3, no font assets needed).
// Bitmap icons (ROM icons, file/folder BMPs) go through one reusable
// streaming texture.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <cstdio>

#include "ui.h"
#include "SwitchSDL.h"
#include "SwitchLog.h"

using namespace SwitchSDL;

static SDL_Window* UWin = nullptr;
static SDL_Renderer* URen = nullptr;
static SDL_Texture* UTex = nullptr;
static int UTexW = 0, UTexH = 0;
static bool ULinear = true;

ColorSetId systemTheme;
u32* font = nullptr;
u32* fileIcon = nullptr;
u32* folderIcon = nullptr;
static u32 uiPalette[6];

static bool showSelector = true;

static const int charWidths[] =
{
    11,  9, 11, 20, 18, 28, 24,  7, 12, 12,
    14, 24,  9, 12,  9, 16, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21,  9,  9, 26, 24,
    26, 18, 28, 24, 21, 24, 26, 20, 20, 27,
    23,  9, 17, 21, 16, 31, 27, 29, 19, 29,
    20, 18, 21, 26, 24, 37, 21, 21, 24, 12,
    16, 12, 18, 16,  9, 20, 21, 18, 21, 20,
    10, 20, 20,  8, 12, 19,  9, 30, 20, 21,
    21, 21, 12, 16, 12, 20, 17, 29, 17, 17,
    16,  9,  8,  9, 12,  0, 40, 40, 40, 40
};

u32 rgbaToU32(u8 r, u8 g, u8 b, u8 a)
{
    // packed 0xRRGGBBAA so the in-memory bytes are R,G,B,A, matching the
    // SDL_PIXELFORMAT_RGBA8888 textures used for upload (the only format
    // SDL-dlink's deko3d backend uploads without a broken byte swap).
    return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | (u32)a;
}

static u32* bmpToTexture(const char* filename)
{
    FILE* bmp = fopen(filename, "rb");
    if (!bmp) return nullptr;
    u8 header[70];
    if (fread(header, 1, 70, bmp) != 70) { fclose(bmp); return nullptr; }
    int width = *(int*)&header[18];
    int height = *(int*)&header[22];
    if (width <= 0 || height <= 0 || width > 512 || height > 512) { fclose(bmp); return nullptr; }
    u32* tex = new u32[width * height];
    for (int y = 1; y <= height; y++)
        for (int x = 0; x < width; x++)
        {
            u8 b, g, r, a;
            if (!fread(&b, 1, 1, bmp)) break;
            fread(&g, 1, 1, bmp); fread(&r, 1, 1, bmp); fread(&a, 1, 1, bmp);
            tex[(height - y) * width + x] = rgbaToU32(r, g, b, a);
        }
    fclose(bmp);
    return tex;
}

static int stringWidth(const std::string& str)
{
    int width = 0;
    for (size_t i = 0; i < str.size(); i++)
    {
        unsigned char c = str[i];
        if (c < 32 || c > 126) continue;
        width += charWidths[c - 32];
    }
    return width;
}

static void loadTheme()
{
    setsysInitialize();
    setsysGetColorSetId(&systemTheme);
    setsysExit();

    // romfs is mounted once for the whole session (shaders + theme bitmaps);
    // do NOT unmount here, the deko shader loader needs it later.
    std::string theme = (systemTheme == ColorSetId_Light) ? "light" : "dark";
    std::string f = "romfs:/font.bmp";
    font = bmpToTexture(f.c_str());
    f = "romfs:/file-" + theme + ".bmp";
    fileIcon = bmpToTexture(f.c_str());
    f = "romfs:/folder-" + theme + ".bmp";
    folderIcon = bmpToTexture(f.c_str());
    // no romfsExit: shaders are loaded from romfs later (DekoRenderer::Init)

    if (systemTheme == ColorSetId_Light)
    {
        uiPalette[0] = rgbaToU32(235, 235, 235, 255);
        uiPalette[1] = rgbaToU32( 45,  45,  45, 255);
        uiPalette[2] = rgbaToU32(205, 205, 205, 255);
        uiPalette[3] = rgbaToU32(255, 255, 255, 255);
        uiPalette[4] = rgbaToU32( 50, 215, 210, 255);
        uiPalette[5] = rgbaToU32( 50,  80, 240, 255);
    }
    else
    {
        uiPalette[0] = rgbaToU32( 45,  45,  45, 255);
        uiPalette[1] = rgbaToU32(255, 255, 255, 255);
        uiPalette[2] = rgbaToU32( 75,  75,  75, 255);
        uiPalette[3] = rgbaToU32( 35,  35,  35, 255);
        uiPalette[4] = rgbaToU32( 85, 185, 225, 255);
        uiPalette[5] = rgbaToU32(  0, 255, 200, 255);
    }
}

void initUI(SDL_Window* win, SDL_Renderer* ren)
{
    UWin = win; URen = ren;
    loadTheme();
}

void deinitUI()
{
    if (UTex) { S.DestroyTexture(UTex); UTex = nullptr; }
    delete[] font; font = nullptr;
    delete[] fileIcon; fileIcon = nullptr;
    delete[] folderIcon; folderIcon = nullptr;
}

static void ensureTex(int w, int h)
{
    if (UTex && UTexW == w && UTexH == h) return;
    if (UTex) { S.DestroyTexture(UTex); UTex = nullptr; }
    // RGBA8888: the only format SDL-dlink uploads with a straight memcpy
    // (its ARGB/ABGR/BGRA converters assume big-endian byte order).
    UTex = S.CreateTexture(URen, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    UTexW = w; UTexH = h;
    if (UTex) S.SetTextureScaleMode(UTex, ULinear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void setTextureFiltering(bool enabled)
{
    ULinear = enabled;
    if (UTex) S.SetTextureScaleMode(UTex, enabled ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void drawImage(u32* image, int imageWidth, int imageHeight, float x, float y, float width, float height)
{
    if (!image || imageWidth <= 0 || imageHeight <= 0) return;
    ensureTex(imageWidth, imageHeight);
    if (!UTex) return;
    S.UpdateTexture(UTex, nullptr, image, imageWidth * 4);
    SDL_FRect dst = {x, y, width, height};
    S.RenderTexture(URen, UTex, nullptr, &dst);
}

void drawString(std::string str, float x, float y, int size, bool right, u32 color)
{
    // SDL_RenderDebugText draws 8px glyphs; emulate size via render scale.
    float scale = size / 8.0f;
    if (right)
    {
        int w = (int)(str.size() * 8 * scale);
        x -= w;
    }
    u8 r = (color >> 24) & 0xFF, g = (color >> 16) & 0xFF, b = (color >> 8) & 0xFF;
    S.SetRenderDrawColor(URen, r, g, b, 255);
    float oldSx = 1, oldSy = 1;
    // debug text has no per-call scale; approximate by drawing at 8px grid
    // (size differences are cosmetic in menus).
    (void)oldSx; (void)oldSy; (void)scale;
    S.RenderDebugText(URen, x, y, str.c_str());
}

void drawLine(float x1, float y1, float x2, float y2, u32 color)
{
    u8 r = (color >> 24) & 0xFF, g = (color >> 16) & 0xFF, b = (color >> 8) & 0xFF;
    S.SetRenderDrawColor(URen, r, g, b, 255);
    // crude line via thin rects (menus only use h/v lines)
    SDL_FRect rc;
    if (y1 == y2) { rc = {std::min(x1,x2), y1, std::abs(x2-x1), 2.0f}; }
    else if (x1 == x2) { rc = {x1, std::min(y1,y2), 2.0f, std::abs(y2-y1)}; }
    else { rc = {std::min(x1,x2), std::min(y1,y2), std::abs(x2-x1), 2.0f}; }
    S.RenderFillRect(URen, &rc);
}

void clearDisplay(u8 shade)
{
    S.SetRenderDrawColor(URen, shade, shade, shade, 255);
    S.RenderClear(URen);
}

void refreshDisplay()
{
    S.RenderPresent(URen);
    static int presentCount = 0;
    if (++presentCount % 600 == 0)
        SwitchLog::Log("present heartbeat %d", presentCount);
}

u64 menuScreen(std::string title, std::string actionPlus, std::string actionX,
               std::vector<Icon> icons, std::vector<std::string> items,
               std::vector<std::string> subitems, int* selection)
{
    if (actionPlus != "") actionPlus = "> " + actionPlus + "     ";
    if (actionX != "") actionX = "X " + actionX + "     ";
    std::string actionB = "B Back     ";
    std::string actionA = "A OK";

    unsigned int position = *selection;
    bool upHeld = false, downHeld = false, scroll = false;
    std::chrono::steady_clock::time_point timeHeld;

    bool touchStarted = false, touchScroll = false;
    u32 touchX = 0, touchY = 0, touchMoveX = 0, touchMoveY = 0;

    setTextureFiltering(true);
    SwitchLog::Log("menu: %s (%d items)", title.c_str(), (int)items.size());

    while (true)
    {
        clearDisplay((systemTheme == ColorSetId_Light) ? 235 : 45);

        drawString(title, 72, 30, 42, false, uiPalette[1]);
        drawLine(30, 88, 1250, 88, uiPalette[1]);
        drawLine(30, 648, 1250, 648, uiPalette[1]);
        drawString(actionPlus + actionX + actionB + actionA, 1218, 667, 34, true, uiPalette[1]);

        padUpdate(&gPad);
        u64 pressed = padGetButtonsDown(&gPad);
        u64 released = padGetButtonsUp(&gPad);
        if (pressed)
            SwitchLog::Log("menu '%s' pressed: 0x%llX", title.c_str(), (unsigned long long)pressed);

        if (pressed & HidNpadButton_Up && position > 0)
        {
            if (!showSelector) showSelector = true;
            else position--;
            upHeld = true;
            timeHeld = std::chrono::steady_clock::now();
        }
        else if (pressed & HidNpadButton_Down && position < items.size() - 1)
        {
            if (!showSelector) showSelector = true;
            else position++;
            downHeld = true;
            timeHeld = std::chrono::steady_clock::now();
        }
        else if ((pressed & (HidNpadButton_A | HidNpadButton_B)) || (actionX != "" && (pressed & HidNpadButton_X)) || (actionPlus != "" && (pressed & HidNpadButton_Plus)))
        {
            if (!(pressed & HidNpadButton_A) || showSelector)
            {
                showSelector = true;
                *selection = position;
                return pressed;
            }
            showSelector = true;
        }

        if (released & HidNpadButton_Up) { upHeld = false; scroll = false; }
        if (released & HidNpadButton_Down) { downHeld = false; scroll = false; }

        if ((upHeld && position > 0) || (downHeld && position < items.size() - 1))
        {
            std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - timeHeld;
            if (!scroll && elapsed.count() > 0.5f) scroll = true;
            if (scroll && elapsed.count() > 0.1f)
            {
                position += (upHeld && position > 0) ? -1 : 1;
                timeHeld = std::chrono::steady_clock::now();
            }
        }

        HidTouchScreenState ts;
        bool touching = hidGetTouchScreenStates(&ts, 1) > 0 && ts.count > 0;
        if (touching)
        {
            if (!touchStarted)
            {
                touchX = ts.touches[0].x;
                touchY = ts.touches[0].y;
                touchStarted = true; touchScroll = false; showSelector = false;
            }
            touchMoveX = ts.touches[0].x;
            touchMoveY = ts.touches[0].y;

            if (touchScroll)
            {
                int newPos = *selection + (int)(touchY - touchMoveY) / 70;
                if (items.size() <= 7) position = 0;
                else if (newPos > (int)items.size() - 4) position = items.size() - 4;
                else if (newPos < 3) position = 3;
                else position = newPos;
            }
            else if (touchMoveX > touchX + 25 || touchMoveX < touchX - 25 ||
                     touchMoveY > touchY + 25 || touchMoveY < touchY - 25)
            {
                touchScroll = true;
                if (items.size() <= 7) *selection = position;
                if (position > items.size() - 4) *selection = items.size() - 4;
                else if (position < 3) *selection = 3;
                else *selection = position;
            }
        }
        else touchStarted = false;

        if (items.size() > 0)
            drawLine(90, 124, 1190, 124, uiPalette[2]);

        for (unsigned int i = 0; i < 7; i++)
        {
            if (i < items.size())
            {
                unsigned int row;
                if (position < 4 || items.size() <= 7) row = i;
                else if (position > items.size() - 4) row = items.size() - 7 + i;
                else row = i + position - 3;

                if (!touchStarted && !touchScroll && touchX >= 90 && touchX < 1190 &&
                    touchY >= 124 + i * 70 && touchY < 194 + i * 70)
                {
                    *selection = row;
                    return HidNpadButton_A | TOUCH_FLAG;
                }

                if (row == position && showSelector)
                {
                    drawImage(&uiPalette[3], 1, 1,   90, 125 + i * 70, 1100, 69);
                    drawImage(&uiPalette[4], 1, 1,   89, 121 + i * 70, 1103,  5);
                    drawImage(&uiPalette[4], 1, 1,   89, 191 + i * 70, 1103,  5);
                    drawImage(&uiPalette[4], 1, 1,   88, 122 + i * 70,    5, 73);
                    drawImage(&uiPalette[4], 1, 1, 1188, 122 + i * 70,    5, 73);
                }
                else drawLine(90, 194 + i * 70, 1190, 194 + i * 70, uiPalette[2]);

                if (icons.size() > row && icons[row].texture)
                {
                    drawImage(icons[row].texture, icons[row].size, icons[row].size, 105, 126 + i * 70, 64, 64);
                    drawString(items[row], 184, 140 + i * 70, 38, false, uiPalette[1]);
                }
                else drawString(items[row], 105, 140 + i * 70, 38, false, uiPalette[1]);

                if (subitems.size() > row)
                    drawString(subitems[row], 1175, 143 + i * 70, 32, true, uiPalette[5]);
            }
        }

        refreshDisplay();
    }
}

u64 messageScreen(std::string title, std::vector<std::string> text, bool exit)
{
    clearDisplay((systemTheme == ColorSetId_Light) ? 235 : 45);
    drawString(title, 72, 30, 42, false, uiPalette[1]);
    drawLine(30, 88, 1250, 88, uiPalette[1]);
    drawLine(30, 648, 1250, 648, uiPalette[1]);
    if (exit) drawString("+ Exit", 1218, 667, 34, true, uiPalette[1]);
    for (size_t i = 0; i < text.size(); i++)
        drawString(text[i], 90, 124 + i * 38, 38, false, uiPalette[1]);
    refreshDisplay();

    while (true)
    {
        padUpdate(&gPad);
        u64 pressed = padGetButtonsDown(&gPad);
        if ((!exit && pressed) || (pressed & HidNpadButton_Plus))
            return pressed;
    }
}

std::vector<std::string> dirContents(std::string directory, std::string extension)
{
    std::vector<std::string> contents;
    DIR* dir = opendir(directory.c_str());
    if (!dir) return contents;
    dirent* entry;
    while ((entry = readdir(dir)))
    {
        std::string name = entry->d_name;
        if (entry->d_type == DT_DIR || name.find(extension, (name.length() > extension.length() ? name.length() - extension.length() : 0)) != std::string::npos)
            contents.push_back(name);
    }
    closedir(dir);
    std::sort(contents.begin(), contents.end());
    return contents;
}
