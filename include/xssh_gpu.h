#ifndef XSSH_GPU_H
#define XSSH_GPU_H
#include <stdint.h>
#include <SDL.h>
/* Optional NV2A compositor: SDL rasterizes native pixels in RAM; pbGL/pbkit
   presents that RAM image as 256x256 nearest-neighbor textured quads. */
#define XSSH_GPU_TILE 256
#define XSSH_GPU_MAX_TILES 96

typedef struct {
    unsigned texture;
    uint32_t digest;
    int loaded;
} XsshGpuTile;
typedef struct {
    int initialized;
    int width, height, tiles_x, tiles_y;
    uint8_t *upload_tile;
    unsigned saver_texture;
    uint8_t *saver_pixels;
    int saver_tex_width, saver_tex_height;
    XsshGpuTile tiles[XSSH_GPU_MAX_TILES];
} XsshGpu;

/* Returns 0 only when GPU scanout is active, nonzero otherwise. */
int xssh_gpu_init(XsshGpu *gpu,int width,int height);
/* Surface is a software SDL surface with RGBA bytes, pitch at least width*4. */
void xssh_gpu_present(XsshGpu *gpu,const SDL_Surface *surface);
/* Present RGB565 screensaver directly as a GPU texture. Returns nonzero on
   success; returns zero when the caller must use the SDL compatibility path. */
int xssh_gpu_present_saver(XsshGpu *gpu,const uint8_t *rgb565,
                           int width,int height,int margin_percent);
void xssh_gpu_shutdown(XsshGpu *gpu);
#endif
