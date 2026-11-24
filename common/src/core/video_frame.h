#pragma once

#include "plum.h"

#include <array>
#include <cstdint>

namespace plum {

// Which hardware layer produced a pixel. The pseudo-3D renderer pushes each one to a
// different depth instead of flattening them. Every console maps its own layers onto
// these: the back plane is the Mega Drive's plane B, the Game Boy's and the NES's
// background (the SNES's lowest background in use), the front plane what sits over it
// (the Mega Drive's plane A, the Game Boy's window, the SNES's higher backgrounds), then
// the sprites. Mars is the 32X's bitmap.
enum class Layer : uint8_t { Backdrop = 0, Back = 1, Front = 2, Window = 3, Sprite = 4, Mars = 5 };

// Shadow/highlight state of a pixel (the Mega Drive's VDP register 12 bit 3; the SNES's
// colour math darkens or brightens some pixels by halves).
enum class Shade : uint8_t { Normal = 0, Shadow = 1, Highlight = 2 };

// Trivial on purpose (no member initializers): Pixel{} is all zeros, the backdrop, and
// the frame's arrays of 300 000 of them stay cheap for the compiler (MSVC runs out of
// heap otherwise). Always write Pixel{} or Pixel{...}, never a bare `Pixel p;`.
struct Pixel {
    uint8_t color;  // palette * 16 + index; index 0 is transparent (the backdrop shows)
    Layer layer;
    bool priority;  // priority bit of the tile or sprite
    Shade shade;
    uint16_t source;  // tile number or sprite number
};

// Everything the picture processor produced for one frame. Not just a picture: for
// every pixel it also records the layer it came from, and it keeps the layers as they
// were before they were mixed (alpha 0 where a layer is empty), so a renderer can pull
// them apart without holes. Colours are already resolved with the palette in effect on
// that line, so mid-frame palette changes survive in 3D too.
//
// The two planes are called B (the back one) and A (the front one), as the renderer
// stands them apart.
struct VideoFrame {
    // Most columns the widescreen option adds on one side: a little past 16:9. Much
    // larger frames make MSVC run out of heap space compiling this struct.
    static constexpr int kMaxWideSide = 56;
    // 512 (the SNES's high resolution) plus what the widescreen option adds on both sides.
    // The Mega Drive's pictures are narrower (kMdMaxWidth in plumsg), the row stride the same.
    static constexpr int kMaxWidth = 512 + 2 * kMaxWideSide;
    // 239 lines doubled in the SNES's interlace, 240 in the Mega Drive's interlace mode 2.
    static constexpr int kMaxHeight = 480;
    static constexpr int kPixels = kMaxWidth * kMaxHeight;

    int width = 256;  // the Mega Drive's 256 (H32) or 320 (H40), plus the widescreen columns
    // Columns the widescreen option adds on the left and on the right: picture the console
    // never shows, drawn from the planes and sprites past its edges. 0 without the option.
    int wideLeft = 0, wideRight = 0;
    int height = 224;  // the Mega Drive's 224 or 240; 448 or 480 in interlace mode 2
    bool interlaced = false;
    // How many lines make the height of a 4:3 picture: a TV's 224 for the consoles on
    // one, 144 for the Game Boy's screen, which shows all of its picture.
    int aspectLines = 224;
    // The console's own screen is as wide as tall times this (the Game Boy's 160x144
    // is 10:9), 0 for a TV picture at 4:3.
    float screenAspect = 0;
    // The console has a single background (the Game Boy, the NES), drawn as plane B. The
    // pseudo-3D view then raises its solid tiles to plane A, as the playfield, so they
    // get the voxels' extrusion like a Mega Drive's plane A does.
    bool oneBackground = false;

    // Row stride is always kMaxWidth.
    std::array<uint32_t, kPixels> rgba{};  // final picture, 0xAARRGGBB
    std::array<Pixel, kPixels> info{};     // where each final pixel came from

    std::array<uint32_t, kPixels> planeB{};
    std::array<uint32_t, kPixels> planeA{};  // includes the window
    std::array<uint32_t, kPixels> sprites{};
    std::array<Pixel, kPixels> planeBInfo{};
    std::array<Pixel, kPixels> planeAInfo{};
    std::array<Pixel, kPixels> spriteInfo{};
    std::array<uint32_t, kMaxHeight> backdrop{};  // backdrop colour of each line
    // The 32X's picture of each line (alpha 0 where it draws nothing), whether or not
    // it is in front of the Mega Drive's. Indexed by line, not by interlaced row. Only
    // plumsg fills it.
    std::array<uint32_t, kPixels> mars{};

    struct Line {
        int16_t hscrollA = 0, hscrollB = 0;
        bool displayEnabled = false;
    };
    std::array<Line, kMaxHeight> lines{};

    uint64_t frameNumber = 0;

    // Width of the console's own picture, without the widescreen columns.
    int consoleWidth() const { return width - wideLeft - wideRight; }

    uint32_t pixel(int x, int y) const { return rgba[y * kMaxWidth + x]; }
};

// The columns to add on each side of a 256-pixel 4:3 picture (plumnin's consoles) to make it w:h wide
// (16:9 is 43, 16:10 26), within kMaxWideSide.
inline int wideColumnsFor(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    const double total = 256.0 * 3.0 * w / (4.0 * h);
    const int side = static_cast<int>((total - 256.0) / 2.0 + 0.5);
    return side < 0 ? 0 : side > VideoFrame::kMaxWideSide ? VideoFrame::kMaxWideSide : side;
}

}  // namespace plum
