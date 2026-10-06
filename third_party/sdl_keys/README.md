# SDL key codes

`SDL_keycode.h` and `SDL_scancode.h` are copied from SDL 2.32 (zlib licence, `LICENSE.txt`),
with `SDL_stdinc.h` replaced by `<stdint.h>`. `SDL.h` is ours. They exist so that
`sm/src/config.c` and `config.h` (snesrev's PC frontend code, which names keys the SDL way)
compile in the 3DS build without SDL itself. The PC build of `sm/` still uses the real SDL.
