#include "controls.h"
#include "emu.h"

#define MAX_PADS 4
#define STICK_DEADZONE 12000

static SDL_GameController *pads[MAX_PADS];

static void open_pad(int index)
{
  int i;
  if (!SDL_IsGameController(index)) return;
  for (i = 0; i < MAX_PADS; i++)
    if (!pads[i]) { pads[i] = SDL_GameControllerOpen(index); return; }
}

void controls_init(void)
{
  int i;
  SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
  for (i = 0; i < SDL_NumJoysticks(); i++) open_pad(i);
}

void controls_shutdown(void)
{
  int i;
  for (i = 0; i < MAX_PADS; i++)
    if (pads[i]) { SDL_GameControllerClose(pads[i]); pads[i] = NULL; }
}

MenuAction controls_event(const SDL_Event *e)
{
  switch (e->type)
  {
    case SDL_CONTROLLERDEVICEADDED:
      open_pad(e->cdevice.which);
      break;

    case SDL_CONTROLLERDEVICEREMOVED:
    {
      int i;
      for (i = 0; i < MAX_PADS; i++)
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == e->cdevice.which)
        {
          SDL_GameControllerClose(pads[i]);
          pads[i] = NULL;
        }
      break;
    }

    case SDL_KEYDOWN:
      if (e->key.repeat && e->key.keysym.sym != SDLK_UP && e->key.keysym.sym != SDLK_DOWN)
        break;
      switch (e->key.keysym.sym)
      {
        case SDLK_UP:     return MENU_UP;
        case SDLK_DOWN:   return MENU_DOWN;
        case SDLK_LEFT:   return MENU_LEFT;
        case SDLK_RIGHT:  return MENU_RIGHT;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          if (e->key.keysym.mod & KMOD_ALT) return MENU_FULLSCREEN;
          return MENU_OK;
        case SDLK_z:
        case SDLK_SPACE:  return MENU_OK;
        case SDLK_x:
        case SDLK_BACKSPACE: return MENU_BACK;
        case SDLK_ESCAPE: return MENU_PAUSE;
        case SDLK_F11:    return MENU_FULLSCREEN;
        default: break;
      }
      break;

    case SDL_CONTROLLERBUTTONDOWN:
      switch (e->cbutton.button)
      {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return MENU_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return MENU_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return MENU_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return MENU_RIGHT;
        case SDL_CONTROLLER_BUTTON_A:          return MENU_OK;
        case SDL_CONTROLLER_BUTTON_B:          return MENU_BACK;
        case SDL_CONTROLLER_BUTTON_GUIDE:      return MENU_PAUSE;
        default: break;
      }
      break;

    default:
      break;
  }
  return MENU_NONE;
}

uint16_t controls_pad(void)
{
  const Uint8 *k = SDL_GetKeyboardState(NULL);
  uint16_t b = 0;
  int i;

  if (k[SDL_SCANCODE_UP])    b |= EMU_BTN_UP;
  if (k[SDL_SCANCODE_DOWN])  b |= EMU_BTN_DOWN;
  if (k[SDL_SCANCODE_LEFT])  b |= EMU_BTN_LEFT;
  if (k[SDL_SCANCODE_RIGHT]) b |= EMU_BTN_RIGHT;
  if (k[SDL_SCANCODE_Z])     b |= EMU_BTN_A;
  if (k[SDL_SCANCODE_X])     b |= EMU_BTN_B;
  if (k[SDL_SCANCODE_C])     b |= EMU_BTN_C;
  if (k[SDL_SCANCODE_A])     b |= EMU_BTN_X;
  if (k[SDL_SCANCODE_S])     b |= EMU_BTN_Y;
  if (k[SDL_SCANCODE_D])     b |= EMU_BTN_Z;
  if (k[SDL_SCANCODE_RETURN] && !(SDL_GetModState() & KMOD_ALT)) b |= EMU_BTN_START;

  for (i = 0; i < MAX_PADS; i++)
  {
    SDL_GameController *p = pads[i];
    int ax, ay;
    if (!p) continue;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP))    b |= EMU_BTN_UP;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  b |= EMU_BTN_DOWN;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  b |= EMU_BTN_LEFT;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= EMU_BTN_RIGHT;
    /* Face buttons by position: left = A, bottom = B, right = C. */
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X))          b |= EMU_BTN_A;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A))          b |= EMU_BTN_B;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B))          b |= EMU_BTN_C;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  b |= EMU_BTN_X;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_Y))             b |= EMU_BTN_Y;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= EMU_BTN_Z;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_START))      b |= EMU_BTN_START;
    if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK))       b |= EMU_BTN_MODE;

    ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
    ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -STICK_DEADZONE) b |= EMU_BTN_LEFT;
    if (ax >  STICK_DEADZONE) b |= EMU_BTN_RIGHT;
    if (ay < -STICK_DEADZONE) b |= EMU_BTN_UP;
    if (ay >  STICK_DEADZONE) b |= EMU_BTN_DOWN;
  }

  /* The real pad can't press opposite directions at once; some games glitch. */
  if ((b & (EMU_BTN_UP | EMU_BTN_DOWN)) == (EMU_BTN_UP | EMU_BTN_DOWN))    b &= ~EMU_BTN_DOWN;
  if ((b & (EMU_BTN_LEFT | EMU_BTN_RIGHT)) == (EMU_BTN_LEFT | EMU_BTN_RIGHT)) b &= ~EMU_BTN_RIGHT;
  return b;
}
