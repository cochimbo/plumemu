#include "render/box_shelf.h"

#include "render/box_shelf_draw.h"

#include <algorithm>
#include <cmath>

// plumsg's part of the 3D shelf (render/box_shelf.h): the Mega Drive's, the 32X's, the
// Mega CD's, the Master System's and the Game Gear's cases, cartridges, discs and consoles.

namespace plum {

using namespace shelf;
using namespace plumsg;  // its consoles: ConsoleLayout...

namespace {

constexpr float kLidOpen = 165.0f;  // degrees, all the way open

}  // namespace

namespace {

// The models that come with plumsg (or the user's own of the same consoles): how they are
// turned and where their slots are, once standing centred on the floor.
struct ModelFile {
    ConsoleModel model;
    const char* file;
    float width, turn;  // 13.5 cm = 1
    float slot[3];
    const float* led;   // its power light, if the model shows where (and its tilt)
    float ledTilt;
    const float* box;   // the console's own bounds in the file, when it has more (its pads)
};
const float kMegaDriveLed[3] = {0.206f, 0.415f, 0.42f};
// The Master System by dark_igorek, CC BY 4.0, with its two pads plugged in in front (in metres).
const float kMasterSystemBox[6] = {-0.184f, 0.0f, -0.091f, 0.184f, 0.07f, 0.085f};
// plumsg's own Game Gear (tools/models/gamegear.py): its bounds, its power light on the
// glass left of the screen, its screen (middle and size) and its slot's top.
const float kGameGearBox[6] = {-0.105f, 0.0f, -0.02f, 0.105f, 0.11f, 0.02f};
const float kGameGearLed[3] = {-47.5f / 135.0f, 77.4f / 135.0f, 17.75f / 135.0f};
const float kGameGearScreen[4] = {69.8f / 135.0f, 17.75f / 135.0f, 67.0f / 135.0f, 50.9f / 135.0f};
const float kGameGearSlotY = 110.0f / 135.0f;
// plumsg's own Mega-CD (tools/models/megacd.py), 280 mm wide like the Mega Drive on it:
// its body's bounds (the cover at its right stands out of them), also the tray's, which
// is saved closed in the same place.
const float kMegaCdBox[6] = {-0.14f, 0.0f, -0.10733f, 0.14f, 0.0672f, 0.10733f};
// plumsg's own 32X (tools/models/mega32x.py): its origin is where it meets the Mega
// Drive's slot, under the middle of its own slot, whose mouth is 65 mm over it.
const float kMega32xBox[6] = {-0.105f, 0.0f, -0.06f, 0.105f, 0.068f, 0.06f};
const ModelFile kModelFiles[] = {
    // The Mega Drive by synthetic worlds (Jon Blenky), CC BY 4.0: its slot is behind the
    // dome, its power light on the plate in front of it.
    {ConsoleModel::MegaDrive, "megadrive.glb", 278.0f / 135.0f, 0.0f, {0.206f, 0.43f, -0.306f}, kMegaDriveLed, 12.9f, nullptr},
    {ConsoleModel::MasterSystem, "mastersystem.glb", 365.0f / 135.0f, 0.0f, {ConsoleLayout::kSmsSlotX, ConsoleLayout::kSmsSlotY,
     ConsoleLayout::kSmsSlotZ}, nullptr, 0.0f, kMasterSystemBox},
    {ConsoleModel::GameGear, "gamegear.glb", 210.0f / 135.0f, 0.0f, {0.0f, kGameGearSlotY, ConsoleLayout::kGgSlotZ},
     kGameGearLed, 90.0f, kGameGearBox},
    {ConsoleModel::Mega32X, "mega32x.glb", 210.0f / 135.0f, 0.0f, {0, 65.0f / 135.0f, 0}, nullptr, 0.0f, kMega32xBox},
    {ConsoleModel::MegaCD, "megacd.glb", 280.0f / 135.0f, 0.0f, {0, 0, 0}, nullptr, 0.0f, kMegaCdBox},
    {ConsoleModel::MegaCDTray, "megacd-tray.glb", 280.0f / 135.0f, 0.0f, {0, 0, 0}, nullptr, 0.0f, kMegaCdBox},
};

// Where things are on plumsg's own Mega-CD, in the millimetres megacd.py measures in
// (scaled by kOwnCd to the real size, 13.5 cm = 1): its height, where the Mega Drive stands
// on it (flat on its top, its right side in the cover at the right), how far the tray comes
// out, where the disc lies on it, closed, and its READY and ACCESS windows (middle, size).
constexpr float kOwnCd = 280.0f / 300.0f / 135.0f;
constexpr float kOwnCdHeight = 72.0f * kOwnCd;
constexpr float kOwnCdMdX = -0.5f * kOwnCd, kOwnCdMdZ = -4.0f * kOwnCd;
constexpr float kOwnTrayTravel = 140.0f * kOwnCd;
constexpr float kOwnDisc[3] = {-44.0f * kOwnCd, 38.0f * kOwnCd, 26.5f * kOwnCd};
constexpr float kOwnReady[4] = {56.2f * kOwnCd, 38.85f * kOwnCd, 23.0f * kOwnCd, 6.3f * kOwnCd};
constexpr float kOwnAccess[4] = {103.95f * kOwnCd, 38.85f * kOwnCd, 23.9f * kOwnCd, 6.3f * kOwnCd};
constexpr float kOwnLightsZ = 114.9f * kOwnCd;

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
};
const CartridgeFile kCartridgeFiles[] = {
    // The Mega Drive's by Naser, CC BY 4.0: the picture and title above the "MEGA DRIVE"
    // band of its European label.
    {ConsoleSystem::MegaDrive, "megadrive-cartridge.glb", 0, 0, {0.125f, 0.617f, 0.623f, 0.967f}, {0.125f, 0.662f, 0.623f, 0.967f}, 0, 1.8f, 0.45f},
    // The Master System's, from dark_igorek's model: lying on its back, its label (R-Type's)
    // turned on its side in the texture (whose coordinates there run from 1 to 2). The
    // label is a strip along the top, where a cover's title goes. The file has it turned
    // 7.8 degrees, which would leave it crooked in its box.
    {ConsoleSystem::MasterSystem, "mastersystem-cartridge.glb", 90, 0, {1.473f, 0.0f, 1.552f, 0.40f}, {1.473f, 0.0f, 1.552f, 0.40f}, 1, 4.5f, 0.12f, -7.8f},
    // The Game Gear's, plumsg's own: standing, its label (55.6 x 38.4 mm) the whole texture.
    {ConsoleSystem::GameGear, "gamegear-cartridge.glb", 0, 0, {0, 0, 1, 1}, {0, 0, 1, 1}, 0, 1.45f, 0.45f},
};

}  // namespace

