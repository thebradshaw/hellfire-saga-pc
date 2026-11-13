A PC port of **Sonic the HellFire Saga** for Windows and Linux, made by Vibe Slop Studios.

**You need your own ROM.** This download contains no game data. Bring the
HellFire Saga v2.11 ROM, already patched onto your own Sonic 3 ROM (SHA-1
`f8f57b7295c93ee30fc02340051d081a060f8e17`), and drag it onto the launcher.

**Windows:** unzip and run `hellfire.exe`.
**Linux (x86-64):** extract and run `./hellfire`. Needs SDL2
(`sudo apt install libsdl2-2.0-0`).

New in 1.1: **Sonic Mania mode**
- The game now plays at 424×240 by default, Sonic Mania's own picture size:
  wider than before and 16 lines taller, with the level, rings and enemies
  drawn all the way to every edge.
- In boss fights and cutscenes the black bars close in from all four sides,
  back to the original 320×224, so bosses play exactly as designed.
- The health meter sits further out in the corner while you run, and slides
  back in with the boss bars.
- Fixes for the wide screens: stray sprite pieces no longer flash at the
  right edge, and Forest of Decay act 2's rain now reaches the left edge.
- Pick the screen under Options → Screen: Mania 424×240, Wide 400×224 or the
  original 320×224.

What's in it:
- Sonic Mania mode (424×240) and 400×224 widescreen, with black bars that
  slide in for boss fights and cutscenes
- The game's 68000 code is recompiled to native C from your ROM when you press
  Play (about a second). **Game code** in the launcher options switches to
  the emulated CPU instead.
- 60/120 FPS, VSync, CRT effects, optional invincibility, bug fixes
- Soundtrack player with the official album's track names
- Launcher art by SAWhane

Credits: the HellFire Saga team; SAWhane; Genesis Plus GX by Eke-Eke and
Charles MacDonald (non-commercial license: these builds may not be sold);
TinyCC (LGPL 2.1). Sonic the Hedgehog 3 © SEGA; this is an unofficial, free
fan project.

Enjoying it? You can [buy me a coffee](https://buymeacoffee.com/chrisbradshaw).
