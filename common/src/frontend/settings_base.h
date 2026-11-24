#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "render/renderer.h"

namespace plum {

// What both programs' menus change apart from the controls and the console's own
// settings (each program's Settings, which is one of these with its sound, ports and
// BIOS added). They live in plumsg.ini or plumnin.ini, read and written by
// settings_ini.cpp, which each program builds with its own Settings.
struct SettingsBase {
    static constexpr size_t kMaxRecent = 10;
    static constexpr size_t kMaxHistory = 200;
    // The library's tabs besides the consoles' (librarySystem): every game, the ones
    // played lately and the favourites.
    static constexpr int kTabAll = -1, kTabFavorites = -2, kTabHistory = -3;
    static constexpr int kMaxRewindSeconds = 60;

    std::string language;  // "es", "en", "fr" or "pt"; empty: the system's, decided at start
    // Console mode, for PCs played from the sofa with a pad (Steam Deck, Big Picture): full
    // screen, no menu bar, the library at start, bigger letters and the pad in every window.
    // 0 automatic (on when Steam's game mode or Big Picture started the program), 1 on, 2 off.
    int consoleMode = 0;
    // The size of the interface in percent (kMinUiScale-kMaxUiScale); 0 automatic: 100,
    // or after the screen's height in console mode.
    static constexpr int kMinUiScale = 75, kMaxUiScale = 250;
    int uiScale = 0;
    std::string view = "2D";  // Renderer::name() of the active view
    RenderOptions render;
    // Draw every sprite on a line, without the console's limit that makes them flicker.
    bool unlimitedSprites = false;
    // Widescreen: columns added on the left and on the right (up to
    // VideoFrame::kMaxWideSide), with the planes and sprites past the edges of the screen.
    int wideLeft = 0, wideRight = 0;
    int windowScale = 3;
    bool fullscreen = false;
    bool showMenu = true;
    int volume = 80;  // 0-100
    bool mute = false;
    // The console's processor speed in percent (100 off, up to kMaxOverclock), for games
    // that slow down. In a netplay game the host's counts; hardcore mode keeps it at 100.
    static constexpr int kMaxOverclock = 400;
    int overclock = 100;
    int stateSlot = 0;  // 0-9
    int rewindSeconds = 20;  // how far back rewinding reaches, 0 = off
    int fastForward = 4;     // fast-forward speed, times normal (2-10)
    int slowMotion = 50;     // slow-motion speed, percent of normal (10-90)
    // Run-ahead: frames of input lag taken away by showing the picture that many frames
    // ahead (0 off, up to kMaxRunAhead).
    static constexpr int kMaxRunAhead = 4;
    int runAhead = 0;
    // The frames-per-second counter over the game, with its graph, and the corner it is
    // in: 0 top left, 1 top right, 2 bottom left, 3 bottom right.
    static constexpr int kFpsCorners = 4;
    bool fpsCounter = false;
    int fpsCorner = 3;
    std::vector<std::string> recent;  // most recent first
    // Every game played (up to kMaxHistory), the last one first: the library's Histórico tab.
    std::vector<std::string> history;
    // The games marked as favourites in the library (its game menu), in the order marked.
    std::vector<std::string> favorites;
    std::string lastFolder;
    std::vector<std::string> libraryFolders;  // where the game library looks for games
    bool libraryAtStart = true;   // open the library when the program starts without a ROM
    int librarySystem = -1;       // the library's tab: kTabAll, kTabHistory, kTabFavorites or a ConsoleSystem
    bool libraryList = false;     // the library as a list (title, console, year...) instead of a grid of covers
    bool libraryBoxes = false;    // the library as a row of 3D boxes that open (wins over libraryList)
    bool libraryDetails = true;   // the panel on the right with the chosen game's cover, data and description
    static constexpr int kMinCoverWidth = 90, kMaxCoverWidth = 300;
    int libraryCoverWidth = 150;  // the width of the grid's covers, in pixels: smaller ones fit more to a row
    static constexpr int kLibrarySorts = 5;
    int librarySort = 0;          // what the games are sorted by: 0 title, 1 console, 2 year, 3 genre, 4 developer
    // Netplay: the UDP port to host on (and to join when the address has none), the
    // last host joined and the input delay in frames.
    static constexpr int kDefaultNetPort = 7845;
    int netPort = kDefaultNetPort;
    std::string netAddress;
    int netInputDelay = 2;
    // RetroAchievements: on or off, the account (its token, never the password) and
    // hardcore mode.
    bool achievements = false;
    std::string raUser;
    std::string raToken;
    bool raHardcore = false;
    // ScreenScraper (screenscraper.fr), for the library's media: the user's account, a
    // developer account if the program was built without one, and the kinds of media to
    // download (SsKind ids with commas; empty: the usual ones).
    std::string ssUser, ssPassword;
    std::string ssDevId, ssDevPassword;
    std::string ssMedia;
    // Downloading a game again overwrites what it has (else only what is missing is added).
    bool ssOverwrite = false;

    void addRecent(const std::string& path);
    void removeRecent(const std::string& path);
    // Puts `path` first in the history.
    void addHistory(const std::string& path);
    void removeHistory(const std::string& path);
    bool isFavorite(const std::string& path) const;
    // Marks `path` as a favourite, or takes it out of them.
    void setFavorite(const std::string& path, bool favorite);
    // The same game's path however it was written: slashes either way and, on Windows,
    // letters in either case.
    static bool samePath(const std::string& a, const std::string& b);
    static std::string pathKey(const std::string& path);
};

// What settings_ini.cpp and each program's settings.cpp read and write values with.
namespace ini {
std::string lower(std::string s);
bool yes(const std::string& value);  // yes, 1 or true (iniName's spellings too)
inline const char* flag(bool b) { return b ? "yes" : "no"; }
extern const char* const kRegionNames[4];  // auto, japan, usa, europe
}  // namespace ini

}  // namespace plum