void BoxShelf::loadConsoleModels(const std::vector<std::string>& folders) {
    for (const CartridgeFile& f : kCartridgeFiles) {
        for (const std::string& folder : folders) {
            GltfModel model;
            std::string error;
            if (!loadGltf(folder + f.file, model, &error)) continue;
            setCartridgeModel(f.system, model, f.turnX, f.turnZ, f.label, f.picture, f.labelTurn, f.pictureAspect, f.pictureMiddle, f.turnY);
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
            setConsoleModel(m.model, model, m.width, m.turn, m.slot, m.led, m.ledTilt);
            break;
        }
    }
}

BoxShelf::Size BoxShelf::boxSize(const Item& item) {
    Size s;
    if (item.system != ConsoleSystem::MegaCD || !item.texture || item.width <= 0 || item.height <= 0) return s;
    // The front's shape, from the cover (half of a whole sleeve, less the spine).
    const float w = static_cast<float>(item.width), h = static_cast<float>(item.height);
    const float f = coverFront(item.width, item.height, item.system);
    const float aspect = (f > 0 ? w * (1 - f) : w) / h;
    if (aspect < 0.85f) return s;  // the tall box, as a Mega Drive's
    // A jewel case: 14.2 x 12.5 x 1 cm.
    s.h = 12.5f / 13.5f;
    s.w = std::clamp(aspect * s.h, 0.8f, 1.25f);
    s.d = 0.075f;
    return s;
}

