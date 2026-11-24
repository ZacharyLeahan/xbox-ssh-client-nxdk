#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "xssh_gpu.h"
#include <GL/gl.h>
static int uploads=0, frees=0, texes=0, next_tex=1, swaps=0;
#define STUB(name,signature) void name signature {}
int glGetError(void){return GL_NO_ERROR;}
void glDeleteTextures(int n,const GLuint*t){(void)t;frees+=n;}
STUB(glViewport,(int x,int y,int w,int h))
STUB(glMatrixMode,(int mode))
STUB(glLoadIdentity,(void))
STUB(glOrtho,(double l,double r,double b,double t,double n,double f))
STUB(glDisable,(int what))
STUB(glEnable,(int what))
STUB(glTexEnvi,(int a,int b,int c))
STUB(glPixelStorei,(int a,int b))
void glGenTextures(int n,GLuint*p){for(int i=0;i<n;i++)p[i]=next_tex++;texes+=n;}
STUB(glBindTexture,(int t,GLuint x))
STUB(glTexParameteri,(int a,int b,int c))
STUB(glTexImage2D,(int a,int b,int c,int d,int e,int f,int g,int h,const void*i))
STUB(glClearColor,(float a,float b,float c,float d))
STUB(glClear,(int mask))
void glTexSubImage2D(int a,int b,int c,int d,int e,int f,int g,int h,const void*i){(void)i;uploads++;if(e<1||e>256||f<1||f>256)abort();}
STUB(glBegin,(int mode))
STUB(glTexCoord2f,(float u,float v))
STUB(glVertex2f,(float x,float y))
STUB(glEnd,(void))
int pbgl_init(int u){return u?0:-1;}
void pbgl_set_swap_interval(int i){(void)i;}
void pbgl_swap_buffers(void){swaps++;}
void pbgl_shutdown(void){}
int pb_back_buffer_width(void){return 1280;}
int pb_back_buffer_height(void){return 720;}
int main(void){
  XsshGpu gpu;
  if(xssh_gpu_init(&gpu,1280,720))abort();
  SDL_PixelFormat fmt={4};
  SDL_Surface s={1280,720,1280*4,&fmt,NULL};
  s.pixels=calloc((size_t)s.pitch*s.h,1);
  xssh_gpu_present(&gpu,&s);
  if(uploads!=15) {printf("first frame uploads %d, expected 15\n",uploads);return 2;}
  xssh_gpu_present(&gpu,&s);
  if(uploads!=15){printf("unchanged frame unexpectedly uploaded %d\n",uploads);return 3;}
  ((uint8_t*)s.pixels)[(300*1280+300)*4]=127;
  xssh_gpu_present(&gpu,&s);
  if(uploads!=16){printf("single-cell update uploaded %d tiles\n",uploads);return 4;}
  xssh_gpu_shutdown(&gpu);
  if(texes!=15 || frees!=15 || swaps!=3){printf("leak: texes %d frees %d swaps %d\n",texes,frees,swaps);return 5;}
  free(s.pixels);
  printf("PASS: initial upload=15, identical frame=0 uploads, one changed pixel=1 upload, no texture leaks\n");
  return 0;
}
