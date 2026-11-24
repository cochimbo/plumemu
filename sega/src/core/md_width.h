#pragma once

#include "core/video_frame.h"

namespace plumsg {

// The widest picture plumsg's consoles draw: the Mega Drive's 320 (H40) plus what the
// widescreen option adds on both sides. The common VideoFrame is wider (the SNES's 512):
// its rows keep VideoFrame::kMaxWidth as their stride, but the VDPs fill, clear and record
// only this much of each one, as when the frame was plumsg's own.
constexpr int kMdMaxWidth = 320 + 2 * VideoFrame::kMaxWideSide;

}  // namespace plumsg