BoxShelf::Mat4 BoxShelf::heldBox() const {
    // Out of the shelf towards the viewer, then over to the front, turning to show its
    // cover; aside to the left once its cartridge is out.
    const Vec3 s = shelfSpot(selected_);
    const float t1 = smooth(take_ / 0.4f), t2 = smooth((take_ - 0.3f) / 0.7f);
    const Vec3 out{s.x, s.y, kShelfZ + 1.1f}, front{stageX_ - 0.35f * smooth(open_), 0.1f, 0};
    const Vec3 a{lerp(s.x, out.x, t1), lerp(s.y, out.y, t1), lerp(s.z, out.z, t1)};
    const Vec3 p{lerp(a.x, front.x, t2), lerp(a.y, front.y, t2), lerp(a.z, front.z, t2)};
    return translate(p.x, p.y, p.z) * rotateY(90 * (1 - t2)) * rotation(boxTurn_);
}

BoxShelf::Mat4 BoxShelf::heldCartridge() const {
    // Lifted out of the box and up in front of it, facing the viewer.
    const Mat4 in = cartridgeInBox(heldBox(), system_, held_);
    const float s = smooth((open_ - 0.35f) / 0.65f);
    if (s <= 0) return in;
    const Vec3 out{stageX_ + 0.45f, 0.1f, 1.3f};
    const Vec3 p{lerp(in[12], out.x, s), lerp(in[13], out.y, s) + std::sin(s * kPi) * 0.25f, lerp(in[14], out.z, s)};
    return translate(p.x, p.y, p.z) * rotation(towards(kNoTurn, cartTurn_, s));
}

BoxShelf::Mat4 BoxShelf::cartridgeInBox(const Mat4& box, ConsoleSystem s, const Size& size) const {
    // The disc on its hub in the upper half (in the middle of a jewel case); a cartridge
    // lies in the lower half, its label to the front and its contacts down, as in the console.
    if (s == ConsoleSystem::MegaCD) return box * translate(0, size.h > 1.2f ? 0.16f : 0.0f, -size.d / 2 + kWall + 0.02f);
    const CartridgeShape c = cartridgeShape(s);
    return box * translate(0, -size.h / 2 + kWall + 0.1f + c.h / 2, -size.d / 2 + kWall + 0.006f + c.d / 2);
}

float BoxShelf::coverFront(int width, int height, ConsoleSystem system) {
    if (width <= 0 || height <= 0) return 0;
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    if (system == ConsoleSystem::MegaCD) {
        // A jewel case's front is about as wide as tall, so a sleeve is told by being well
        // wider; its front is half of it but for the spine (about an eighth of its height).
        if (w < h * 1.35f) return 0;
        return std::clamp(1 - (w - 0.12f * h) / 2 / w, 0.0f, 0.9f);
    }
    if (width < height * 6 / 5) return 0;
    return std::clamp(1 - kBoxW / kBoxH * h / w, 0.0f, 0.9f);
}

bool BoxShelf::addDisc(const Item& item, Part p, const Mat4& place) {
    if (item.system != ConsoleSystem::MegaCD) return false;
    const unsigned int art = item.label ? item.label : item.texture;
    // A 12 cm disc, 1.2 mm thick, its label printed with the cover; the hole in the middle.
    // A jewel case's square front shows a whole disc's worth of it.
    if (!item.label && boxSize(item).h < 1.2f) {
        std::array<float, 4>& r = p.texRect;
        const float fw = (r[2] - r[0]) * static_cast<float>(item.width), fh = static_cast<float>(item.height);
        if (fw > fh) {
            const float cut = (1 - fh / fw) * (r[2] - r[0]) / 2;
            r[0] += cut, r[2] -= cut;
        }
    }
    p.model = place * scale(0.889f, 0.889f, 0.009f);
    p.colour = {0.78f, 0.79f, 0.84f};
    p.shine = 1.0f;
    p.cylinder = true;
    p.texture = art;
    p.texFaces = art ? 1 << 4 : 0;
    parts_.push_back(p);
    p.model = place * scale(0.111f, 0.111f, 0.0095f);
    p.colour = {kBackground[0], kBackground[1], kBackground[2]};
    p.texFaces = 0;
    p.shine = 0;
    parts_.push_back(p);
    return true;
}

BoxShelf::Vec3 BoxShelf::boxColour(ConsoleSystem) { return {0.045f, 0.045f, 0.055f}; }  // black plastic

float BoxShelf::boxOpening() const { return smooth(open_ / 0.6f); }

