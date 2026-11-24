#pragma once

// The code plumsg (sega/) and plumnin (nintendo/) share lives in namespace plum.
//
// While the rest of each program still lives in its own namespace (plumsg, plumnin), that
// namespace sees plum's names, so the code that used plumsg::tr or plumnin::Rewind keeps
// compiling as it was: each program's targets define PLUM_APP_NAMESPACE as their own
// namespace. This goes away once the code that uses the common code says plum:: itself.
namespace plum {}

#ifdef PLUM_APP_NAMESPACE
namespace PLUM_APP_NAMESPACE {
using namespace ::plum;
}
#endif
