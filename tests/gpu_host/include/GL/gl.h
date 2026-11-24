#ifndef MOCK_GL
#define MOCK_GL
#include <stdint.h>
typedef unsigned GLuint; typedef unsigned GLenum;typedef int GLint;typedef int GLsizei;typedef unsigned char GLboolean;typedef float GLfloat;
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_DEPTH_TEST 0xb71
#define GL_CULL_FACE 0xb44
#define GL_LIGHTING 0xb50
#define GL_BLEND 0xbe2
#define GL_TEXTURE_2D 0xde1
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_REPLACE 0x1e01
#define GL_UNPACK_ALIGNMENT 0xcf5
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_CLAMP 0x2900
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_QUADS 7
int glGetError(void);
void glDeleteTextures(int n,const GLuint*t);
void glViewport(int x,int y,int w,int h);
void glMatrixMode(int mode);
void glLoadIdentity(void);
void glOrtho(double l,double r,double b,double t,double n,double f);
void glDisable(int what);void glEnable(int what);
void glTexEnvi(int a,int b,int c);
void glPixelStorei(int a,int b);
void glGenTextures(int n,GLuint*p);
void glBindTexture(int target,GLuint texture);
void glTexParameteri(int a,int b,int c);
void glTexImage2D(int a,int b,int c,int d,int e,int f,int g,int h,const void*i);
void glClearColor(float a,float b,float c,float d);
void glClear(int mask);
void glTexSubImage2D(int a,int b,int c,int d,int e,int f,int g,int h,const void*i);
void glBegin(int mode);void glTexCoord2f(float u,float v);void glVertex2f(float x,float y);void glEnd(void);
#endif
