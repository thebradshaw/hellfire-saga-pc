/*
 * emu.c - drives the Genesis Plus GX (Wide) core.
 */
#include "shared.h"
#include "emu.h"
#include "hf_file.h"

#define BITMAP_W 720
#define BITMAP_H 576

t_config config;

/* Mega CD backup cartridge size; the core expects the frontend to own it. */
uint8_t cart_size = 0;

static uint32_t framebuffer[BITMAP_W * BITMAP_H];
static uint16_t pad_state;
static int      loaded;
static char     sram_file[1024];

/* ---- callbacks the core requires ---------------------------------------- */

/* zlib-compatible CRC-32, used by the core to fingerprint ROMs and SRAM. */
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len)
{
  static uint32_t table[256];
  static int ready;
  uint32_t c;
  unsigned int i;
  int k;

  if (!ready)
  {
    for (i = 0; i < 256; i++)
    {
      c = i;
      for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = 1;
  }
  if (!buf) return 0;
  c = (uint32_t)crc ^ 0xFFFFFFFFu;
  for (i = 0; i < len; i++) c = table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

void osd_input_update(void)
{
  input.pad[0] = pad_state;
}

int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension)
{
  FILE *f;
  long size;
  size_t len = strlen(filename);

  if (extension)
  {
    if (len >= 3) memcpy(extension, filename + len - 3, 3);
    else          memset(extension, 0, 3);
    extension[3] = 0;
  }

  if (!filename[0]) return 0;
  f = hf_fopen(filename, "rb");
  if (!f) return 0;

  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || size > maxsize) { fclose(f); return 0; }

  size = (long)fread(buffer, 1, (size_t)size, f);
  fclose(f);
  return (int)size;
}

/* ---- setup ---------------------------------------------------------------- */

static void set_defaults(int extra_columns)
{
  int i;
  memset(&config, 0, sizeof(config));

  /* sound */
  config.psg_preamp  = 150;
  config.fm_preamp   = 100;
  config.cdda_volume = 100;
  config.pcm_volume  = 100;
  config.hq_fm       = 1;
  config.hq_psg      = 1;
  config.filter      = 0;
  config.lp_range    = 0x9999;
  config.low_freq    = 880;
  config.high_freq   = 5000;
  config.lg = config.mg = config.hg = 100;
  config.ym2612      = YM2612_DISCRETE;
  config.ym2413      = 2;
  config.mono        = 0;

  /* system: everything auto-detected from the ROM header */
  config.addr_error  = 1;
  config.add_on      = HW_ADDON_AUTO;
  config.enhanced_vscroll_limit = 8;
  config.vdp_fix_dma_boundary_bug = 1;

  /* video */
  config.overscan    = 0;
  config.h40_extra_columns = (uint8_t)extra_columns;
  config.h40_extra_columns_alpha = 100;

  /* input: one 6-button pad in port 1 */
  input.system[0] = SYSTEM_GAMEPAD;
  input.system[1] = SYSTEM_GAMEPAD;
  for (i = 0; i < MAX_INPUTS; i++)
    config.input[i].padtype = DEVICE_PAD6B;
}

static void sram_read(void)
{
  FILE *f;
  if (!sram.on || !sram_file[0]) return;
  f = hf_fopen(sram_file, "rb");
  if (!f) return;
  if (fread(sram.sram, 1, sizeof(sram.sram), f) == 0) { /* empty file */ }
  fclose(f);
}

static void sram_write(void)
{
  FILE *f;
  if (!sram.on || !sram_file[0]) return;
  f = hf_fopen(sram_file, "wb");
  if (!f) return;
  fwrite(sram.sram, 1, sizeof(sram.sram), f);
  fclose(f);
}

int emu_load(const char *rom_path, const char *sram_path, int extra_columns)
{
  char path[1024];

  if (loaded) emu_unload();

  set_defaults(extra_columns);
  system_bios = 0;
  memset(boot_rom, 0xFF, sizeof(boot_rom));

  memset(&bitmap, 0, sizeof(bitmap));
  bitmap.width  = BITMAP_W;
  bitmap.height = BITMAP_H;
  bitmap.pitch  = BITMAP_W * 4;
  bitmap.data   = (uint8 *)framebuffer;
  bitmap.viewport.changed = 3;

  snprintf(path, sizeof(path), "%s", rom_path);
  if (load_rom(path) <= 0) return 0;

  snprintf(sram_file, sizeof(sram_file), "%s", sram_path ? sram_path : "");

  audio_init(EMU_SAMPLE_RATE, 0);
  system_init();
  sram_read();
  system_reset();

  pad_state = 0;
  loaded = 1;
  return 1;
}

void emu_unload(void)
{
  if (!loaded) return;
  sram_write();
  audio_shutdown();
  loaded = 0;
}

int  emu_is_loaded(void) { return loaded; }
void emu_reset(void)     { if (loaded) system_reset(); }
void emu_set_pad(uint16_t buttons) { pad_state = buttons; }

int emu_run_frame(int16_t *audio_out)
{
  if (!loaded) return 0;
  system_frame_gen(0);
  return audio_update(audio_out);
}

const uint32_t *emu_frame(int *width, int *height, int *pitch_bytes)
{
  *width  = bitmap.viewport.w + 2 * bitmap.viewport.x;
  *height = bitmap.viewport.h + 2 * bitmap.viewport.y;
  *pitch_bytes = bitmap.pitch;
  return framebuffer;
}

double emu_fps(void)
{
  /* Master clock / (cycles per line * lines per frame) */
  return vdp_pal ? 53203424.0 / (3420.0 * 313.0)    /* 49.70 Hz */
                 : 53693175.0 / (3420.0 * 262.0);   /* 59.92 Hz */
}

uint8_t emu_read_ram8(uint32_t address)
{
  return READ_BYTE(work_ram, address & 0xFFFF);
}

uint16_t emu_read_ram16(uint32_t address)
{
  return (uint16_t)((emu_read_ram8(address) << 8) | emu_read_ram8(address + 1));
}

void emu_write_ram8(uint32_t address, uint8_t value)
{
  WRITE_BYTE(work_ram, address & 0xFFFF, value);
}

/* ROM bytes are stored byte-swapped in 16-bit pairs on little-endian hosts,
   so byte address A lives at cart.rom[A ^ 1]. */
uint8_t emu_rom_read8(uint32_t address)
{
  if (address >= MAXROMSIZE) return 0xFF;
  return READ_BYTE(cart.rom, address);
}

void emu_rom_write8(uint32_t address, uint8_t value)
{
  if (address >= MAXROMSIZE) return;
#ifdef LSB_FIRST
  cart.rom[address ^ 1] = value;
#else
  cart.rom[address] = value;
#endif
}
