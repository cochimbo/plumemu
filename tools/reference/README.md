# Image and sound reference

The plan for joining plumsg and plumnin (phase 0) asks that every step leave both
emulators drawing and sounding exactly as before. This folder checks that: a fixed set
of runs of `plumsg-headless` and `plumnin-headless` (`runs.txt`), each one summed up by
the hash of its last frame, the hash of its whole sound and the hash of its per-frame
log (a picture and a sound hash for every frame). `reference.txt` holds what plumsg
6e2285f and plumnin 0432fe4 gave before they came into plumemu.

The runs cover the built-in test programs of both (Mega Drive, Master System, Game Gear;
NES, Game Boy, Game Boy Color, SNES), the 240p Test Suite, and Game Boy, NES and SNES test
ROMs, with the original sound and with the Roland sound or the synthesizer (GeneralUser GS).

```sh
tools/reference/fetch.sh roms                                   # downloads, once
tools/reference/run.sh build/sega/plumsg-headless build/nintendo/plumnin-headless roms now.txt
tools/reference/compare.sh now.txt                               # exit code 1 if anything changed
```

The CI's headless job does the same on every pull request. A change that draws or sounds
different on purpose rewrites `reference.txt` (run.sh's output, with its header kept)
and says why in the pull request.

The test ROMs from plumsg's issues are not here: GitHub only serves them to its own
runners. They stay in plumsg's `Test ROMs` workflow (`.github/workflows/test-roms.yml`),
which compares a branch with its base.
