#include "render/box_shelf.h"

#include "render/box_shelf_draw.h"

#include <algorithm>
#include <cmath>

// plumnin's part of the 3D shelf (render/box_shelf.h): the Game Boy's, the Game Boy Color's,
// the NES's and the Super Nintendo's boxes, cartridges and consoles.

namespace plum {

using namespace shelf;
using namespace plumnin;  // its consoles: ConsoleLayout, consoleModelOf...

namespace {

constexpr float kCard = 0.006f;      // the cardboard's thickness
constexpr float kFlapOpen = 115.0f;  // degrees, the end flap all the way open

}  // namespace

namespace {

// The models that come with plumnin (assets/models, made by tools/models/prepare.py), or the
// user's own of the same consoles: their real width, how they are turned to face +z, and
// the console's own bounds in the file when it has more (the NES's pad). Where their slots,
// screens and lights are, once stood on the floor, is in ConsoleLayout, the same as the
// built-in models'.
struct ModelFile {
    ConsoleModel model;
    const char* file;
    float width, turn;  // 13.5 cm = 1
    const float* box;
};
// The NES by RadioactiveAG, CC BY 4.0: front to -x, its pad and cable beside it.
const float kNesBox[6] = {1.4406f, 0.0033f, -0.031f, 4.5931f, 1.2683f, 4.0864f};
const ModelFile kModelFiles[] = {
    // The Game Boy by lokeig (CC BY-NC 4.0), the Game Boy Color by Wikiti (CC BY 4.0) and the
    // Super Nintendo by quick_loop (CC BY 4.0), all of them already facing +z.
    {ConsoleModel::GameBoy, "gameboy.glb", 90.0f / 135.0f, 0.0f, nullptr},
    {ConsoleModel::GameBoyColor, "gameboycolor.glb", 78.0f / 135.0f, 0.0f, nullptr},
    {ConsoleModel::Nes, "nes.glb", 254.0f / 135.0f, 90.0f, kNesBox},
    {ConsoleModel::Snes, "snes.glb", 200.0f / 135.0f, 0.0f, nullptr},
};

// Each power light: where it is (ConsoleLayout), and how it is tilted towards the front.
struct Led {
    const float* at;
    float tilt;
};
Led ledOf(ConsoleModel m) {
    using L = ConsoleLayout;
    switch (m) {
        case ConsoleModel::GameBoyColor: return {L::kGbcLed, 90.0f};
        case ConsoleModel::Nes: return {L::kNesLed, 90.0f};
        case ConsoleModel::Snes: return {L::kSnesLed, 25.0f};  // on the slope of its front
        default: return {L::kGbLed, 90.0f};
    }
}

// The cartridges' models: how to turn them to stand with the label to the front, where
// their texture has the label (which a scan replaces) and the picture in it (which a
// cover replaces), the label's quarter turns in the texture and the picture's shape.
struct CartridgeFile {
    ConsoleSystem system;
    const char* file;
    float turnX, turnZ;
    float label[4], picture[4];
    int labelTurn;
    float pictureAspect;  // width / height
    float pictureMiddle;  // how far down the cover the part shown on it is centred
    float turnY = 0;      // first, to straighten a model that comes turned in its file
    int labelImage = -1;  // the texture with the label, when it has more
};
const CartridgeFile kCartridgeFiles[] = {
    // The Game Boy's, from lokeig's model: the one in the slot (standing, its label to the
    // back), with the look of the Tetris one beside it. Its label fills the lower part.
    {ConsoleSystem::GameBoy, "gameboy-cartridge.glb", 0, 0, {0.005f, 0.532f, 0.325f, 0.84f}, {0.005f, 0.532f, 0.325f, 0.84f}, 0,
     1.04f, 0.45f, 180.0f},
    // A Game Boy Color game's, the same in black (the Color's model has only the top of one).
    {ConsoleSystem::GameBoyColor, "gameboycolor-cartridge.glb", 0, 0, {0.005f, 0.532f, 0.325f, 0.84f}, {0.005f, 0.532f, 0.325f, 0.84f},
     0, 1.04f, 0.45f, 180.0f},
    // The NES's, from RadioactiveAG's: lying on its back, its top end to -z. Its label (Punch-
    // Out!!'s) is a texture of its own, the third the model reads (after the shell's normal
    // map and colour), all of it.
    {ConsoleSystem::Nes, "nes-cartridge.glb", 90, 0, {0, 0, 1, 1}, {0, 0, 1, 1}, 0, 0.61f, 0.5f, 0.0f, 2},
    // The Super Nintendo's, from SomeKevin's: standing, its label to the back; the label
    // (a European one) is the long one across its top, mirrored in the texture.
    {ConsoleSystem::Snes, "snes-cartridge.glb", 0, 0, {0.006f, 0.078f, 0.486f, 0.236f}, {0.006f, 0.078f, 0.486f, 0.236f}, 2,
     2.7f, 0.4f, 180.0f},
};

}  // namespace

void BoxShelf::loadConsoleModels(const std::vector<std::string>& folders) {
    for (const CartridgeFile& f : kCartridgeFiles) {
        for (const std::string& folder : folders) {
            GltfModel model;
            std::string error;
            if (!loadGltf(folder + f.file, model, &error)) continue;
            setCartridgeModel(f.system, model, f.turnX, f.turnZ, f.label, f.picture, f.labelTurn, f.pictureAspect, f.pictureMiddle, f.turnY,
                              f.labelImage);
            break;
        }
    }
    // The room's television, stood on its foot and scaled to its real width.
    for (const std::string& folder : folders) {
        GltfModel model;
        std::string error;
        if (!loadGltf(folder + "television.glb", model, &error)) continue;
        Custom& c = custom_[kRoomCustom];
        upload(c, model);
        const float cx = (model.min[0] + model.max[0]) / 2, cz = (model.min[2] + model.max[2]) / 2;
        const float k = model.max[0] > model.min[0] ? kTelevisionWidth / (model.max[0] - model.min[0]) : 1;
        c.fit = scale(k, k, k) * translate(-cx, -model.min[1], -cz);
        break;
    }
    for (const ModelFile& m : kModelFiles) {
        for (const std::string& folder : folders) {
            GltfModel model;
            std::string error;
            if (!loadGltf(folder + m.file, model, &error)) continue;
            if (m.box) {
                std::copy(m.box, m.box + 3, model.min);
                std::copy(m.box + 3, m.box + 6, model.max);
            }
            const Led l = ledOf(m.model);
            const float none[3] = {0, 0, 0};
            setConsoleModel(m.model, model, m.width, m.turn, none, l.at, l.tilt);
            break;
        }
    }
}

namespace {

// The front of each system's box, width / height: the NES's (12.7 x 18.2 cm), the Game
// Boy's (12.7 cm square), the Super Nintendo's lying on its side (18 x 12.7 cm).
float frontAspect(ConsoleSystem s) {
    switch (s) {
        case ConsoleSystem::Nes: return 12.7f / 18.2f;
        case ConsoleSystem::Snes: return 18.0f / 12.7f;
        default: return 1.0f;  // the Game Boy's, square
    }
}


// A Super Nintendo box lying down opens at its right end, the others at the top.
bool opensAtSide(ConsoleSystem s, float w, float h) { return s == ConsoleSystem::Snes && w > h; }

// 0 in .. 1 the holder out as far as it goes.
float trayOut(float open) { return smooth((open - 0.3f) / 0.25f); }

// 0 standing .. 1 an NES box on its side, to be opened.
float tipped(ConsoleSystem s, float open) { return s == ConsoleSystem::Nes ? smooth(open / 0.15f) : 0.0f; }

}  // namespace

BoxShelf::Size BoxShelf::boxSize(const Item& item) {
    Size s;
    switch (item.system) {
        case ConsoleSystem::Nes: s = {12.7f / 13.5f, 18.2f / 13.5f, 3.8f / 13.5f}; break;
        case ConsoleSystem::Snes: s = {18.0f / 13.5f, 12.7f / 13.5f, 3.8f / 13.5f}; break;
        default: s = {12.7f / 13.5f, 12.7f / 13.5f, 2.5f / 13.5f}; break;  // square, 5 x 5 inches
    }
    // A Super Nintendo cover taller than wide (a European one standing up) gets a box to match.
    if (item.system == ConsoleSystem::Snes && item.texture && item.width > 0 && item.height > 0) {
        const float w = static_cast<float>(item.width), h = static_cast<float>(item.height);
        const float f = coverFront(item.width, item.height, item.system);
        if ((f > 0 ? w * (1 - f) : w) < h) std::swap(s.w, s.h);
    }
    return s;
}

BoxShelf::Mat4 BoxShelf::heldBox() const {
    // Out of the shelf towards the viewer, then over to the front, turning to show its
    // cover; aside to the left once its cartridge is out.
    const Vec3 s = shelfSpot(selected_);
    const float t1 = smooth(take_ / 0.4f), t2 = smooth((take_ - 0.3f) / 0.7f);
    // An NES one, too tall to slide its holder out upwards in view, is first tipped onto
    // its side, its top end to the right.
    const float aside = system_ == ConsoleSystem::Nes ? 0.55f : 0.35f;
    const Vec3 out{s.x, s.y, kShelfZ + 1.1f}, front{stageX_ - aside * smooth(open_), 0.1f, 0};
    const Vec3 a{lerp(s.x, out.x, t1), lerp(s.y, out.y, t1), lerp(s.z, out.z, t1)};
    const Vec3 p{lerp(a.x, front.x, t2), lerp(a.y, front.y, t2), lerp(a.z, front.z, t2)};
    return translate(p.x, p.y, p.z) * rotateY(90 * (1 - t2)) * rotation(boxTurn_) * rotateZ(-90 * tipped(system_, open_));
}

BoxShelf::Mat4 BoxShelf::heldCartridge() const {
    // Pulled out of its holder through the open end, then up in front of the box, facing
    // the viewer.
    const Mat4 in = cartridgeInBox(heldBox(), system_, held_);
    const float s = smooth((open_ - 0.65f) / 0.35f);
    if (s <= 0) return in;
    const Vec3 out{stageX_ + 0.45f, 0.1f, 1.3f};
    const Vec3 p{lerp(in[12], out.x, s), lerp(in[13], out.y, s) + std::sin(s * kPi) * 0.25f, lerp(in[14], out.z, s)};
    // From lying as the box does to the way it is turned.
    const Quat from = boxTurn_ * quatZ(-90 * tipped(system_, open_));
    return translate(p.x, p.y, p.z) * rotation(towards(from, cartTurn_, s));
}

BoxShelf::Mat4 BoxShelf::cartridgeInBox(const Mat4& box, ConsoleSystem s, const Size& size) const {
    // It stands in the holder in front of the manual, its label to the front, its contacts
    // down and its top a little below the open end; the holder slides out with it, then it
    // is pulled out the rest of the way.
    const CartridgeShape c = cartridgeShape(s);
    const bool side = opensAtSide(s, size.w, size.h);
    const float length = side ? size.w : size.h, along = side ? c.w : c.h;
    const float tray = trayOut(open_) * length * 0.4f, pull = smooth((open_ - 0.5f) / 0.2f) * along * 0.75f;
    const float at = length / 2 - kCard - 0.03f - along / 2 + tray + pull;  // its middle along the way out
    const float z = -size.d / 2 + kCard + 0.012f + kCard + c.d / 2;
    return box * (side ? translate(at, 0, z) : translate(0, at, z));
}

float BoxShelf::coverFront(int width, int height, ConsoleSystem system) {
    if (width <= 0 || height <= 0) return 0;
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    // A whole sleeve is the back, the spine and the front side by side, so it is told by
    // being well wider than a front.
    const float aspect = frontAspect(system);
    if (w >= h * aspect * 1.6f) return std::clamp(1 - aspect * h / w, 0.0f, 0.9f);
    // A European Super Nintendo one, its front standing up: about 1.6 times as wide as
    // tall, where a front lying down alone is 1.42.
    if (system == ConsoleSystem::Snes && w >= h * 1.55f) return std::clamp(1 - (12.7f / 18.0f) * h / w, 0.0f, 0.9f);
    return 0;
}

BoxShelf::Vec3 BoxShelf::boxColour(ConsoleSystem s) {
    switch (s) {
        case ConsoleSystem::GameBoyColor: return {0.85f, 0.83f, 0.78f};
        case ConsoleSystem::Nes: return {0.07f, 0.07f, 0.08f};
        case ConsoleSystem::Snes: return {0.09f, 0.09f, 0.1f};
        default: return {0.82f, 0.8f, 0.75f};
    }
}

BoxShelf::Mat4 BoxShelf::addBox(const std::vector<Item>& items, int index, const Mat4& place, float open, bool withManual) {
    // A box of thin card, closed by a tuck flap at one end: the top of an NES or Game Boy
    // one, the right end of a Super Nintendo one lying down. Open, the flap swings back,
    // and the card holder inside, with the manual behind the cartridge, slides out a way.
    const Item& item = items[static_cast<size_t>(index)];
    const Size S = boxSize(item);
    const float bw = S.w, bh = S.h, bd = S.d;  // this box's own
    auto add = [&](const Mat4& m, Vec3 colour, float shine, int texFaces = 0) {
        Part p;
        p.model = place * m;
        p.colour = colour;
        p.shine = shine;
        p.texture = item.texture;
        p.texFaces = item.texture ? texFaces : 0;
        p.box = index;
        parts_.push_back(p);
    };
    const Vec3 card = boxColour(item.system);
    const float cardShine = 0.25f;
    // With a whole sleeve its back and spine are printed on the box; else the spine takes
    // the edge of the front next to it.
    const Sleeve cover = sleeve(item.width, item.height, item.system);
    const bool wrap = cover.spine[2] > cover.spine[0];
    const std::array<float, 4> spine =
        wrap ? cover.spine
             : std::array<float, 4>{cover.front[0], cover.front[1], cover.front[0] + (cover.front[2] - cover.front[0]) * bd / bw,
                                    cover.front[3]};
    const bool side = opensAtSide(item.system, S.w, S.h);
    // The front, the back and the spine (left), then the other sides but the open end.
    add(translate(0, 0, bd / 2 - kCard / 2) * scale(bw, bh, kCard), item.texture ? Vec3{1, 1, 1} : card, cardShine, 1 << 4);
    parts_.back().texRect = cover.front;
    add(translate(0, 0, -bd / 2 + kCard / 2) * scale(bw, bh, kCard), card, cardShine, wrap ? 1 << 5 : 0);
    parts_.back().texRect = cover.back;
    add(translate(-bw / 2 + kCard / 2, 0, 0) * scale(kCard, bh, bd - 2 * kCard), card, cardShine, 1 << 1);
    parts_.back().texRect = spine;
    if (!side) add(translate(bw / 2 - kCard / 2, 0, 0) * scale(kCard, bh, bd - 2 * kCard), card, cardShine);
    if (side) add(translate(0, bh / 2 - kCard / 2, 0) * scale(bw - 2 * kCard, kCard, bd - 2 * kCard), card, cardShine);
    add(translate(0, -bh / 2 + kCard / 2, 0) * scale(bw - 2 * kCard, kCard, bd - 2 * kCard), card, cardShine);
    // The open end, seen with the way out as +y: across it ew, along the way out length.
    const float ew = side ? bh : bw, length = side ? bw : bh;
    const Mat4 end = side ? translate(bw / 2, 0, 0) * rotateZ(-90) : translate(0, bh / 2, 0);
    // The flap, on its fold at the back, and the tongue at its end tucked down inside the
    // front; opening, the flap swings up and back and the tongue straightens.
    const float f = smooth((open - 0.12f) / 0.2f);
    const Mat4 flap = end * translate(0, -kCard / 2, -bd / 2 + kCard / 2) * rotateX(-kFlapOpen * f);
    add(flap * translate(0, 0, bd / 2 - kCard / 2) * scale(ew, kCard, bd - kCard), card, cardShine);
    const float tongue = std::min(0.12f, length * 0.12f);
    add(flap * translate(0, 0, bd - 2 * kCard) * rotateX(lerp(90.0f, 20.0f, f)) * translate(0, 0, tongue / 2) *
            scale(ew - 4 * kCard, kCard, tongue),
        card, cardShine);
    // The holder: a board at the back with a rail down each side and a ledge across the
    // bottom, sliding out along the way out.
    const float out = trayOut(open_) * length * 0.4f;
    const Vec3 holderColour = item.system == ConsoleSystem::Nes ? Vec3{0.1f, 0.1f, 0.11f} : Vec3{0.9f, 0.89f, 0.86f};
    const float hw = ew - 2 * kCard - 0.004f, hl = length - 2 * kCard - 0.004f;
    const float boardZ = -bd / 2 + kCard + 0.012f + kCard / 2;  // the manual lies behind it
    const Mat4 holder = end * translate(0, -kCard - 0.002f - hl / 2 + out, 0);
    const float railZ = (boardZ + bd / 2 - kCard - 0.002f) / 2, railD = bd / 2 - kCard - 0.002f - boardZ;
    if (out > 0.0005f || open > 0.001f) {
        add(holder * translate(0, 0, boardZ) * scale(hw, hl, kCard), holderColour, 0.15f);
        const CartridgeShape c = cartridgeShape(item.system);
        const float across = side ? c.h : c.w, rail = std::max(0.02f, (hw - across) / 2 - 0.004f);
        add(holder * translate(-hw / 2 + rail / 2, 0, railZ) * scale(rail, hl, railD), holderColour, 0.15f);
        add(holder * translate(hw / 2 - rail / 2, 0, railZ) * scale(rail, hl, railD), holderColour, 0.15f);
        add(holder * translate(0, -hl / 2 + 0.015f, railZ) * scale(hw, 0.03f, railD), holderColour, 0.15f);
    }
    // The manual behind the board, its cover to the front, riding out with the holder.
    const float mz = -bd / 2 + kCard + 0.006f;
    const Mat4 manual = (side ? translate(bw / 2 - kCard - 0.002f - (bw - 2 * kCard) / 2 + out, 0, mz)
                              : translate(0, bh / 2 - kCard - 0.002f - (bh - 2 * kCard) / 2 + out, mz)) *
                        rotateY(180);
    if (withManual && open > 0.001f) {
        add(manual * scale(bw * 0.86f, bh * 0.88f, 0.006f), {0.86f, 0.84f, 0.78f}, 0.1f, 1 << 5);
        parts_.back().texRect = cover.front;
        if (item.manual && index == selected_) {
            parts_.back().manual = true;
            if (!pages_.empty() && pages_[0].texture) {
                parts_.back().texture = pages_[0].texture;
                parts_.back().texFaces = 1 << 5;
                parts_.back().texRect = {0, 0, 1, 1};
                parts_.back().colour = {1, 1, 1};
            }
        }
    }
    return place * manual;
}

BoxShelf::Mat4 BoxShelf::addConsole(const Item& item, float light, float rise, float /*tray: none*/) {
    using L = ConsoleLayout;
    const ConsoleSystem s = item.system;
    const ConsoleModel m = consoleModelOf(s);
    const Mat4 base = translate(stageX_, kFloorY + rise, kConsoleZ);
    // The console's own model when it was given one, else the built-in.
    const Custom& own = custom_[static_cast<int>(m)];
    Part p;
    p.colour = {1, 1, 1};
    p.shine = 0.45f;
    if (own.vao) {
        p.model = base * own.fit;
        p.custom = static_cast<int>(m);
    } else {
        p.model = base;
        p.mesh = static_cast<int>(m);
    }
    parts_.push_back(p);
    // The power light: dark red on the built-in model, lit once the game is in.
    const Led l = ledOf(m);
    if (light > 0 || !own.vao) {
        Part led;
        led.model = base * translate(l.at[0], l.at[1], l.at[2]) * rotateX(l.tilt - 90) * scale(0.03f, 0.03f, 0.004f);
        led.colour = light > 0 ? Vec3{1.0f, 0.12f, 0.08f} : Vec3{0.22f, 0.03f, 0.03f};
        led.glow = light > 0 ? 1.6f : 0.0f;
        led.shine = 0.6f;
        led.cylinder = true;
        parts_.push_back(led);
    }
    const CartridgeShape c = cartridgeShape(s);
    switch (s) {
        case ConsoleSystem::Nes:
            // Lying on its back in the door, pushed down: its label up, its top end out.
            return translate(L::kNesDoor[0], kFloorY + L::kNesDoor[1] - L::kNesDown, kConsoleZ + L::kNesDoor[2] - L::kNesIn) *
                   rotateY(180) * rotateX(-90);
        case ConsoleSystem::Snes:
            return translate(L::kSnesSlot[0], kFloorY + L::kSnesSlot[1] + c.h / 2 - c.insert, kConsoleZ + L::kSnesSlot[2]);
        default: {
            // A Game Boy's screen shows the game's cover once it is on.
            const bool color = s == ConsoleSystem::GameBoyColor;
            const float* sc = color ? L::kGbcScreen : L::kGbScreen;
            const float* slot = color ? L::kGbcSlot : L::kGbSlot;
            Part screen;
            screen.model = base * translate(sc[0], sc[1], (color ? L::kGbcFrontZ : L::kGbFrontZ) + 0.002f) * scale(sc[2], sc[3], 0.002f);
            screen.colour = color ? Vec3{0.05f, 0.06f, 0.05f} : Vec3{0.42f, 0.47f, 0.2f};
            screen.shine = 1.0f;
            if (light > 0 && item.texture) {
                screen.texture = item.texture;
                screen.texFaces = 1 << 4;
                screen.texRect = sleeve(item.width, item.height, item.system).front;
                screen.colour = {1, 1, 1};
                screen.glow = 0.6f;
            }
            parts_.push_back(screen);
            return translate(slot[0], kFloorY + slot[1] + c.h / 2 - c.insert, kConsoleZ + slot[2]);
        }
    }
}

void BoxShelf::addInsertion(const Item& item, float light, float rise, float t) {
    const Mat4 inConsole = translate(stageX_, 0, 0) * addConsole(item, light, rise, 0);
    // The cartridge's way: up from where it was held, over to above the slot in an arc,
    // then down into it. Into an NES it goes lying on its back, in through the door, and
    // then it is pushed down.
    const bool nes = system_ == ConsoleSystem::Nes;
    const Mat4 start = heldCartridge();
    const Vec3 p0{start[12], start[13], start[14]}, p1{p0.x, p0.y + 0.15f, p0.z};
    const Vec3 p3{inConsole[12], inConsole[13], inConsole[14]};
    const Vec3 in{p3.x, p3.y + (nes ? ConsoleLayout::kNesDown : 0.0f), p3.z};
    const Vec3 p2 = nes ? Vec3{in.x, in.y, in.z + ConsoleLayout::kNesIn + cartridgeShape(system_).h / 2 + 0.08f}
                        : Vec3{p3.x, p3.y + cartridgeShape(system_).insert + 0.12f, p3.z};
    Vec3 p;
    if (t < 0.25f) {
        const float s = smooth(t / 0.25f);
        p = {lerp(p0.x, p1.x, s), lerp(p0.y, p1.y, s), lerp(p0.z, p1.z, s)};
    } else if (t < 0.7f) {
        const float s = smooth((t - 0.25f) / 0.45f);
        p = {lerp(p1.x, p2.x, s), lerp(p1.y, p2.y, s) + std::sin(s * kPi) * 0.2f, lerp(p1.z, p2.z, s)};
    } else if (nes) {
        // In, then down.
        const float s = smooth((t - 0.7f) / 0.2f), d = smooth((t - 0.9f) / 0.1f);
        p = {in.x, lerp(in.y, p3.y, d), lerp(p2.z, in.z, s)};
    } else {
        const float s = smooth((t - 0.7f) / 0.3f);
        p = {p3.x, lerp(p2.y, p3.y, s), p3.z};
    }
    // Into an NES on its back, its top end to the viewer; into a Game Boy with its label to
    // the back, the top of it showing.
    const float lay = nes ? -90.0f * smooth((t - 0.25f) / 0.45f) : 0.0f;
    const bool handheld = system_ == ConsoleSystem::GameBoy || system_ == ConsoleSystem::GameBoyColor;
    const float spin = nes || handheld ? 180.0f * smooth((t - 0.25f) / 0.45f) : 0.0f;
    addCartridge(item, translate(p.x, p.y, p.z) * rotateY(spin) * rotation(cartTurn_) * rotateX(lay));
}

void BoxShelf::consoleView(ConsoleSystem s, Vec3* eye, Vec3* target) {
    *eye = {0.15f, 0.55f, 4.3f}, *target = {0.0f, -0.05f, 1.6f};  // a Game Boy, standing
    switch (s) {
        case ConsoleSystem::GameBoyColor: *eye = {0.15f, 0.5f, 4.2f}, *target = {0.0f, -0.1f, 1.6f}; break;
        case ConsoleSystem::Nes: *eye = {0.5f, 1.3f, 5.6f}, *target = {0.0f, -0.35f, 1.7f}; break;
        case ConsoleSystem::Snes: *eye = {0.35f, 1.1f, 5.1f}, *target = {0.0f, -0.35f, 1.5f}; break;
        default: break;
    }
}

float BoxShelf::boxOpening() const { return open_; }

bool BoxShelf::addDisc(const Item&, Part, const Mat4&) { return false; }  // all cartridges

}  // namespace plum
