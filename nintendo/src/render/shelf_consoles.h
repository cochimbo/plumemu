#pragma once

// plumnin's consoles on the 3D shelf: the Game Boy, the Game Boy Color, the NES and the
// Super Nintendo, their cardboard boxes with a flap and a card holder, and their
// cartridges. The shelf (render/box_shelf.h) draws them with what is here and in
// shelf_consoles.cpp.

#include "core/system.h"
#include "render/console_models.h"

namespace plum {

using plumnin::buildCartridgeModel;
using plumnin::buildConsoleModel;
using plumnin::cartridgeShape;
using plumnin::CartridgeShape;
using plumnin::ConsoleModel;
using plumnin::ConsoleSystem;
using plumnin::kConsoleVertexFloats;
using plumnin::kSystemCount;

namespace shelf {
constexpr float kOpenTime = 1.5f, kCloseTime = 1.1f;  // seconds, the box opening and closing
// The manual, from how it stands in the box (turned this much) to its place in front.
constexpr float kManualTurn = -180.0f;
constexpr float kBoxShine = 0.25f;  // a closed box on the shelf: card
}  // namespace shelf

}  // namespace plum
