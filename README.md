# hellfire-saga-pc

A PC port of **Sonic the HellFire Saga**, one of the best Sonic hacks ever, for Windows and Linux. It's written in plain C with SDL2.

- Widescreen at **400×224**: 16:9 at the Genesis's native height
- A launcher with art by **SAWhane**
- Black bars that ease in during boss fights, so bosses play at the original 320px width *(coming)*
- A soundtrack player built from your own ROM *(coming)*
- Game logic recompiled to native C *(coming, see Roadmap)*

> **This repo contains no game data.** You need your own copy of the
> HellFire Saga v2.11 ROM, already patched. The launcher checks it by
> SHA-1 (`f8f57b7295c93ee30fc02340051d081a060f8e17`).

## Playing

1. Patch your own Sonic 3 ROM with the HellFire Saga v2.11 patch.
2. Run `hellfire` (or `hellfire.exe`) and drag the patched `.gen`/`.bin` onto the window.
   You can also pass it on the command line: `hellfire path/to/HellfireSaga.gen`.
3. The launcher remembers the file. Press **Play**.

| Action | Keyboard | Controller |
|---|---|---|
| D-pad | Arrow keys | D-pad / left stick |
| A / B / C | Z / X / C | X / A / B (Xbox layout) |
| X / Y / Z | A / S / D | LB / Y / RB |
| Start | Enter | Start |
| Pause menu | Esc | Guide |
| Fullscreen | F11 or Alt+Enter | |

Settings and battery saves are stored in `%APPDATA%\HellFireSagaPC\hellfire\` (Windows) or `~/.local/share/HellFireSagaPC/hellfire/` (Linux).

## Building

**Linux**

```sh
sudo apt install build-essential cmake libsdl2-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/hellfire
```

**Windows** (cross-compiled from Linux or WSL with MinGW-w64)

```sh
sudo apt install mingw-w64
curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz
tar xzf SDL2-devel-2.30.9-mingw.tar.gz
cmake -B build-win -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
  -DSDL2_DIR=$PWD/SDL2-2.30.9/x86_64-w64-mingw32/lib/cmake/SDL2
cmake --build build-win -j --target hellfire
```

GitHub Actions builds both on every pull request. Download them from the run's **Artifacts**.

**Smoke test.** This boots a ROM headless, runs N frames and writes a screenshot:

```sh
./build/hellfire_smoketest HellfireSaga.gen out.ppm 2400 10   # 10 extra columns = 400px
```

## How it fits together

```
src/main.c        main loop, frame pacing (vsync lockstep on 60 Hz screens)
src/launcher.c    ROM setup, menus, options, credits, pause menu
src/gfx.c         tiny software renderer: rectangles, 8x8 font, images
src/video.c       one SDL texture, pixel-perfect or fit scaling
src/audio.c       short SDL audio queue with drift correction (low latency)
src/controls.c    keyboard + SDL game controllers
src/romcheck.c    SHA-1 check of the player's ROM
src/widescreen.c  in-memory patches so the game draws level + sprites 400px wide
src/emu.c         the only file that talks to the hardware core
src/osd.h         glue header the core expects
third_party/gpgx-wide   Genesis Plus GX Wide: 68000, Z80, VDP, YM2612, PSG in C
```

## Roadmap

1. **Playable base**: launcher, ROM check, 400×224, Windows + Linux builds. *(this step)*
2. **Native code**: a static recompiler that translates the ROM's 68000 code
   into C, which replaces the emulated CPU. The graphics and sound chips stay
   as C reimplementations, because the game drives them directly. The
   recompiled C is generated on the player's PC from their own ROM, never
   committed here. Each recompiled frame is checked against step 1's output.
3. **Widescreen polish**: level and sprites already fill 400px (`src/widescreen.c`); still to check: boss arenas, camera limits at level edges, other zones.
4. **Boss black bars**: detect boss fights from game state and animate bars in to 320px.
5. **Soundtrack**: render every track from the player's ROM and play it in the launcher.

## Credits

- Sonic the HellFire Saga by the HellFire Saga team
- Launcher art by SAWhane
- Sonic the Hedgehog 3 © SEGA. This is an unofficial, free fan project.
- [Genesis Plus GX](https://github.com/ekeeke/Genesis-Plus-GX) by Eke-Eke and Charles MacDonald, and its
  [widescreen fork](https://github.com/libretro/Genesis-Plus-GX-Wide). Their non-commercial license
  (`third_party/gpgx-wide/LICENSE.txt`) means builds of this project may not be sold.
- [font8x8](https://github.com/dhepper/font8x8) by Daniel Hepper (public domain)
- [stb_image](https://github.com/nothings/stb) by Sean Barrett (public domain / MIT)
