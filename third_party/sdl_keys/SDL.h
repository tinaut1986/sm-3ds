/* Stand-in for <SDL.h> in the 3DS build: sm/src/config.c only needs the key codes and
   SDL_GetKeyFromName, which source/main.c defines (no keyboard, every name unknown). */
#ifndef SM3DS_SDL_KEYS_SDL_H
#define SM3DS_SDL_KEYS_SDL_H

#include "SDL_keycode.h"

int SDL_GetKeyFromName(const char *name);

#endif
