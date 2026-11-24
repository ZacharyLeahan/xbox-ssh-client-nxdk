/* XSSH NV2A hardware compositor via pbGL (pbkit).
 * CPU text rasterization is retained, while the GPU composites a texture atlas.
 * Never initialize SDL's own video framebuffer/window in this mode: PBKit
 * exclusively owns Xbox scanout. Tile sizes are power-of-two for pbGL.
 */
#include "xssh_gpu.h"
#include <string.h>
#include <stdlib.h>
#define GL_GLEXT_PROTOTYPES
#include <pbgl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <pbkit/pbkit.h>

#define TILE XSSH_GPU_TILE
#define TILE_BYTES ((size_t)TILE*TILE*4u)

/* A fast per-tile checksum avoids redundant CPU-to-GPU copies while
   SDL's persistent software surface retains unchanged terminal pixels. */
static uint32_t tile_hash(const uint8_t *src,int pitch,int w,int h){
    uint32_t v=2166136261u;
    for(int y=0;y<h;y++){
        const uint32_t *p=(const uint32_t *)(src+(size_t)y*pitch);
        for(int x=0;x<w;x++){
            v^=p[x];v*=16777619u;
        }
    }
    return v;
}

void xssh_gpu_shutdown(XsshGpu *g){
    if(!g)return;
    if(g->initialized){
        for(int i=0;i<g->tiles_x*g->tiles_y;i++){
            GLuint tex=(GLuint)g->tiles[i].texture;
            if(tex)glDeleteTextures(1,&tex);
        }
        if(g->saver_texture){GLuint t=(GLuint)g->saver_texture;glDeleteTextures(1,&t);}
        pbgl_shutdown();
    }
    free(g->saver_pixels);
    free(g->upload_tile);
    memset(g,0,sizeof(*g));
}

