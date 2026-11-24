/* Used for builds without pbGL (GPU_ACCELERATION=n); SDL software fallback. */
#include "xssh_gpu.h"
int xssh_gpu_init(XsshGpu *gpu,int width,int height){(void)gpu;(void)width;(void)height;return -1;}
void xssh_gpu_present(XsshGpu *gpu,const SDL_Surface *surface){(void)gpu;(void)surface;}
int xssh_gpu_present_saver(XsshGpu *gpu,const uint8_t *pixels,
                           int width,int height,int margin_percent){
    (void)gpu;
    (void)pixels;
    (void)width;
    (void)height;
    (void)margin_percent;
    return 0; /* Let the caller draw the screensaver with SDL. */
}
void xssh_gpu_shutdown(XsshGpu *gpu){(void)gpu;}
