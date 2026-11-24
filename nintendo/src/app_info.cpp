#include "core/app_info.h"

#include "core/system.h"

// The version comes from CMake (PLUM_VERSION: the release's, else git describe's).
#ifndef PLUM_VERSION
#define PLUM_VERSION "dev"
#endif

namespace plum {

const AppInfo& appInfo() {
    static const AppInfo info{"plumnin", PLUM_VERSION, "plumnin", "plumnin", "plumnin.ini", "https://github.com/cochimbo/plumemu",
                              "(*.gb, *.gbc, *.nes, *.sfc, *.smc, *.zip)",
                              "*.gb;*.gbc;*.cgb;*.nes;*.sfc;*.smc;*.zip",
                              plumnin::isGameFile, "Game Genie o Pro Action Replay"};
    return info;
}

}  // namespace plum
