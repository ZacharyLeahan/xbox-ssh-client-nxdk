#ifndef XSSH_FONT_SIZE_H
#define XSSH_FONT_SIZE_H

/* Font sizes are native pixel glyph heights; the terminal grid follows the video mode. */
#define XSSH_FONT_DEFAULT 12
#define XSSH_FONT_MIN 8
#define XSSH_FONT_MAX 24
#define XSSH_TERM_AREA_WIDTH 640
#define XSSH_TERM_AREA_HEIGHT 288

/* Parse a decimal font height strictly; return zero for invalid settings. */
void xssh_font_layout_for_area(int size,int area_width,int area_height,int *glyph_width,int *glyph_height,int *columns,int *rows);
void xssh_font_layout_for_height(int size,int area_height,int *glyph_width,int *glyph_height,int *columns,int *rows);
int xssh_font_parse(const char *value, int *size);

/* Compute the dimensions of one glyph and of the terminal grid. */
void xssh_font_layout(int size, int *glyph_width, int *glyph_height,
                      int *columns, int *rows);

#endif
