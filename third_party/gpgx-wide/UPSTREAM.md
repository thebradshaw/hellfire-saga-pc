Vendored from https://github.com/libretro/Genesis-Plus-GX-Wide
commit b7ad005431f5f0b55e559f6d598d1ed2479bf13b (2026-07-28), `core/` only.

Removed: `core/cd_hw/libchdr` and `core/tremor` (Mega CD disc image formats,
not needed for a cartridge game). No other changes. Our `src/osd.h` replaces
the libretro frontend's osd.h.
