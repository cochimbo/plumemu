#pragma once

#include "plum.h"

namespace plum {

// What the code both programs share needs to know about each of the program's consoles,
// by the number of its ConsoleSystem (0 the Mega Drive in plumsg, the Game Boy in
// plumnin...). Each program fills it in, in its core/system.cpp.
struct SystemInfo {
    const char* name;            // "Mega Drive"
    const char* key;             // for folders and files: "MegaDrive"
    int screenScraperId;         // ScreenScraper's number for it, 0 if it has none
    const char* libretroName;    // its name in libretro-database's metadat, null if it has none
    const char* libretroCheats;  // and in its cht folder, null if it has none
};
int systemCount();
// Null for a number that is no console.
const SystemInfo* systemInfo(int system);

}  // namespace plum