BoxShelf::Mat4 BoxShelf::addBox(const std::vector<Item>& items, int index, const Mat4& place, float lid, bool withManual) {
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
    const Vec3 plastic{0.045f, 0.045f, 0.055f};
    const float inner = bd - kWall;
    // With a whole sleeve its back and spine go round the tray.
    const Sleeve cover = sleeve(item.width, item.height, item.system);
    const bool wrap = cover.spine[2] > cover.spine[0];
    // The tray: its back and four walls, open at the front.
    add(translate(0, 0, -bd / 2 + kWall / 2) * scale(bw, bh, kWall), plastic, 0.6f, wrap ? 1 << 5 : 0);
    parts_.back().texRect = cover.back;
    add(translate(-bw / 2 + kWall / 2, 0, -kWall / 2) * scale(kWall, bh, inner), plastic, 0.6f, wrap ? 1 << 1 : 0);
    parts_.back().texRect = cover.spine;
    add(translate(bw / 2 - kWall / 2, 0, -kWall / 2) * scale(kWall, bh, inner), plastic, 0.6f);
    add(translate(0, bh / 2 - kWall / 2, -kWall / 2) * scale(bw - 2 * kWall, kWall, inner), plastic, 0.6f);
    add(translate(0, -bh / 2 + kWall / 2, -kWall / 2) * scale(bw - 2 * kWall, kWall, inner), plastic, 0.6f);
    // Without a cover the lid takes the console's colour.
    Vec3 lidColour{0.16f, 0.17f, 0.2f};
    switch (item.system) {
        case ConsoleSystem::Mega32X: lidColour = {0.45f, 0.1f, 0.08f}; break;
        case ConsoleSystem::MasterSystem: lidColour = {0.85f, 0.85f, 0.85f}; break;
        case ConsoleSystem::GameGear: lidColour = {0.25f, 0.12f, 0.35f}; break;
        case ConsoleSystem::MegaCD: lidColour = {0.1f, 0.2f, 0.4f}; break;
        default: break;
    }
    const Mat4 hinge = translate(-bw / 2, 0, bd / 2 - kWall / 2) * rotateY(-lid * kLidOpen);
    add(hinge * translate(bw / 2, 0, 0) * scale(bw, bh, kWall), item.texture ? plastic : lidColour, 0.7f, 1 << 4);
    parts_.back().texRect = cover.front;
    const Mat4 manual = hinge * translate(bw / 2, 0, -kWall / 2 - 0.004f);
    if (lid <= 0.001f) return place * manual;
    // Inside: the manual on the lid (its own cover once it is drawn), the holder and the game.
    if (withManual) {
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
    add(translate(0, 0, -bd / 2 + kWall + 0.003f) * scale(bw - 2 * kWall, bh - 2 * kWall, 0.006f),
        {0.11f, 0.11f, 0.13f}, 0.3f);
    return place * manual;
}

BoxShelf::Mat4 BoxShelf::addConsole(const Item& item, float light, float rise, float tray) {
    using L = ConsoleLayout;
    const ConsoleSystem s = item.system;
    const Mat4 base = translate(stageX_, kFloorY + rise, kConsoleZ);
    // The console's own model when it was given one, else the built-in.
    auto mesh = [&](ConsoleModel m, const Mat4& at) {
        Part p;
        p.colour = {1, 1, 1};
        p.shine = 0.45f;
        const Custom& c = custom_[static_cast<int>(m)];
        if (c.vao) {
            p.model = base * at * c.fit;
            p.custom = static_cast<int>(m);
        } else {
            p.model = base * at;
            p.mesh = static_cast<int>(m);
        }
        parts_.push_back(p);
        // Its own power light, lit (the model shows it off).
        if (c.vao && c.hasLed && light > 0) {
            Part l;
            l.model = base * at * translate(c.led[0], c.led[1], c.led[2]) * rotateX(c.ledTilt - 90) * scale(0.022f, 0.022f, 0.002f);
            l.colour = {1.0f, 0.12f, 0.08f};
            l.glow = 1.6f;
            l.cylinder = true;
            parts_.push_back(l);
        }
    };
    // Where its slot is: the given model's, or the built-in's.
    auto slotOf = [&](ConsoleModel m, float x, float y, float z) {
        const Custom& c = custom_[static_cast<int>(m)];
        return c.vao ? Vec3{c.slot[0], c.slot[1], c.slot[2]} : Vec3{x, y, z};
    };
    // The power light: dark red, lit once the game is in.
    auto led = [&](const float* at, bool front) {
        Part p;
        p.model = base * translate(at[0], at[1], at[2]) * (front ? scale(0.03f, 0.03f, 0.006f) : scale(0.035f, 0.006f, 0.035f));
        p.colour = light > 0 ? Vec3{1.0f, 0.12f, 0.08f} : Vec3{0.22f, 0.03f, 0.03f};
        p.glow = light > 0 ? 1.6f : 0.0f;
        p.shine = 0.6f;
        parts_.push_back(p);
    };
    const CartridgeShape c = cartridgeShape(s);
    switch (s) {
        case ConsoleSystem::GameGear: {
            mesh(ConsoleModel::GameGear, identity());
            // Its own model's screen and slot, or the built-in's.
            const bool own = custom_[static_cast<int>(ConsoleModel::GameGear)].vao != 0;
            if (!own) led(L::kGgLed, true);
            const float sy = own ? kGameGearScreen[0] : L::kGgScreenY, sz = own ? kGameGearScreen[1] : L::kGgScreenZ;
            const float sw = own ? kGameGearScreen[2] : L::kGgScreenW, sh = own ? kGameGearScreen[3] : L::kGgScreenH;
            // The screen shows the game's cover once it is on.
            Part screen;
            screen.model = base * translate(0, sy, sz) * scale(sw, sh, 0.002f);
            screen.colour = {0.05f, 0.07f, 0.06f};
            screen.shine = 1.0f;
            if (light > 0 && item.texture) {
                screen.texture = item.texture;
                screen.texFaces = 1 << 4;
                screen.texRect = sleeve(item.width, item.height, item.system).front;
                screen.glow = 0.6f;
            }
            parts_.push_back(screen);
            const float slotY = own ? kGameGearSlotY : L::kGgSlotY;
            return translate(0, kFloorY + slotY + c.h / 2 - c.insert, kConsoleZ + L::kGgSlotZ);
        }
        case ConsoleSystem::MasterSystem:
            mesh(ConsoleModel::MasterSystem, identity());
            if (!custom_[static_cast<int>(ConsoleModel::MasterSystem)].vao) led(L::kSmsLed, false);
            return translate(L::kSmsSlotX, kFloorY + L::kSmsSlotY + c.h / 2 - c.insert, kConsoleZ + L::kSmsSlotZ);
        case ConsoleSystem::MegaCD: {
            // The Mega-CD under the Mega Drive, its tray sliding out of the front.
            if (custom_[static_cast<int>(ConsoleModel::MegaCD)].vao && custom_[static_cast<int>(ConsoleModel::MegaCDTray)].vao) {
                mesh(ConsoleModel::MegaCD, identity());
                mesh(ConsoleModel::MegaDrive, translate(kOwnCdMdX, kOwnCdHeight, kOwnCdMdZ));
                const float out = kOwnTrayTravel * tray;
                mesh(ConsoleModel::MegaCDTray, translate(0, 0, out));
                // READY lights green once the disc is in, ACCESS red while it is read.
                if (light > 0) {
                    for (const float* w : {kOwnReady, kOwnAccess}) {
                        Part p;
                        p.model = base * translate(w[0], w[1], kOwnLightsZ) * scale(w[2], w[3], 0.002f);
                        p.colour = w == kOwnReady ? Vec3{0.45f, 0.8f, 0.55f} : Vec3{0.9f, 0.5f, 0.35f};
                        p.glow = 0.12f * light;
                        p.shine = 0.6f;
                        parts_.push_back(p);
                    }
                }
                return translate(kOwnDisc[0], kFloorY + kOwnDisc[1] + 0.006f, kConsoleZ + kOwnDisc[2] + out) * rotateX(-90);
            }
            mesh(ConsoleModel::MegaCD, identity());
            mesh(ConsoleModel::MegaDrive, translate(L::kCdMdX, L::kCdHeight, L::kCdMdZ));
            led(L::kCdLed, true);
            const float z = L::kTrayZ + L::kTrayTravel * tray;
            mesh(ConsoleModel::MegaCDTray, translate(L::kTrayX, L::kTrayY, z));
            return translate(L::kTrayX, kFloorY + L::kTrayY + 0.006f, kConsoleZ + z) * rotateX(-90);
        }
        case ConsoleSystem::Mega32X: {
            mesh(ConsoleModel::MegaDrive, identity());
            const Vec3 md = slotOf(ConsoleModel::MegaDrive, L::kMdSlotX, L::kMdSlotY, L::kMdSlotZ);
            mesh(ConsoleModel::Mega32X, translate(md.x, md.y, md.z));
            if (!custom_[static_cast<int>(ConsoleModel::MegaDrive)].vao) led(L::kMdLed, false);
            const Vec3 x = slotOf(ConsoleModel::Mega32X, 0, L::k32xSlotY, 0);
            return translate(md.x + x.x, kFloorY + md.y + x.y + c.h / 2 - c.insert, kConsoleZ + md.z + x.z);
        }
        default: {
            mesh(ConsoleModel::MegaDrive, identity());
            if (!custom_[static_cast<int>(ConsoleModel::MegaDrive)].vao) led(L::kMdLed, false);
            const Vec3 md = slotOf(ConsoleModel::MegaDrive, L::kMdSlotX, L::kMdSlotY, L::kMdSlotZ);
            return translate(md.x, kFloorY + md.y + c.h / 2 - c.insert, kConsoleZ + md.z);
        }
    }
}

void BoxShelf::addInsertion(const Item& item, float light, float rise, float t) {
    // The Mega-CD's tray opens for the disc and closes with it.
    const float tray = smooth((t - 0.1f) / 0.2f) * (1 - smooth((t - 0.84f) / 0.14f));
    const Mat4 inConsole = translate(stageX_, 0, 0) * addConsole(item, light, rise, tray);
    // The cartridge's way: up from where it was held, over to above the slot in an arc,
    // then down into it. A disc turns to lie flat on the way.
    const Mat4 start = heldCartridge();
    const Vec3 p0{start[12], start[13], start[14]}, p1{p0.x, p0.y + 0.15f, p0.z};
    const float above = system_ == ConsoleSystem::MegaCD ? 0.3f : cartridgeShape(system_).insert + 0.12f;
    const Vec3 p3{inConsole[12], inConsole[13], inConsole[14]}, p2{p3.x, p3.y + above, p3.z};
    Vec3 p;
    if (t < 0.25f) {
        const float s = smooth(t / 0.25f);
        p = {lerp(p0.x, p1.x, s), lerp(p0.y, p1.y, s), lerp(p0.z, p1.z, s)};
    } else if (t < 0.75f) {
        const float s = smooth((t - 0.25f) / 0.5f);
        p = {lerp(p1.x, p2.x, s), lerp(p1.y, p2.y, s) + std::sin(s * kPi) * 0.2f, lerp(p1.z, p2.z, s)};
    } else {
        // Into the slot; the disc drops onto the tray and rides in on it.
        const float s = smooth((t - 0.75f) / (system_ == ConsoleSystem::MegaCD ? 0.08f : 0.25f));
        p = {p3.x, lerp(p2.y, p3.y, s), p3.z};
    }
    const float turn = system_ == ConsoleSystem::MegaCD ? -90.0f * smooth((t - 0.25f) / 0.5f) : 0.0f;
    // A Game Gear's cartridge goes in with its label to the back, the top of it showing.
    const float spin = system_ == ConsoleSystem::GameGear ? 180.0f * smooth((t - 0.25f) / 0.5f) : 0.0f;
    addCartridge(item, translate(p.x, p.y, p.z) * rotateY(spin) * rotation(cartTurn_) * rotateX(turn));
}

void BoxShelf::consoleView(ConsoleSystem s, Vec3* eye, Vec3* target) {
    *eye = {0.3f, 0.9f, 4.9f}, *target = {0.1f, -0.35f, 1.5f};
    switch (s) {
        case ConsoleSystem::Mega32X: *eye = {0.35f, 1.3f, 5.5f}, *target = {0.1f, -0.05f, 1.4f}; break;
        case ConsoleSystem::MegaCD: *eye = {0.5f, 1.2f, 5.9f}, *target = {0.0f, -0.25f, 1.9f}; break;
        case ConsoleSystem::MasterSystem: *eye = {0.4f, 1.0f, 5.5f}, *target = {0.2f, -0.35f, 1.4f}; break;
        case ConsoleSystem::GameGear: *eye = {0.15f, 0.5f, 4.3f}, *target = {0.0f, -0.12f, 1.6f}; break;
        default: break;
    }
}

}  // namespace plum
