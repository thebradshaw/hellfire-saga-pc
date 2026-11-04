/*
 * controls.h - keyboard and controller handling.
 *
 * Default keyboard layout:
 *   Arrows = D-pad, Z/X/C = A/B/C, A/S/D = X/Y/Z, Enter = Start,
 *   Escape = pause menu, F11 or Alt+Enter = fullscreen.
 * Any SDL-recognised controller works (Xbox, PlayStation, Switch Pro...).
 */
#ifndef HF_CONTROLS_H
#define HF_CONTROLS_H

#include <stdint.h>
#include <SDL.h>

/* Menu actions, triggered on press. */
typedef enum
{
  MENU_NONE = 0,
  MENU_UP, MENU_DOWN, MENU_LEFT, MENU_RIGHT,
  MENU_OK, MENU_BACK,
  MENU_PAUSE,
  MENU_FULLSCREEN
} MenuAction;

void controls_init(void);
void controls_shutdown(void);

/* Feed every SDL event here. Returns a menu action if the event made one. */
MenuAction controls_event(const SDL_Event *e);

/* Genesis buttons currently held (EMU_BTN_* bits), from all devices. */
uint16_t controls_pad(void);

#endif