int xssh_gpu_init(XsshGpu *g,int width,int height){
    if(!g||width<320||height<200||width>1920||height>1080)return -1;
    memset(g,0,sizeof(*g));
    g->width=width;g->height=height;
    g->tiles_x=(width+TILE-1)/TILE;
    g->tiles_y=(height+TILE-1)/TILE;
    if(g->tiles_x*g->tiles_y>XSSH_GPU_MAX_TILES)return -1;
    g->upload_tile=(uint8_t*)malloc(TILE_BYTES);
    if(!g->upload_tile)return -2;
    memset(g->upload_tile,0,TILE_BYTES);
    /* The Xbox video mode has already been selected by xssh_video_initialize. */
    if(pbgl_init(GL_TRUE)!=0){
        free(g->upload_tile);
        memset(g,0,sizeof(*g));
        return -3;
    }
    g->initialized=1;
    if(pb_back_buffer_width()!=width || pb_back_buffer_height()!=height){
        xssh_gpu_shutdown(g);
        return -4;
    }
    pbgl_set_swap_interval(1);
    glViewport(0,0,width,height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0,(double)width,(double)height,0.0,-1.0,1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    for(int i=0;i<g->tiles_x*g->tiles_y;i++){
        GLuint tex=0;
        glGenTextures(1,&tex);
        if(!tex){xssh_gpu_shutdown(g);return -5;}
        g->tiles[i].texture=tex;
        glBindTexture(GL_TEXTURE_2D,tex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,TILE,TILE,0,
                     GL_RGBA,GL_UNSIGNED_BYTE,g->upload_tile);
        if(glGetError()!=GL_NO_ERROR){
            xssh_gpu_shutdown(g);
            return -6;
        }
    }
    return 0;
}

void xssh_gpu_present(XsshGpu *g,const SDL_Surface *surf){
    if(!g||!g->initialized||!surf||!surf->pixels||
       surf->w!=g->width||surf->h!=g->height||surf->format->BytesPerPixel!=4)return;
    SDL_Surface *surface=(SDL_Surface*)surf;
    if(SDL_MUSTLOCK(surface) && SDL_LockSurface(surface)!=0)return;
    glClearColor(0.f,0.f,0.f,1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    for(int ty=0;ty<g->tiles_y;ty++)for(int tx=0;tx<g->tiles_x;tx++){
        int index=ty*g->tiles_x+tx;
        XsshGpuTile *t=&g->tiles[index];
        int x=tx*TILE,y=ty*TILE;
        int w=g->width-x;if(w>TILE)w=TILE;
        int h=g->height-y;if(h>TILE)h=TILE;
        const uint8_t *pix=(const uint8_t*)surf->pixels+(size_t)y*surf->pitch+x*4;
        uint32_t digest=tile_hash(pix,surf->pitch,w,h);
        glBindTexture(GL_TEXTURE_2D,(GLuint)t->texture);
        if(!t->loaded || t->digest!=digest){
            /* Repack dirty tiles with tight pitch; don't rely on GL row-length
               extensions that some pbGL versions do not implement. */
            for(int r=0;r<h;r++)
                memcpy(g->upload_tile+(size_t)r*w*4,
                       pix+(size_t)r*surf->pitch,(size_t)w*4);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,g->upload_tile);
            t->digest=digest;
            t->loaded=1;
        }
        /* Vertex positions and UVs are integer-pixel aligned. No fractional
           sampling at native 480i/480p/720p/1080i output resolutions. */
        float u=(float)w/(float)TILE,v=(float)h/(float)TILE;
        float left=(float)x,top=(float)y,right=(float)(x+w),bottom=(float)(y+h);
        glBegin(GL_QUADS);
        glTexCoord2f(0.f,0.f);glVertex2f(left,top);
        glTexCoord2f(u,0.f);glVertex2f(right,top);
        glTexCoord2f(u,v);glVertex2f(right,bottom);
        glTexCoord2f(0.f,v);glVertex2f(left,bottom);
        glEnd();
    }
    if(SDL_MUSTLOCK(surface))SDL_UnlockSurface(surface);
    pbgl_swap_buffers();
}

/* The screensaver is at most 640x480 RGB565. Let NV2A scale one texture
   rather than making SDL scale every frame to 720p and then hashing/uploading
   every full-size tile. Upload is one tightly packed 640x480 RGBA image. */
int xssh_gpu_present_saver(XsshGpu *g,const uint8_t *pixels,
                           int width,int height,int margin_percent){
    if(!g||!g->initialized||!pixels||width<1||height<1||
       width>640||height>480)return 0;
    int tw=1,th=1;
    while(tw<width)tw*=2;
    while(th<height)th*=2;
    if(tw>1024||th>512)return 0;
    if(!g->saver_texture||tw!=g->saver_tex_width||th!=g->saver_tex_height){
        if(g->saver_texture){GLuint tex=(GLuint)g->saver_texture;
            glDeleteTextures(1,&tex);g->saver_texture=0;}
        free(g->saver_pixels);g->saver_pixels=NULL;
        g->saver_pixels=(uint8_t*)calloc((size_t)tw*th,4);
        if(!g->saver_pixels)return 0;
        GLuint tex=0;glGenTextures(1,&tex);
        if(!tex)return 0;
        g->saver_texture=tex;
        g->saver_tex_width=tw;g->saver_tex_height=th;
        glBindTexture(GL_TEXTURE_2D,tex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
        /* pbGL implementations may not accept a NULL initial texture. */
        uint8_t *clear_tex=(uint8_t*)calloc((size_t)tw*th,4);
        if(!clear_tex)return 0;
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,tw,th,0,GL_RGBA,
                     GL_UNSIGNED_BYTE,clear_tex);
        free(clear_tex);
        if(glGetError()!=GL_NO_ERROR)return 0;
    }
    /* pbGL/NV2A can corrupt columns when a sub-image upload uses a
       narrower row stride than the power-of-two backing texture. Always
       upload a fully padded, tightly packed POT surface. The allocation is
       retained across frames (no per-frame malloc/free and no row overread).
       Padding stays transparent black for the lifetime of the texture. */
    uint8_t *dst=g->saver_pixels;
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        int i=y*width+x;
        int o=(y*tw+x)*4;
        uint16_t v=(uint16_t)pixels[2*i]|((uint16_t)pixels[2*i+1]<<8);
        dst[o]=(uint8_t)(((v>>11)&31)*255/31);
        dst[o+1]=(uint8_t)(((v>>5)&63)*255/63);
        dst[o+2]=(uint8_t)((v&31)*255/31);
        dst[o+3]=255;
    }
    glBindTexture(GL_TEXTURE_2D,(GLuint)g->saver_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,tw,th,GL_RGBA,GL_UNSIGNED_BYTE,dst);
    if(glGetError()!=GL_NO_ERROR)return 0;
    glClearColor(0.f,0.f,0.f,1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    int mx=g->width*margin_percent/100,my=g->height*margin_percent/100;
    float left=(float)mx,top=(float)my;
    float right=(float)(g->width-mx),bottom=(float)(g->height-my);
    float u=(float)width/tw,v=(float)height/th;
    glBegin(GL_QUADS);
    glTexCoord2f(0.f,0.f);glVertex2f(left,top);
    glTexCoord2f(u,0.f);glVertex2f(right,top);
    glTexCoord2f(u,v);glVertex2f(right,bottom);
    glTexCoord2f(0.f,v);glVertex2f(left,bottom);
    glEnd();
    pbgl_swap_buffers();
    return 1;
}
