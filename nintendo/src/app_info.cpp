#include "core/app_info.h"

// The version comes from CMake (PLUM_VERSION: the release's, else git describe's).
#ifndef PLUM_VERSION
#define PLUM_VERSION "dev"
#endif

namespace plum {

const AppInfo& appInfo() {
    static const AppInfo info{"plumnin", PLUM_VERSION, "plumnin", "plumnin", "plumnin.ini", "https://github.com/cochimbo/plumemu"};
    return info;
}

}  // namespace plum
