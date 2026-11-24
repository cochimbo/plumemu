#pragma once

#include "plum.h"

#include <string>

namespace plum {

// What makes the common code one program or the other: its name, version and where its
// data lives. Each program defines appInfo() once (sega/src/app_info.cpp,
// nintendo/src/app_info.cpp); nothing in common/ writes "plumsg" or "plumnin" itself.
struct AppInfo {
    const char* name;       // "plumsg": window titles, crash reports, the User-Agent, file tags
    const char* version;    // "1.2.0", or git describe's for a build that is not a release
    const char* dataOrg;    // SDL_GetPrefPath(dataOrg, dataApp): %APPDATA%\plumsg\plumsg,
    const char* dataApp;    //   ~/.local/share/plumsg/plumsg (never changes: users' data is there)
    const char* iniName;    // "plumsg.ini", in that folder or beside the executable (portable)
    const char* repoUrl;    // where the code, the issues and the releases are
    // The games it opens, as the Windows file dialog's filter: what it shows after
    // "Juegos" ("(*.md, *.gen...)") and the patterns it matches ("*.md;*.gen...").
    const char* gameFilterName;
    const char* gamePatterns;
    // Whether it opens that file as a game (by its name), for the file browser.
    bool (*isGameFile)(const std::string& path);
    // The cheat codes it takes, as the cheat file's first lines name them.
    const char* cheatCodes;
};

const AppInfo& appInfo();

}  // namespace plum
