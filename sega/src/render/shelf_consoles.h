#pragma once

// plumsg's consoles on the 3D shelf: the Mega Drive, the 32X, the Mega CD, the Master System
// and the Game Gear, their plastic cases with a hinged lid and their cartridges (and the
// Mega CD's discs). The shelf (render/box_shelf.h) draws them with what is here and in
// shelf_consoles.cpp.

#include "core/system.h"
#include "render/console_models.h"

namespace plum {

using plumsg::buildCartridgeModel;
using plumsg::buildConsoleModel;
using plumsg::cartridgeShape;
using plumsg::CartridgeShape;
using plumsg::ConsoleModel;
using plumsg::ConsoleSystem;
using plumsg::kConsoleVertexFloats;
using plumsg::kSystemCount;

namespace shelf {
constexpr float kOpenTime = 0.9f, kCloseTime = 0.7f;  // seconds, the box opening and closing
// The manual, from how it lies in the open lid (turned this much) to its place in front.
constexpr float kManualTurn = -165.0f;
constexpr float kBoxShine = 0.6f;  // a closed box on the shelf: glossy plastic
}  // namespace shelf

}  // namespace plum
