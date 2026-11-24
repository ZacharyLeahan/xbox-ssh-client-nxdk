#ifndef SDL_H
#define SDL_H
#include <stdint.h>
typedef struct SDL_PixelFormat{int BytesPerPixel;} SDL_PixelFormat;
typedef struct SDL_Surface{int w,h,pitch;SDL_PixelFormat*format;void*pixels;} SDL_Surface;
#define SDL_MUSTLOCK(s) 0
static inline int SDL_LockSurface(SDL_Surface*s){(void)s;return 0;}
static inline void SDL_UnlockSurface(SDL_Surface*s){(void)s;}
#endif
