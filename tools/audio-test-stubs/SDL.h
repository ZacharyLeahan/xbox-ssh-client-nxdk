#ifndef TEST_SDL_H
#define TEST_SDL_H
#include <stdint.h>
typedef uint32_t Uint32;
Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
#endif
