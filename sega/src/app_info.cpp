#include "core/app_info.h"

#include "core/system.h"

// The version comes from CMake (PLUM_VERSION: the release's, else git describe's).
#ifndef PLUM_VERSION
#define PLUM_VERSION "dev"
#endif

namespace plum {

const AppInfo& appInfo() {
    static const AppInfo info{"plumsg", PLUM_VERSION, "plumsg", "plumsg", "plumsg.ini", "https://github.com/cochimbo/plumemu",
                              "(*.md, *.gen, *.smd, *.bin, *.32x, *.sms, *.gg, *.zip, *.cue, *.chd, *.iso)",
                              "*.md;*.gen;*.smd;*.bin;*.32x;*.sms;*.gg;*.zip;*.cue;*.chd;*.iso",
                              plumsg::isGameFile, "Game Genie ABCD-EFGH o Action Replay FF0A12:0009"};
    return info;
}

}  // namespace plum
