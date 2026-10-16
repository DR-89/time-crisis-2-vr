#pragma once
#include <stdbool.h>
#include <SDL2/SDL.h>
/* SDL input plus the optional SteamVR overlay keyboard. */
bool qkeyboard_modal(void);
void qkeyboard_shutdown(void);
void qkeyboard_update(void);
bool qkeyboard_event(const SDL_Event *event);
