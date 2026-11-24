/* Used for builds without pbGL (GPU_ACCELERATION=n); SDL software fallback. */
#include "xssh_gpu.h"
int xssh_gpu_init(XsshGpu *gpu,int width,int height){(void)gpu;(void)width;(void)height;return -1;}
void xssh_gpu_present(XsshGpu *gpu,const SDL_Surface *surface){(void)gpu;(void)surface;}
void xssh_gpu_shutdown(XsshGpu *gpu){(void)gpu;}
