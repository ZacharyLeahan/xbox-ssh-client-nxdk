/* XSSH - an SSH2 terminal client for the original Xbox / nxdk.
   Networking: nxdk lwIP; crypto: libssh2 + Mbed TLS.
   Passwords are entered at runtime only; first-use SSH host keys need confirmation. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <SDL.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <libssh2.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include "font8x12.h"
#include "font8x12_aa.h"
#include "unicode_glyphs.h"
#include "terminal.h"
#include "rng.h"
#include "usb_keyboard.h"
#include "video_xbox.h"
#include "network_config.h"
#include "network_xbox.h"
#include "theme.h"
#include "font_size.h"
#include "font_style.h"
#include "connect_form.h"
#include "known_hosts.h"
#include "xssh_build.h"
#include "xssh_led.h"
#include "xssh_saver.h"
#include "xssh_mp3.h"
#include "xssh_drums.h"
#include "xssh_gpu.h"

#define WIDTH g_screen_width
#define HEIGHT g_screen_height
static int g_screen_width=640;
static int g_screen_height=480;
static int g_output_width=640;
static int g_output_height=480;
/* The keyboard and status/footer remain anchored to the screen bottom. */
#define BOTTOM_OFFSET (HEIGHT-480)
#define GLYPH_W 8
#define GLYPH_H 12
#define SCREEN_Y 24
#define USB_TERMINAL_HEIGHT (HEIGHT-56)
#define PAD_TERMINAL_HEIGHT (HEIGHT-192)
#define KEY_Y (HEIGHT-138)
#define KEY_W 44
#define KEY_H 23
#define KNOWN_HOSTS_FILE "D:\\xssh-known-hosts.txt"
#define TX_CAP 4096
#define SSH_READ_BURST_MAX 2
#define SSH_READ_TIME_BUDGET_MS 2
#define SSH_KEEPALIVE_INTERVAL_SECONDS 15
#define SSH_KEEPALIVE_POLL_MS 1000u
#define SSH_TX_STALL_NOTICE_MS 5000u
#define SSH_TX_RETRY_INTERVAL_MS 20u /* fallback for libssh2 buffered progress */
#define CONFIG_FILE "D:\\xssh.cfg"
#define SAVER_DEFAULT_FILE "D:\\screensaver.xsv"
#define WALLPAPER_DIR "D:\\wallpapers"

typedef enum { ST_FORM, ST_VERIFY_HOST, ST_CONNECTED, ST_FAILED, ST_POWER_CONFIRM } Stage;
typedef struct {
    char host[80];
    int port;
    char user[80];
    char key_file[XSSH_KEY_PATH_MAX];
    char pin[65];
    char video_mode[48];
    XsshTheme theme;
    int theme_config_valid;
    int font_size;
    int font_style;
    int text_antialiasing;
    int font_config_valid;
    XsshNetworkConfig network;
    int network_config_valid;
    XsshLedMode led_mode;
    int led_valid;
    int screensaver_enabled;
    int screensaver_seconds;
    char screensaver_file[120];
    char wallpaper_file[256];
    int wallpaper_opacity;
    int wallpaper_margin;
    int margin_effects_wallpaper;
    int gpu_acceleration;
} Settings;
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Surface *gpu_surface;
    XsshGpu gpu;
    int gpu_active;
    int last_cursor_x, last_cursor_y, last_cursor_visible;
    SDL_Texture *font;
    SDL_Texture *font_aa;
    SDL_Texture *term_cache;
    uint32_t cached_cells[TERM_MAX_ROWS][TERM_MAX_COLS];
    unsigned char cached_attrs[TERM_MAX_ROWS][TERM_MAX_COLS];
    uint32_t cached_fg[TERM_MAX_ROWS][TERM_MAX_COLS];
    uint32_t cached_bg[TERM_MAX_ROWS][TERM_MAX_COLS];
    int cache_valid;
    unsigned int cache_scan_cursor;
    int cache_scan_active;
    int cache_scan_again;
    int term_height;
    int font_resize_pending;
    int last_keyboard_present;
    unsigned int last_render_ms;
    Stage last_rendered_stage; /* declared after Stage typedef */
    SDL_GameController *pad;
    Terminal term;
    int terminal_glyph_w, terminal_glyph_h;
    Settings settings;
    XsshVideoActive video;
    XsshNetworkState network;
    unsigned int last_network_poll;
    Stage stage;
    XsshConnectForm form;
    char pending_hostkey[65];
    int shift;
    int key_col, key_row;
    char status[160];
    char volume_status[40];
    char ram_status[24];
    char cpu_status[24];
    uint64_t cpu_busy_ticks;
    uint64_t cpu_total_ticks;
    uint64_t cpu_sample_start_ticks;
    unsigned int volume_status_until_ms;
    unsigned int last_ram_poll_ms;
    int ui_status_dirty;
    int sock;
    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    unsigned char outgoing[TX_CAP];
    size_t tx_len;
    unsigned int ssh_last_tx_progress_ms;
    unsigned int ssh_last_keepalive_poll_ms;
    int ssh_tx_stall_reported;
    unsigned int ssh_tx_queued_ms;
    unsigned int ssh_tx_last_attempt_ms;
    unsigned int ssh_tx_eagain_count;
    unsigned int ssh_tx_partial_count;
    int ssh_tx_waiting;
    unsigned int ssh_tx_queued_since_ms;
    unsigned int ssh_tx_last_delay_ms;
    unsigned int ssh_tx_max_delay_ms;
    unsigned int pad_old;
    int wait_for_port_one;
    int running;
    int ready;
    int menu_focus; /* 0=form fields, 1=startup actions */
    int menu_action; /* 0=connect, 1=exit, 2=reboot, 3=shutdown */
    int power_action;
    uint8_t last_led_pattern;
    int led_pattern_valid;
    unsigned int last_input_ms;
    unsigned int saver_start_ms;
    unsigned int saver_last_frame;
    int saver_active;
    XsshSaver saver;
    XsshMp3 music;
    XsshDrums drums;
    SDL_Texture *wallpaper_texture;
    int wallpaper_width,wallpaper_height;
    char wallpaper_current[256];
    int wallpaper_opacity;
    int wallpaper_margin;
    int margin_effects_wallpaper;
    int wallpaper_hold;
    unsigned int wallpaper_next_step;
    SDL_Texture *saver_texture;
    uint32_t *saver_rgba_buffer;
    int saver_rgb565_native;
    int swallow_wake_text;
    int swallow_drum_text;
} App;

/* Forward declaration: startup power actions call this before its definition. */
static void display_margin_clear(App *a);
static void display_margin_apply(App *a);
static void resize_for_keyboard(App *a,int force);
static void disconnect(App *a);

static const char *keyboard[4] = {
    "1234567890-=", "qwertyuiop[]\\", "asdfghjkl;'", "zxcvbnm,./"
};
static const char *keyboard_shift[4] = {
    "!@#$%^&*()_+", "QWERTYUIOP{}|", "ASDFGHJKL:\"", "ZXCVBNM<>?"
};
static const char *special[6] = {"SPACE","ENTER","BACK","TAB","ESC","CTRL-C"};
static void logline(App *a,const char *s) {
    term_feed(&a->term,(const unsigned char*)s,(unsigned int)strlen(s));
    term_feed(&a->term,(const unsigned char*)"\r\n",2);
}
static void status(App *a,const char *s) {
    snprintf(a->status,sizeof(a->status),"%s",s);
    logline(a,s);
}
/* Local UI notifications should never contaminate the SSH terminal grid.
   Music controls update only the fixed bottom status line. */
static void status_bar_only(App *a,const char *s) {
    snprintf(a->status,sizeof(a->status),"%s",s);
    a->ui_status_dirty=1;
}
static char *trim(char *s) {
    while(*s==' '||*s=='\t')s++;
    size_t n=strlen(s);
    while(n&&(s[n-1]=='\r'||s[n-1]=='\n'||s[n-1]==' '||s[n-1]=='\t'))s[--n]=0;
    return s;
}
static int load_settings(Settings *s) {
    memset(s,0,sizeof(*s));
    s->port=22;
    s->theme=XSSH_THEME_XBOX;
    s->theme_config_valid=1;
    s->font_size=XSSH_FONT_DEFAULT;
    s->font_style=XSSH_FONT_DEFAULT_STYLE;
    s->text_antialiasing=0;
    s->font_config_valid=1;
    xssh_network_default(&s->network);
    s->network_config_valid=1;
    s->led_mode=XSSH_LED_AUTO;
    s->led_valid=1;
    s->screensaver_enabled=1;
    s->screensaver_seconds=120;
    s->wallpaper_opacity=50;
    s->wallpaper_margin=0;
    s->margin_effects_wallpaper=0;
    s->gpu_acceleration=1;
    snprintf(s->screensaver_file,sizeof(s->screensaver_file),"%s",SAVER_DEFAULT_FILE);
    snprintf(s->video_mode,sizeof(s->video_mode),"auto");
    FILE *f=fopen(CONFIG_FILE,"rb");
    if(!f)goto settings_done; /* Interactive connection form does not need server defaults. */
    char line[512];
    while(fgets(line,sizeof(line),f)) {
        char *p=trim(line);if(*p=='#'||!*p)continue;
        char *eq=strchr(p,'=');if(!eq)continue;*eq++=0;
        p=trim(p);eq=trim(eq);
        if(!strcmp(p,"host"))snprintf(s->host,sizeof(s->host),"%s",eq);
        else if(!strcmp(p,"user"))snprintf(s->user,sizeof(s->user),"%s",eq);
        else if(!strcmp(p,"private_key"))snprintf(s->key_file,sizeof(s->key_file),"%s",eq);
        else if(!strcmp(p,"port"))s->port=atoi(eq);
        else if(!strcmp(p,"hostkey_sha256"))snprintf(s->pin,sizeof(s->pin),"%s",eq);
        else if(!strcmp(p,"video_mode"))snprintf(s->video_mode,sizeof(s->video_mode),"%s",eq);
        else if(!strcmp(p,"font_style")){
            if(!xssh_font_style_parse(eq,&s->font_style))s->font_config_valid=0;
        }
        else if(!strcmp(p,"text_antialiasing")){
            if(!strcmp(eq,"yes")||!strcmp(eq,"true")||!strcmp(eq,"1"))s->text_antialiasing=1;
            else if(!strcmp(eq,"no")||!strcmp(eq,"false")||!strcmp(eq,"0"))s->text_antialiasing=0;
            else s->font_config_valid=0;
        }
        else if(!strcmp(p,"font_size")){
            if(!xssh_font_parse(eq,&s->font_size))s->font_config_valid=0;
        }
        else if(!strcmp(p,"theme")){
            if(!xssh_theme_parse(eq,&s->theme))s->theme_config_valid=0;
        }
        else if(!strcmp(p,"network_mode")) {
            if(!xssh_network_parse_mode(eq,&s->network.mode))s->network_config_valid=0;
        }
        else if(!strcmp(p,"network_ip"))snprintf(s->network.ip,sizeof(s->network.ip),"%s",eq);
        else if(!strcmp(p,"network_netmask"))snprintf(s->network.netmask,sizeof(s->network.netmask),"%s",eq);
        else if(!strcmp(p,"network_gateway"))snprintf(s->network.gateway,sizeof(s->network.gateway),"%s",eq);
        else if(!strcmp(p,"led_mode")){if(!xssh_led_parse(eq,&s->led_mode))s->led_valid=0;}
        else if(!strcmp(p,"screensaver_enabled")){
            if(!strcmp(eq,"yes")||!strcmp(eq,"true")||!strcmp(eq,"1"))s->screensaver_enabled=1;
            else if(!strcmp(eq,"no")||!strcmp(eq,"false")||!strcmp(eq,"0"))s->screensaver_enabled=0;
            else s->led_valid=0;
        }
        else if(!strcmp(p,"screensaver_seconds")){
            int v=atoi(eq);if(v>=10 && v<=3600)s->screensaver_seconds=v;
            else s->led_valid=0;
        }
        else if(!strcmp(p,"screensaver_file"))snprintf(s->screensaver_file,sizeof(s->screensaver_file),"%s",eq);
        else if(!strcmp(p,"wallpaper"))snprintf(s->wallpaper_file,sizeof(s->wallpaper_file),"%s",eq);
        else if(!strcmp(p,"wallpaper_opacity")){
            char *end=NULL;long v=strtol(eq,&end,10);
            if(end!=eq && !*end && v>=0 && v<=100)s->wallpaper_opacity=(int)v;
        }
        else if(!strcmp(p,"gpu_acceleration")){
            if(!strcmp(eq,"true") || !strcmp(eq,"yes") || !strcmp(eq,"1"))s->gpu_acceleration=1;
            else if(!strcmp(eq,"false") || !strcmp(eq,"no") || !strcmp(eq,"0"))s->gpu_acceleration=0;
        }
        else if(!strcmp(p,"margin_effects_wallpaper")){
            if(!strcmp(eq,"true") || !strcmp(eq,"1"))s->margin_effects_wallpaper=1;
            else if(!strcmp(eq,"false") || !strcmp(eq,"0"))s->margin_effects_wallpaper=0;
        }
        else if(!strcmp(p,"wallpaper_margin")){
            char *end=NULL;long v=strtol(eq,&end,10);
            if(end!=eq && !*end && v>=0 && v<=25 && v%5==0)s->wallpaper_margin=(int)v;
        }
    }
    fclose(f);
settings_done:
    if(!s->led_valid)return -5;
    if(!s->theme_config_valid)return -3;
    if(!s->font_config_valid)return -4;
    /* Do not silently use DHCP if the user explicitly configured invalid
       static settings; that could put credentials on an unintended network. */
    if(!s->network_config_valid || !xssh_network_validate(&s->network,NULL,0))return -2;
    return 0;
}
static int init_font(App *a) {
    SDL_Surface *s=SDL_CreateRGBSurface(0,16*8,6*12*XSSH_FONT_STYLES,32,
                                       0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
    if(!s)return -1;
    uint32_t white=SDL_MapRGBA(s->format,255,255,255,255);
    uint32_t blank=SDL_MapRGBA(s->format,255,255,255,0);
    if(SDL_MUSTLOCK(s))SDL_LockSurface(s);
    for(int y=0;y<s->h;y++){
        uint32_t *dst=(uint32_t*)((uint8_t*)s->pixels+y*s->pitch);
        for(int x=0;x<s->w;x++){
            int face=y/(6*12);
            int gy=y%(6*12);
            int glyph=(gy/12)*16+x/8;
            uint8_t bit=(glyph<95 && face<XSSH_FONT_STYLES)?xssh_fonts[face][glyph][gy%12]:0;
            dst[x]=(bit & (0x80u>>(x%8)))?white:blank;
        }
    }
    if(SDL_MUSTLOCK(s))SDL_UnlockSurface(s);
    a->font=SDL_CreateTextureFromSurface(a->renderer,s);
    SDL_FreeSurface(s);
    if(!a->font)return -1;
    SDL_SetTextureBlendMode(a->font,SDL_BLENDMODE_BLEND);
    /* Separate 8-bit coverage atlas: color tint is applied by SDL at draw time.
       The unmodified binary bitmap remains available with AA disabled. */
    s=SDL_CreateRGBSurface(0,16*8,6*12*XSSH_FONT_STYLES,32,
                           0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
    if(!s)return -1;
    if(SDL_MUSTLOCK(s) && SDL_LockSurface(s)!=0){SDL_FreeSurface(s);return -1;}
    for(int y=0;y<s->h;y++){
        uint32_t *dst=(uint32_t*)((uint8_t*)s->pixels+y*s->pitch);
        int face=y/(6*12),gy=y%(6*12),glyph_row=gy/12,py=gy%12;
        for(int x=0;x<s->w;x++){
            int glyph=glyph_row*16+x/8;
            uint8_t alpha=(face<XSSH_FONT_STYLES && glyph<95)
                ? xssh_fonts_aa[face][glyph][py][x%8] : 0;
            dst[x]=SDL_MapRGBA(s->format,255,255,255,alpha);
        }
    }
    if(SDL_MUSTLOCK(s))SDL_UnlockSurface(s);
    a->font_aa=SDL_CreateTextureFromSurface(a->renderer,s);
    SDL_FreeSurface(s);
    if(!a->font_aa)return -1;
    SDL_SetTextureBlendMode(a->font_aa,SDL_BLENDMODE_BLEND);
    return 0;
}
static void rect(App *a,int x,int y,int w,int h,XsshRgb color) {
    SDL_Rect rc={x,y,w,h};
    SDL_SetRenderDrawColor(a->renderer,color.r,color.g,color.b,255);
    SDL_RenderFillRect(a->renderer,&rc);
}
static void outline(App *a,int x,int y,int w,int h,XsshRgb color) {
    SDL_Rect rc={x,y,w,h};
    SDL_SetRenderDrawColor(a->renderer,color.r,color.g,color.b,255);
    SDL_RenderDrawRect(a->renderer,&rc);
}
static void glyph_scaled(App *a,int x,int y,char c,XsshRgb color,int w,int h) {
    if(c<32||c>126)return;
    int n=(int)c-32;
    SDL_Rect src={(n%16)*8,(n/16)*12+a->settings.font_style*6*12,8,12};
    SDL_Rect dst={x,y,w,h};
    SDL_Texture *atlas=a->settings.text_antialiasing?a->font_aa:a->font;
    SDL_SetTextureColorMod(atlas,color.r,color.g,color.b);
    SDL_RenderCopy(a->renderer,atlas,&src,&dst);
}
static void glyph(App *a,int x,int y,char c,XsshRgb color) {
    glyph_scaled(a,x,y,c,color,GLYPH_W,GLYPH_H);
}
static void text_at(App *a,int x,int y,const char *s,XsshRgb color,int max) {
    for(int i=0;s[i]&&i<max;i++)glyph(a,x+i*8,y,s[i],color);
}
static int key_index(const App*a) {
    if(a->key_row==4) {
        int c=a->key_col;
        if(c<3)return 0; if(c<6)return 1;if(c<8)return 2;
        if(c<10)return 3;if(c<12)return 4;return 5;
    }
    return a->key_col;
}
static void keyboard_key(App *a,int x,int y,int w,int h,int selected,XsshThemeColors c) {
    /* A selected key swaps the two theme colors, without introducing any
       third accent color. Unselected keys get an outline for separation. */
    rect(a,x,y,w,h,selected?c.foreground:c.background);
    if(!selected)outline(a,x,y,w,h,c.foreground);
}
static void box_glyph(App *a,int px,int py,int w,int h,unsigned char ch,XsshRgb c){
    int mx=px+w/2,my=py+h/2;
    int left=(ch==TERM_HLINE||ch==TERM_CROSS||ch==TERM_UR||ch==TERM_LR||ch==TERM_TLEFT||ch==TERM_TTOP||ch==TERM_TBOT);
    int right=(ch==TERM_HLINE||ch==TERM_CROSS||ch==TERM_UL||ch==TERM_LL||ch==TERM_TRIGHT||ch==TERM_TTOP||ch==TERM_TBOT);
    int up=(ch==TERM_VLINE||ch==TERM_CROSS||ch==TERM_LL||ch==TERM_LR||ch==TERM_TLEFT||ch==TERM_TRIGHT||ch==TERM_TBOT);
    int down=(ch==TERM_VLINE||ch==TERM_CROSS||ch==TERM_UL||ch==TERM_UR||ch==TERM_TLEFT||ch==TERM_TRIGHT||ch==TERM_TTOP);
    SDL_SetRenderDrawColor(a->renderer,c.r,c.g,c.b,255);
    if(left)SDL_RenderDrawLine(a->renderer,px,my,mx,my);
    if(right)SDL_RenderDrawLine(a->renderer,mx,my,px+w-1,my);
    if(up)SDL_RenderDrawLine(a->renderer,mx,py,mx,my);
    if(down)SDL_RenderDrawLine(a->renderer,mx,my,mx,py+h-1);
}
/* Codepoint renderer: btop uses UTF-8 Braille graphs (U+2800..28FF),
 * block fractions (U+2580..259F), and Unicode line art. */
static void unicode_glyph(App *a,int px,int py,int w,int h,uint32_t cp,XsshRgb c){
    SDL_SetRenderDrawColor(a->renderer,c.r,c.g,c.b,255);
    if(cp>=0x2800 && cp<=0x28ff){
        const unsigned bits=cp-0x2800;
        /* Unicode Braille positions: left 1,2,3,7; right 4,5,6,8. */
        const unsigned masks[2][4]={{1,2,4,64},{8,16,32,128}};
        int dw=w/4;if(dw<1)dw=1;
        int dh=h/8;if(dh<1)dh=1;
        for(int col=0;col<2;col++)for(int row=0;row<4;row++){
            if(bits & masks[col][row]){
                SDL_Rect dot={px+(col?3*w/4:w/4)-dw/2,
                              py+(row*2+1)*h/8-dh/2,dw,dh};
                SDL_RenderFillRect(a->renderer,&dot);
            }
        }
        return;
    }
    if(cp>=0x2580 && cp<=0x259f){
        if(cp==0x2588){SDL_Rect r={px,py,w,h};SDL_RenderFillRect(a->renderer,&r);return;}
        if(cp==0x2580||cp==0x2584){SDL_Rect r={px,py+(cp==0x2584?h/2:0),w,h/2};SDL_RenderFillRect(a->renderer,&r);return;}
        /* Block elements. Partition each cell into an 8x8 microgrid. */
        const unsigned quadrants[10]={4,8,1,13,9,7,11,2,6,14};
        for(int ry=0;ry<8;ry++)for(int rx=0;rx<8;rx++){
            int on=0;
            if(cp==0x2580)on=(ry<4);
            else if(cp>=0x2581&&cp<=0x2587)on=(ry>=8-(int)(cp-0x2580));
            else if(cp==0x2588)on=1;
            else if(cp>=0x2589&&cp<=0x258f)on=(rx<(int)(0x2590-cp));
            else if(cp==0x2590)on=(rx>=4);
            else if(cp>=0x2591&&cp<=0x2593){int v=(int)(cp-0x2590);on=((rx+2*ry)%4)<v;}
            else if(cp==0x2594)on=(ry==0);
            else if(cp==0x2595)on=(rx==7);
            else if(cp>=0x2596){int q=(int)(cp-0x2596);int mask=quadrants[q];int quadrant=(ry<4?0:2)+(rx<4?0:1);on=(mask>>quadrant)&1;}
            if(on){
                int x0=px+rx*w/8,x1=px+(rx+1)*w/8;
                int y0=py+ry*h/8,y1=py+(ry+1)*h/8;
                SDL_Rect r={x0,y0,x1>x0?x1-x0:1,y1>y0?y1-y0:1};
                SDL_RenderFillRect(a->renderer,&r);
            }
        }
        return;
    }
    /* Additional line variants render as consistent straight line segments. */
    if(cp>=0x2500&&cp<=0x257f){
        int midx=px+w/2,midy=py+h/2;
        if(cp==0x2500||cp==0x2501||cp==0x2504||cp==0x2505||cp==0x2508||cp==0x2509||cp==0x254c||cp==0x254d)
            SDL_RenderDrawLine(a->renderer,px,midy,px+w-1,midy);
        else if(cp==0x2502||cp==0x2503||cp==0x2506||cp==0x2507||cp==0x250a||cp==0x250b||cp==0x254e||cp==0x254f)
            SDL_RenderDrawLine(a->renderer,midx,py,midx,py+h-1);
        else { SDL_RenderDrawLine(a->renderer,px,midy,px+w-1,midy);
               SDL_RenderDrawLine(a->renderer,midx,py,midx,py+h-1); }
        return;
    }
    /* A small pre-rasterized supplemental Unicode atlas for arrows/symbols. */
    for(unsigned i=0;i<XSSH_UNICODE_GLYPHS;i++)if(xssh_unicode_points[i]==cp){
        const uint8_t *bitmap=xssh_unicode_bits[i];
        for(int gy=0;gy<12;gy++)for(int gx=0;gx<8;gx++)if(bitmap[gy]&(0x80u>>gx)){
            int x0=px+gx*w/8,x1=px+(gx+1)*w/8;
            int y0=py+gy*h/12,y1=py+(gy+1)*h/12;
            SDL_Rect r={x0,y0,x1>x0?x1-x0:1,y1>y0?y1-y0:1};
            SDL_RenderFillRect(a->renderer,&r);
        }
        return;
    }
    glyph_scaled(a,px,py,'?',c,w,h);
}
static void render_terminal_glyph(App *a,int px,int py,int w,int h,uint32_t ch,XsshRgb fg){
    if(ch>=TERM_HLINE&&ch<=TERM_TBOT)box_glyph(a,px,py,w,h,(unsigned char)ch,fg);
    else if(ch>=32&&ch<=126)glyph_scaled(a,px,py,(char)ch,fg,w,h);
    else if(ch>126)unicode_glyph(a,px,py,w,h,ch,fg);
}
/* Render the wallpaper only where the requested cell intersects the inset image.
   The logical wallpaper destination is shared by the full-screen and cached paths. */
static SDL_Rect wallpaper_bounds(const App *a){
    if(a->margin_effects_wallpaper)return (SDL_Rect){0,0,WIDTH,HEIGHT};
    /* Physical full-screen wallpaper, expressed in the inset viewport's
       unscaled local pixel coordinates. */
    int mx=g_output_width*a->wallpaper_margin/100;
    int my=g_output_height*a->wallpaper_margin/100;
    return (SDL_Rect){-mx,-my,g_output_width,g_output_height};
}
static void wallpaper_draw_region(App *a,int screen_x,int screen_y,int width,int height,int target_x,int target_y){
    if(!a->wallpaper_texture || a->wallpaper_opacity<=0)return;
    SDL_Rect b=wallpaper_bounds(a);
    int x0=screen_x>b.x?screen_x:b.x;
    int y0=screen_y>b.y?screen_y:b.y;
    int x1=screen_x+width<b.x+b.w?screen_x+width:b.x+b.w;
    int y1=screen_y+height<b.y+b.h?screen_y+height:b.y+b.h;
    if(x1<=x0 || y1<=y0)return;
    SDL_Rect src={
        (x0-b.x)*a->wallpaper_width/b.w,
        (y0-b.y)*a->wallpaper_height/b.h,
        (x1-b.x)*a->wallpaper_width/b.w-(x0-b.x)*a->wallpaper_width/b.w,
        (y1-b.y)*a->wallpaper_height/b.h-(y0-b.y)*a->wallpaper_height/b.h
    };
    SDL_Rect dst={target_x+x0-screen_x,target_y+y0-screen_y,x1-x0,y1-y0};
    if(src.w<1)src.w=1;
    if(src.h<1)src.h=1;
    SDL_SetTextureAlphaMod(a->wallpaper_texture,(Uint8)(a->wallpaper_opacity*255/100));
    SDL_RenderCopy(a->renderer,a->wallpaper_texture,&src,&dst);
}
static void draw_terminal_cell(App *a,int x,int y,XsshThemeColors c){
    int px=x*a->terminal_glyph_w,py=y*a->terminal_glyph_h;
    int output_y=py+(a->gpu_active?SCREEN_Y:0);
    uint32_t ch=a->term.cells[y][x];
    int rev=a->term.attr[y][x]&1;
    XsshRgb fg=xssh_theme_terminal_color(a->settings.theme,a->term.fg[y][x]);
    XsshRgb bg=xssh_theme_terminal_background(a->settings.theme,a->term.bg[y][x]);
    if(rev){XsshRgb tmp=fg;fg=bg;bg=tmp;}
    rect(a,px,output_y,a->terminal_glyph_w,a->terminal_glyph_h,bg);
    wallpaper_draw_region(a,px,py+SCREEN_Y,a->terminal_glyph_w,a->terminal_glyph_h,px,output_y);
    if(ch!=' ')render_terminal_glyph(a,px,output_y,a->terminal_glyph_w,a->terminal_glyph_h,ch,fg);
}
/* Bounds the amount of glyph work in each frame. A continuously updating
   terminal (cmatrix/btop) must not monopolize the Xbox on screensaver exit. */
#define TERMINAL_REDRAW_BUDGET 320u
#define TERMINAL_REDRAW_BUDGET_MUSIC 640u
/* Render into an SDL target texture only when cells change. If the nxdk SDL2
   backend cannot target textures, draw directly as a compatibility fallback. */
static void draw_terminal(App *a,XsshThemeColors c){
    if(a->gpu_active){
        /* GPU presentation mode: SDL writes bitmap glyphs directly into a
           persistent, native-resolution CPU surface. Repaint only cells that
           changed; the NV2A GPU then blits only changed surface tiles. */
        if(!a->cache_valid){
            for(int y=0;y<a->term.rows;y++)for(int x=0;x<a->term.cols;x++)
                a->cached_cells[y][x]=UINT32_MAX;
            a->cache_valid=1;
            a->cache_scan_cursor=0;
            a->cache_scan_active=1;
            a->cache_scan_again=0;
            a->last_cursor_visible=0;
        }
        if(a->term.dirty){
            if(a->cache_scan_active)a->cache_scan_again=1;
            else {a->cache_scan_cursor=0;a->cache_scan_active=1;}
            a->term.dirty=0;
        }
        if(a->cache_scan_active){
            unsigned total=(unsigned)a->term.cols*(unsigned)a->term.rows;
            unsigned budget=xssh_mp3_is_playing(&a->music)?
                TERMINAL_REDRAW_BUDGET_MUSIC:TERMINAL_REDRAW_BUDGET;
            unsigned end=a->cache_scan_cursor+budget;
            if(end>total)end=total;
            for(unsigned i=a->cache_scan_cursor;i<end;i++){
                int y=(int)(i/(unsigned)a->term.cols);
                int x=(int)(i%(unsigned)a->term.cols);
                uint32_t ch=a->term.cells[y][x],fg=a->term.fg[y][x],bg=a->term.bg[y][x];
                unsigned char attr=a->term.attr[y][x];
                if(a->cached_cells[y][x]!=ch || a->cached_fg[y][x]!=fg ||
                   a->cached_bg[y][x]!=bg || a->cached_attrs[y][x]!=attr){
                    draw_terminal_cell(a,x,y,c);
                    a->cached_cells[y][x]=ch;a->cached_fg[y][x]=fg;
                    a->cached_bg[y][x]=bg;a->cached_attrs[y][x]=attr;
                }
            }
            a->cache_scan_cursor=end;
            if(end==total){
                a->cache_scan_cursor=0;
                if(a->cache_scan_again)a->cache_scan_again=0;
                else a->cache_scan_active=0;
            }
        }
        /* Erase the previous cursor before moving it, even if no cell data
           changed. This avoids leaving a permanent underscore behind. */
        if(a->last_cursor_visible && a->last_cursor_x>=0 && a->last_cursor_y>=0 &&
           a->last_cursor_x<a->term.cols && a->last_cursor_y<a->term.rows)
            draw_terminal_cell(a,a->last_cursor_x,a->last_cursor_y,c);
        a->last_cursor_visible=0;
        if(a->term.cursor_visible && a->term.x<a->term.cols && a->term.y<a->term.rows){
            int h=a->terminal_glyph_h>=12?2:1;
            rect(a,a->term.x*a->terminal_glyph_w,
                SCREEN_Y+(a->term.y+1)*a->terminal_glyph_h-h,
                a->terminal_glyph_w,h,c.foreground);
            a->last_cursor_visible=1;
            a->last_cursor_x=a->term.x;
            a->last_cursor_y=a->term.y;
        }
        return;
    }
    int cache_ready=a->term_cache!=NULL;
    if(cache_ready && SDL_SetRenderTarget(a->renderer,a->term_cache)!=0){
        SDL_DestroyTexture(a->term_cache);a->term_cache=NULL;
        a->cache_valid=0;cache_ready=0;
        a->cache_scan_active=0;a->cache_scan_again=0;
    }
    if(cache_ready){
        /* Render-target textures must use their own full, unscaled viewport. */
        SDL_RenderSetScale(a->renderer,1.0f,1.0f);
        SDL_RenderSetViewport(a->renderer,NULL);
        if(!a->cache_valid){
            /* A real atlas/font/layout change requires a rebuild, unlike
               screensaver dismissal which leaves the cache intact. */
            SDL_SetRenderDrawColor(a->renderer,c.background.r,c.background.g,c.background.b,255);
            SDL_RenderClear(a->renderer);
            for(int y=0;y<a->term.rows;y++)
                for(int x=0;x<a->term.cols;x++)a->cached_cells[y][x]=UINT32_MAX;
            a->cache_valid=1;
            a->cache_scan_cursor=0;
            a->cache_scan_active=1;
            a->cache_scan_again=0;
        }
        /* Incoming output can arrive during a multi-frame scan. Remember it
           and do a further pass so early cells can't be left outdated. */
        if(a->term.dirty){
            if(a->cache_scan_active)a->cache_scan_again=1;
            else {a->cache_scan_cursor=0;a->cache_scan_active=1;}
            a->term.dirty=0;
        }
        if(a->cache_scan_active){
            unsigned int total=(unsigned int)a->term.cols*(unsigned int)a->term.rows;
            /* Keep full-screen applications responsive even while MP3 is active.
               Bound the per-pass work rather than starving the renderer. */
            unsigned int budget=xssh_mp3_is_playing(&a->music)
                ? TERMINAL_REDRAW_BUDGET_MUSIC : TERMINAL_REDRAW_BUDGET;
            unsigned int end=a->cache_scan_cursor+budget;
            if(end>total)end=total;
            for(unsigned int i=a->cache_scan_cursor;i<end;i++){
                int y=(int)(i/(unsigned int)a->term.cols);
                int x=(int)(i%(unsigned int)a->term.cols);
                uint32_t ch=a->term.cells[y][x],fg=a->term.fg[y][x],bg=a->term.bg[y][x];
                unsigned char attr=a->term.attr[y][x];
                if(a->cached_cells[y][x]!=ch || a->cached_fg[y][x]!=fg ||
                   a->cached_bg[y][x]!=bg || a->cached_attrs[y][x]!=attr){
                    draw_terminal_cell(a,x,y,c);
                    a->cached_cells[y][x]=ch;a->cached_fg[y][x]=fg;
                    a->cached_bg[y][x]=bg;a->cached_attrs[y][x]=attr;
                }
            }
            a->cache_scan_cursor=end;
            if(end==total){
                a->cache_scan_cursor=0;
                if(a->cache_scan_again){a->cache_scan_again=0;}
                else a->cache_scan_active=0;
            }
        }
        SDL_SetRenderTarget(a->renderer,NULL);
        display_margin_apply(a);
        SDL_Rect from={0,0,WIDTH,a->term_height},to={0,SCREEN_Y,WIDTH,a->term_height};
        SDL_RenderCopy(a->renderer,a->term_cache,&from,&to);
    } else {
        for(int y=0;y<a->term.rows;y++)for(int x=0;x<a->term.cols;x++){
            /* Fallback: draw in target coordinates rather than cache coordinates. */
            int px=x*a->terminal_glyph_w,py=SCREEN_Y+y*a->terminal_glyph_h;
            uint32_t ch=a->term.cells[y][x];
            int rev=a->term.attr[y][x]&1;
            XsshRgb fg=xssh_theme_terminal_color(a->settings.theme,a->term.fg[y][x]);
            XsshRgb bg=xssh_theme_terminal_background(a->settings.theme,a->term.bg[y][x]);
            if(rev){XsshRgb tmp=fg;fg=bg;bg=tmp;}
            rect(a,px,py,a->terminal_glyph_w,a->terminal_glyph_h,bg);
            wallpaper_draw_region(a,px,py,a->terminal_glyph_w,a->terminal_glyph_h,px,py);
            if(ch!=' ')render_terminal_glyph(a,px,py,a->terminal_glyph_w,a->terminal_glyph_h,ch,fg);
        }
        a->term.dirty=0;
    }
    if(a->term.cursor_visible){
        int h=a->terminal_glyph_h>=12?2:1;
        rect(a,a->term.x*a->terminal_glyph_w,SCREEN_Y+(a->term.y+1)*a->terminal_glyph_h-h,
             a->terminal_glyph_w,h,c.foreground);
    }
}
static void resize_for_keyboard(App *a,int force){
    int now=xssh_usb_keyboard_connected()!=0;
    if(!force && !a->font_resize_pending && now==a->last_keyboard_present)return;
    int width,rows;
    a->term_height=now?USB_TERMINAL_HEIGHT:PAD_TERMINAL_HEIGHT;
    xssh_font_layout_for_area(a->settings.font_size,WIDTH,a->term_height,&a->terminal_glyph_w,&a->terminal_glyph_h,&width,&rows);
    if(a->term.cols==width&&a->term.rows==rows){
        a->last_keyboard_present=now;
        a->font_resize_pending=0;
        if(force)a->cache_valid=0;
        return;
    }
    /* The server must receive the new grid so full-screen tools reflow. */
    if(a->channel && a->stage==ST_CONNECTED){
        int r=libssh2_channel_request_pty_size(a->channel,width,rows);
        if(r==LIBSSH2_ERROR_EAGAIN){
            /* Retry on the next pump; don't send an incorrect size. */
            return;
        }
        if(r!=0)return;
    }
    term_set_size(&a->term,width,rows);
    a->last_keyboard_present=now;
    a->cache_valid=0;
    a->font_resize_pending=0;
}
/* Keep the terminal's pixel dimensions and server PTY in sync. */
static void change_font_size(App *a,int delta){
    int next=a->settings.font_size+delta;
    if(next<XSSH_FONT_MIN)next=XSSH_FONT_MIN;
    if(next>XSSH_FONT_MAX)next=XSSH_FONT_MAX;
    if(next==a->settings.font_size)return;
    a->settings.font_size=next;
    a->font_resize_pending=1;
    resize_for_keyboard(a,1);
}
static void change_font_style(App *a){
    a->settings.font_style=(a->settings.font_style+1)%XSSH_FONT_STYLES;
    a->cache_valid=0;
    a->term.dirty=1;
    snprintf(a->status,sizeof(a->status),"Font: %s (Ctrl+F to cycle)",
             xssh_font_style_name(a->settings.font_style));
}
static void startup_action(App *a,int selected){
    if(selected==0){a->menu_focus=0;a->form.field=XSSH_FORM_HOST;return;}
    if(selected==1){a->running=0;return;}
    a->power_action=selected;
    a->stage=ST_POWER_CONFIRM;
    snprintf(a->status,sizeof(a->status),"Press Y to confirm, N or Esc to cancel");
}
static void confirm_power(App *a,int yes){
    if(!yes){a->stage=ST_FORM;return;}
    /* HalReturnToFirmware(0) halts, it does NOT power off this Xbox.
       Power-off is controlled by the SMC at 8-bit SMBus addr 0x20,
       power register 0x02, command 0x80.  Do not issue a reboot
       as a fallback on shutdown failure. */
    const int shutdown=(a->power_action==3);
    disconnect(a);
    if(shutdown){
        NTSTATUS result=HalWriteSMBusValue(0x20,0x02,FALSE,0x80);
        if((int32_t)result<0){
            a->stage=ST_FORM;
            status(a,"SMC power-off command failed; Xbox was NOT rebooted");
            return;
        }
        /* Allow the SMC to remove power before resuming the application.
           A failed power-off is reported, not silently changed to reboot. */
        unsigned int start=SDL_GetTicks();
        while((unsigned)(SDL_GetTicks()-start)<1500)SDL_Delay(10);
        a->stage=ST_FORM;
        status(a,"SMC acknowledged shutdown, but Xbox is still running");
        return;
    }
    if(a->term_cache){SDL_DestroyTexture(a->term_cache);a->term_cache=NULL;}
    if(a->font){SDL_DestroyTexture(a->font);a->font=NULL;}
    if(a->font_aa){SDL_DestroyTexture(a->font_aa);a->font_aa=NULL;}
    if(a->pad){SDL_GameControllerClose(a->pad);a->pad=NULL;}
    if(a->renderer){SDL_DestroyRenderer(a->renderer);a->renderer=NULL;}
    if(a->gpu_active){xssh_gpu_shutdown(&a->gpu);a->gpu_active=0;}
    if(a->gpu_surface){SDL_FreeSurface(a->gpu_surface);a->gpu_surface=NULL;}
    if(a->window){SDL_DestroyWindow(a->window);a->window=NULL;}
    xssh_led_restore();
    SDL_Quit();
    HalReturnToFirmware(1); /* HalRebootRoutine */
    a->running=0;
}
static void update_front_led(App *a){
    int state=a->saver_active?4:a->stage==ST_CONNECTED?1:
              a->stage==ST_VERIFY_HOST?2:a->stage==ST_FAILED?3:0;
    uint8_t pattern=xssh_led_pattern(a->settings.led_mode,state);
    if(!a->led_pattern_valid || a->last_led_pattern!=pattern){
        xssh_led_apply(pattern);
        a->led_pattern_valid=1;a->last_led_pattern=pattern;
    }
}
static void saver_stop(App *a){
    if(!a->saver_active)return;
    if(a->saver_texture){SDL_DestroyTexture(a->saver_texture);a->saver_texture=NULL;}
    free(a->saver_rgba_buffer);a->saver_rgba_buffer=NULL;
    xssh_saver_close(&a->saver);
    a->saver_active=0;
    /* Keep the terminal target texture and its cell snapshot: the screensaver
       only covered it, it did not invalidate it. A dirty pass will reconcile
       the latest cmatrix/vim output incrementally after wake. */
    a->term.dirty=1;
    /* GPU mode draws directly onto the persistent CPU surface. Screensaver
       frames have overwritten those pixels, so the terminal needs rebuilding.
       The original SDL render-target cache is unaffected. */
    if(a->gpu_active)a->cache_valid=0;
    a->last_render_ms=0;
    update_front_led(a);
}
static void saver_start(App *a,unsigned int now){
    if(!a->settings.screensaver_enabled || a->saver_active || a->stage==ST_POWER_CONFIRM ||
       a->stage==ST_VERIFY_HOST)return;
    if(!xssh_saver_open(&a->saver,a->settings.screensaver_file)){
        /* Retry after another timeout, not on every 2ms loop iteration. */
        a->last_input_ms=now;
        return;
    }
    a->saver_rgb565_native=1;
    a->saver_texture=SDL_CreateTexture(a->renderer,SDL_PIXELFORMAT_RGB565,
                       SDL_TEXTUREACCESS_STREAMING,(int)a->saver.width,(int)a->saver.height);
    if(!a->saver_texture){
        /* Some SDL backends expose only 32-bit textures. Conversion is a
           fallback, not required for the normal fast RGB565 path. */
        a->saver_rgb565_native=0;
        a->saver_texture=SDL_CreateTexture(a->renderer,SDL_PIXELFORMAT_RGBA8888,
                           SDL_TEXTUREACCESS_STREAMING,(int)a->saver.width,(int)a->saver.height);
        a->saver_rgba_buffer=(uint32_t*)malloc((size_t)a->saver.width*a->saver.height*4);
        if(!a->saver_rgba_buffer){if(a->saver_texture)SDL_DestroyTexture(a->saver_texture);
            a->saver_texture=NULL;}
    }
    if(!a->saver_texture){free(a->saver_rgba_buffer);a->saver_rgba_buffer=NULL;
        xssh_saver_close(&a->saver);a->last_input_ms=now;return;}
    SDL_SetTextureBlendMode(a->saver_texture,SDL_BLENDMODE_NONE);
    a->saver_start_ms=now;a->saver_last_frame=UINT32_MAX;
    a->saver_active=1;update_front_led(a);
}
static void saver_draw(App *a,unsigned int now){
    if(!a->saver_active)return;
    uint32_t frame=((uint32_t)(now-a->saver_start_ms)*a->saver.fps/1000u)%a->saver.frames;
    /* The GPU retains the previous screensaver image. Do not re-upload an
       identical frame when the render loop polls faster than its FPS. */
    if(a->gpu_active && frame==a->saver_last_frame)return;
    if(frame!=a->saver_last_frame){
        if(!xssh_saver_frame(&a->saver,frame)){saver_stop(a);return;}
        const void *pixels=a->saver.pixels;
        int pitch=(int)a->saver.width*2;
        if(!a->saver_rgb565_native){
            size_t n=(size_t)a->saver.width*a->saver.height;
            for(size_t i=0;i<n;i++){
                uint16_t v=(uint16_t)a->saver.pixels[i*2]|((uint16_t)a->saver.pixels[i*2+1]<<8);
                uint32_t r=(v>>11)&31, g=(v>>5)&63, b=v&31;
                a->saver_rgba_buffer[i]=((r*255/31)<<24)|((g*255/63)<<16)|((b*255/31)<<8)|255;
            }
            pixels=a->saver_rgba_buffer;
            pitch=(int)a->saver.width*4;
        }
        if(!a->gpu_active && SDL_UpdateTexture(a->saver_texture,NULL,pixels,pitch)!=0){
            saver_stop(a);return;
        }
        a->saver_last_frame=frame;
    }
    if(a->gpu_active && xssh_gpu_present_saver(&a->gpu,a->saver.pixels,
             (int)a->saver.width,(int)a->saver.height,a->wallpaper_margin))return;
    if(a->gpu_active){
        const void *fallback_pixels=a->saver_rgb565_native ?
            (const void*)a->saver.pixels:(const void*)a->saver_rgba_buffer;
        int fallback_pitch=(int)a->saver.width*(a->saver_rgb565_native?2:4);
        if(SDL_UpdateTexture(a->saver_texture,NULL,fallback_pixels,fallback_pitch)!=0){
            saver_stop(a);return;
        }
    }
    display_margin_clear(a);
    SDL_Rect dest={0,0,WIDTH,HEIGHT};
    SDL_RenderCopy(a->renderer,a->saver_texture,NULL,&dest);
    if(a->gpu_active){
        /* Flush SDL's software command queue before NV2A reads the pixels. */
        SDL_RenderPresent(a->renderer);
        xssh_gpu_present(&a->gpu,a->gpu_surface);
    }else SDL_RenderPresent(a->renderer);
}
/* Strict 32-bit uncompressed BMP with X8R8G8B8 pixels (little-endian
   B,G,R,unused).  24-bit, alpha-bearing, compressed and other files rejected. */
static unsigned int wall_le16(const unsigned char *b){return (unsigned)b[0]|((unsigned)b[1]<<8);}
static unsigned int wall_le32(const unsigned char *b){return wall_le16(b)|(wall_le16(b+2)<<16);}
static int wallpaper_load(App *a,const char *path){
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char h[54];
    if(fread(h,1,sizeof(h),f)!=sizeof(h)){fclose(f);return 0;}
    unsigned int offset=wall_le32(h+10),dib=wall_le32(h+14);
    int w=(int)wall_le32(h+18),signed_h=(int)wall_le32(h+22);
    if(h[0]!='B'||h[1]!='M'||dib!=40||offset<54||offset>4096||
       w<1||w>1024||signed_h==0||signed_h==INT32_MIN||
       signed_h>768||signed_h< -768||wall_le16(h+26)!=1||
       wall_le16(h+28)!=32||wall_le32(h+30)!=0){fclose(f);return 0;}
    int height=signed_h<0?-signed_h:signed_h;
    size_t bytes=(size_t)w*(size_t)height*4;
    if(fseek(f,0,SEEK_END)!=0 || ftell(f)<(long)(offset+bytes) ||
       fseek(f,(long)offset,SEEK_SET)!=0){fclose(f);return 0;}
    uint32_t *pixels=(uint32_t*)malloc(bytes);
    if(!pixels){fclose(f);return 0;}
    /* Read a row at a time; convert to SDL RGBA8888 with opaque alpha. */
    for(int y=0;y<height;y++){
        int dst_y=signed_h>0?height-1-y:y;
        uint32_t *row=pixels+(size_t)dst_y*w;
        for(int x=0;x<w;x++){
            unsigned char b[4];
            if(fread(b,1,4,f)!=4){free(pixels);fclose(f);return 0;}
            /* X8R8G8B8 requires unused high byte to be zero. */
            if(b[3]!=0){free(pixels);fclose(f);return 0;}
            row[x]=((uint32_t)b[2]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[0]<<8)|255u;
        }
    }
    fclose(f);
    SDL_Texture *texture=SDL_CreateTexture(a->renderer,SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_STATIC,w,height);
    if(!texture){free(pixels);return 0;}
    if(SDL_UpdateTexture(texture,NULL,pixels,w*4)!=0){SDL_DestroyTexture(texture);free(pixels);return 0;}
    free(pixels);
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    if(a->wallpaper_texture)SDL_DestroyTexture(a->wallpaper_texture);
    a->wallpaper_texture=texture;
    a->wallpaper_width=w;a->wallpaper_height=height;
    a->cache_valid=0;
    snprintf(a->wallpaper_current,sizeof(a->wallpaper_current),"%s",path);
    a->last_render_ms=0;
    return 1;
}
static int wallpaper_candidate(const char *name){
    size_t n=strlen(name);
    if(n<5||n>160||name[0]=='.'||strchr(name,'\\')||strchr(name,'/'))return 0;
    const char *ext=name+n-4;
    return (ext[0]=='.' && (ext[1]=='b'||ext[1]=='B') &&
            (ext[2]=='m'||ext[2]=='M') && (ext[3]=='p'||ext[3]=='P'));
}
static void wallpaper_next(App *a){
    WIN32_FIND_DATAA fd;
    HANDLE h=FindFirstFileA(WALLPAPER_DIR "\\*",&fd);
    if(h==INVALID_HANDLE_VALUE)return;
    char next[180]={0},first[180]={0};
    const char *current=strrchr(a->wallpaper_current,'\\');
    current=current?current+1:a->wallpaper_current;
    do{
        const char *name=fd.cFileName;
        if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)||!wallpaper_candidate(name))continue;
        if(!first[0]||strcmp(name,first)<0)snprintf(first,sizeof(first),"%s",name);
        if(strcmp(name,current)>0 && (!next[0]||strcmp(name,next)<0))
            snprintf(next,sizeof(next),"%s",name);
    }while(FindNextFileA(h,&fd));
    FindClose(h);
    if(!next[0])snprintf(next,sizeof(next),"%s",first);
    if(!next[0])return;
    /* Invalid files are never used; scan other candidates in sorted order. */
    char attempted[180]={0};
    for(int i=0;i<256;i++){
        char path[256];snprintf(path,sizeof(path),"%s\\%s",WALLPAPER_DIR,next);
        if(wallpaper_load(a,path))return;
        snprintf(attempted,sizeof(attempted),"%s",next);
        h=FindFirstFileA(WALLPAPER_DIR "\\*",&fd);
        if(h==INVALID_HANDLE_VALUE)return;
        char higher[180]={0},lowest[180]={0};
        do{
            const char *name=fd.cFileName;
            if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)||!wallpaper_candidate(name))continue;
            if(!lowest[0]||strcmp(name,lowest)<0)snprintf(lowest,sizeof(lowest),"%s",name);
            if(strcmp(name,attempted)>0&&(!higher[0]||strcmp(name,higher)<0))
                snprintf(higher,sizeof(higher),"%s",name);
        }while(FindNextFileA(h,&fd));
        FindClose(h);
        snprintf(next,sizeof(next),"%s",higher[0]?higher:lowest);
        if(!next[0]||strcmp(next,attempted)==0)break;
    }
}
static void wallpaper_step(App *a){
    a->wallpaper_opacity=(a->wallpaper_opacity+1)%101;
    /* Show the current level on the bottom status bar on every step,
       including the gradual updates while Ctrl+Shift+O is held. */
    char msg[64];
    snprintf(msg,sizeof(msg),"Wallpaper opacity: %d%%",a->wallpaper_opacity);
    status_bar_only(a,msg);
    a->cache_valid=0;
    a->last_render_ms=0;
}
static void wallpaper_margin_step(App *a){
    a->wallpaper_margin=(a->wallpaper_margin+5)%30;
    /* Inset the UI at native pixel size; never resample bitmap glyphs. */
    g_screen_width=g_output_width-2*(g_output_width*a->wallpaper_margin/100);
    g_screen_height=g_output_height-2*(g_output_height*a->wallpaper_margin/100);
    a->font_resize_pending=1;
    resize_for_keyboard(a,1);
    char msg[64];
    snprintf(msg,sizeof(msg),"Wallpaper margin: %d%%",a->wallpaper_margin);
    status_bar_only(a,msg);
    a->cache_valid=0;
    a->last_render_ms=0;
}
/* The margin is a physical-pixel inset, NOT a fractional scaling transform.
   Text is drawn at 1:1 native pixel size to avoid blurry/uneven bitmap glyphs. */
static void display_margin_apply(App *a){
    int mx=g_output_width*a->wallpaper_margin/100;
    int my=g_output_height*a->wallpaper_margin/100;
    SDL_Rect viewport={mx,my,WIDTH,HEIGHT};
    SDL_RenderSetScale(a->renderer,1.0f,1.0f);
    SDL_RenderSetViewport(a->renderer,&viewport);
}
static void display_margin_clear(App *a){
    SDL_RenderSetScale(a->renderer,1.0f,1.0f);
    SDL_RenderSetViewport(a->renderer,NULL);
    SDL_SetRenderDrawColor(a->renderer,0,0,0,255);
    SDL_RenderClear(a->renderer);
    if(!a->margin_effects_wallpaper && a->wallpaper_texture && a->wallpaper_opacity>0){
        SDL_Rect full={0,0,g_output_width,g_output_height};
        SDL_SetTextureAlphaMod(a->wallpaper_texture,(Uint8)(a->wallpaper_opacity*255/100));
        SDL_RenderCopy(a->renderer,a->wallpaper_texture,NULL,&full);
    }
    display_margin_apply(a);
}
static void render(App *a) {
    XsshThemeColors c=xssh_theme_colors(a->settings.theme);
    /* A stage change invalidates the *whole* display, not just the terminal
       cells. In GPU mode the SDL surface persists across sessions, so a
       cached-cell-only repaint otherwise leaves login controls underneath
       transparent/undrawn parts of the new shell (and vice versa). */
    int stage_changed=a->last_rendered_stage!=a->stage;
    if(stage_changed){
        a->cache_valid=0;
        a->cache_scan_active=0;
        a->cache_scan_again=0;
        a->cache_scan_cursor=0;
        a->last_cursor_visible=0;
        a->term.dirty=1;
    }
    if(!a->gpu_active || !a->cache_valid || a->stage!=ST_CONNECTED){
        display_margin_clear(a);
    }else{
        /* Preserve unchanged CPU terminal pixels between redraws. */
        display_margin_apply(a);
        rect(a,0,0,WIDTH,SCREEN_Y,c.background);
        wallpaper_draw_region(a,0,0,WIDTH,SCREEN_Y,0,0);
        /* The footer/status contains dynamic text of changing lengths. */
        int lower=a->last_keyboard_present?HEIGHT-32:HEIGHT-155;
        if(lower<0)lower=0;
        rect(a,0,lower,WIDTH,HEIGHT-lower,c.background);
        wallpaper_draw_region(a,0,lower,WIDTH,HEIGHT-lower,0,lower);
    }
    SDL_SetRenderDrawColor(a->renderer,c.background.r,c.background.g,c.background.b,255);
    if(a->margin_effects_wallpaper && a->wallpaper_texture && a->wallpaper_opacity>0){
        SDL_SetTextureAlphaMod(a->wallpaper_texture,(Uint8)(a->wallpaper_opacity*255/100));
        SDL_Rect wallpaper_dest=wallpaper_bounds(a);
        SDL_RenderCopy(a->renderer,a->wallpaper_texture,NULL,&wallpaper_dest);
    }
    text_at(a,8,6,XSSH_UI_LABEL,c.foreground,25);
    {char video_font[48];
     snprintf(video_font,sizeof(video_font),"%s F%d %s AA:%s",a->video.description,a->settings.font_size,
              xssh_font_style_name(a->settings.font_style),a->settings.text_antialiasing?"ON":"OFF");
     text_at(a,290,6,video_font,c.foreground,24);
    }
    const char *lab=(a->stage==ST_CONNECTED)?"SSH CONNECTED":
                    (a->stage==ST_VERIFY_HOST)?"VERIFY HOST KEY":
                    (a->stage==ST_FORM)?"SSH LOGIN":(a->stage==ST_POWER_CONFIRM)?"POWER MENU":"DISCONNECTED";
    text_at(a,496,6,lab,c.foreground,18);
    rect(a,0,23,WIDTH,1,c.foreground);
    if(a->stage==ST_CONNECTED) {
        draw_terminal(a,c);
    } else if(a->stage==ST_POWER_CONFIRM) {
        text_at(a,72,100,a->power_action==3?"SHUT DOWN ORIGINAL XBOX?":"RESTART ORIGINAL XBOX?",c.foreground,60);
        text_at(a,72,150,"Y / Enter = CONFIRM",c.foreground,60);
        text_at(a,72,175,"N / Escape = CANCEL",c.foreground,60);
    } else if(a->stage==ST_VERIFY_HOST) {
        text_at(a,14,65,"NEW SSH SERVER - VERIFY FINGERPRINT",c.foreground,76);
        text_at(a,14,98,"Server SHA-256 host key (hex):",c.foreground,76);
        text_at(a,14,119,a->pending_hostkey,c.foreground,65);
        text_at(a,14,167,"Compare this fingerprint using a trusted source.",c.foreground,76);
        text_at(a,14,192,"B / Enter = TRUST & CONNECT",c.foreground,76);
        text_at(a,14,215,"X / Esc = CANCEL (no authentication sent)",c.foreground,76);
        text_at(a,14,246,"Trusted keys saved as D:\\xssh-known-hosts.txt",c.foreground,76);
    } else {
        int compact=HEIGHT<460;
        int form_top=compact?45:77;
        int form_step=compact?(HEIGHT<330?22:27):34;
        int form_row_h=compact?(form_step-2):26;
        int actions_y=compact?(form_top+5*form_step+7):270;
        text_at(a,14,compact?30:47,"CONNECT TO SSH SERVER",c.foreground,60);
        /* Release identifier is visible before any network login. */
        if(WIDTH>=700)text_at(a,WIDTH-260,compact?30:47,XSSH_BUILD_ID,c.foreground,30);
        for(int field=0;field<XSSH_FORM_COUNT;field++) {
            const char *value=(field==XSSH_FORM_HOST)?a->form.host:
                              (field==XSSH_FORM_PORT)?a->form.port:
                              (field==XSSH_FORM_USER)?a->form.user:
                              (field==XSSH_FORM_KEY_FILE)?a->form.key_file:a->form.password;
            char display[80];
            if(field==XSSH_FORM_PASSWORD){
                size_t n=strlen(value);if(n>sizeof(display)-1)n=sizeof(display)-1;
                memset(display,'*',n);display[n]=0;
            }else snprintf(display,sizeof(display),"%s",value);
            int y=form_top+field*form_step;
            const char *label=(field==XSSH_FORM_PASSWORD && a->form.key_file[0])?
                              "PASSPHRASE":xssh_form_field_name(field);
            if(a->stage==ST_FORM && a->form.field==field) {
                rect(a,8,y-5,WIDTH-16,form_row_h,c.foreground);
                text_at(a,14,y,label,c.background,12);
                text_at(a,150,y,display,c.background,58);
                int cursor_x=150+(int)strlen(display)*8;
                if(cursor_x<615)rect(a,cursor_x,y+11,7,2,c.background);
            } else {
                outline(a,8,y-5,WIDTH-16,form_row_h,c.foreground);
                text_at(a,14,y,label,c.foreground,12);
                text_at(a,150,y,display,c.foreground,58);
            }
        }
        const char *actions[]={"CONNECT","EXIT","RESTART","SHUTDOWN"};
        for(int i=0;i<4;i++){
            int x=14+i*((WIDTH-28)/4),y=actions_y;
            int selected=(a->menu_focus && a->menu_action==i);
            if(selected)rect(a,x,y,(WIDTH-28)/4-8,25,c.foreground);
            else outline(a,x,y,(WIDTH-28)/4-8,25,c.foreground);
            text_at(a,x+12,y+7,actions[i],selected?c.background:c.foreground,18);
        }
        if(actions_y+58<HEIGHT-42)
            text_at(a,14,actions_y+32,"TAB: MENU / F9: EXIT  F10: RESTART  F11: OFF",c.foreground,74);
    }
    /* On the login form with a USB keyboard, use the short bottom status
       layout. The old 154px footer placed status across the action buttons
       at 480p when display margins reduced the viewport height. */
    int usb_terminal=a->last_keyboard_present &&
                     (a->stage==ST_CONNECTED || a->stage==ST_FORM);
    rect(a,0,usb_terminal?(HEIGHT-30):(HEIGHT-154),WIDTH,1,c.foreground);
    if(a->drums.enabled && !(a->volume_status[0] &&
        (int32_t)(a->volume_status_until_ms-SDL_GetTicks())>0)){
        snprintf(a->volume_status,sizeof(a->volume_status),"DRUM ON %d%%",a->drums.volume);
        a->volume_status_until_ms=SDL_GetTicks()+1500u;
    }
    /* Keep CPU and RAM visible together; volume temporarily replaces RAM. */
    const char *right_status =
        (a->volume_status[0] &&
         (int32_t)(a->volume_status_until_ms-SDL_GetTicks())>0)
        ? a->volume_status : a->ram_status;
    char meters[64];
    if(right_status[0])
        snprintf(meters,sizeof(meters),"%s  %s",a->cpu_status,right_status);
    else snprintf(meters,sizeof(meters),"%s",a->cpu_status);
    int meter_len=(int)strlen(meters);
    if(meter_len>25)meter_len=25;
    int meter_x=WIDTH-4-meter_len*8;
    text_at(a,4,usb_terminal?(HEIGHT-29):(HEIGHT-151),a->status,c.foreground,
            meter_x>8?(meter_x-8)/8:0);
    text_at(a,meter_x,usb_terminal?(HEIGHT-29):(HEIGHT-151),meters,c.foreground,meter_len);
    /* USB keyboard auto-hide remains unchanged in both themes. */
    if(xssh_software_keyboard_visible()) {
        for(int row=0;row<5;row++){
            if(row<4){
                for(int col=0;col<14;col++){
                    const char *keys=a->shift?keyboard_shift[row]:keyboard[row];
                    if(col>=(int)strlen(keys))continue;
                    char ch=keys[col];
                    int x=12+col*KEY_W,y=KEY_Y+row*KEY_H;
                    int sel=(row==a->key_row&&col==a->key_col);
                    keyboard_key(a,x,y,41,21,sel,c);
                    glyph(a,x+17,y+4,ch,sel?c.background:c.foreground);
                }
            } else {
                static const int starts[]={0,3,6,8,10,12};
                static const int spans[]={3,3,2,2,2,2};
                for(int j=0;j<6;j++){
                    int x=12+starts[j]*KEY_W,y=KEY_Y+row*KEY_H;
                    int sel=(a->key_row==row&&key_index(a)==j);
                    int w=spans[j]*KEY_W-3;
                    keyboard_key(a,x,y,w,21,sel,c);
                    int chars=(int)strlen(special[j]);
                    int offset=(w-chars*8)/2;
                    if(offset<0)offset=0;
                    text_at(a,x+offset,y+4,special[j],sel?c.background:c.foreground,12);
                }
            }
        }
    } else if(a->stage!=ST_CONNECTED && HEIGHT>=510) {
        text_at(a,12,353,"USB KEYBOARD CONNECTED",c.foreground,52);
        text_at(a,12,379,"Type using your USB keyboard.",c.foreground,55);
        text_at(a,12,402,"Unplug to restore the controller keyboard.",c.foreground,55);
    }
    {char footer[100];
     snprintf(footer,sizeof(footer),"XBOX IP: %-15s | %s | %s",
              a->network.initialized ? a->network.address : "UNAVAILABLE",
              xssh_usb_keyboard_connected() ? "USB KB READY" : "USB KB NOT FOUND",
              xssh_software_keyboard_visible() ?
                  "D-PAD/A TYPE B ENTER X BACK" : "USB TYPE B ENTER X BACK");
     text_at(a,4,HEIGHT-17,footer,c.foreground,79);}
    if(a->gpu_active){
        /* Flush SDL's software command queue before NV2A reads the pixels. */
        SDL_RenderPresent(a->renderer);
        xssh_gpu_present(&a->gpu,a->gpu_surface);
    }else SDL_RenderPresent(a->renderer);
    a->last_rendered_stage=a->stage;
}

static void disconnect(App *a) {
    if(a->channel){
        /* Socket close first to avoid a blocking protocol shutdown. */
        if(a->sock>=0){close(a->sock);a->sock=-1;}
        libssh2_channel_free(a->channel);a->channel=NULL;
    }
    if(a->sock>=0){close(a->sock);a->sock=-1;}
    if(a->session){libssh2_session_free(a->session);a->session=NULL;}
    a->tx_len=0;
    a->ssh_tx_stall_reported=0;
    a->ssh_last_tx_progress_ms=0;
    a->ssh_last_keepalive_poll_ms=0;
}
static int pin_is_valid(const char *pin) {
    if(strlen(pin)!=64)return 0;
    for(int i=0;i<64;i++)if(!isxdigit((unsigned char)pin[i]))return 0;
    return 1;
}
static int match_pin(const char *pin, const char *computed) {
    unsigned char diff=0;
    for(int i=0;i<64;i++)diff |= (unsigned char)(tolower((unsigned char)pin[i])^computed[i]);
    return diff==0;
}
static int ssh_auth_complete(App *a) {
    int auth_result;
    if(a->form.key_file[0]) {
        /* Host verification has already succeeded. Never fall back to sending
           a key passphrase as an account password. Derive the public RSA key
           from the private PEM file; no separate .pub file is required. */
        /* Diagnostic preflight: verify file access and recognizable PEM envelope.
           This does not parse/decrypt the key or change the auth mechanism. */
        FILE *key=fopen(a->form.key_file,"rb");
        if(!key){status(a,"SSH key stage=file-open: cannot open private key");return -1;}
        char header[96]={0};
        size_t read_count=fread(header,1,sizeof(header)-1,key);
        int read_error=ferror(key);
        fclose(key);
        if(read_error){status(a,"SSH key stage=file-read: I/O error");return -1;}
        if(read_count==0){status(a,"SSH key stage=file-read: empty key file");return -1;}
        const char *format="unknown";
        if(strstr(header,"-----BEGIN RSA PRIVATE KEY-----"))format="RSA-PEM";
        else if(strstr(header,"-----BEGIN PRIVATE KEY-----"))format="PKCS8-PEM";
        else if(strstr(header,"-----BEGIN ENCRYPTED PRIVATE KEY-----"))format="PKCS8-encrypted";
        else if(strstr(header,"-----BEGIN OPENSSH PRIVATE KEY-----"))format="OpenSSH";
        if(!strcmp(format,"unknown")){
            status(a,"SSH key stage=header: unrecognized private key header");
            return -1;
        }
        /* Independent Mbed TLS private-key parsing check. This is diagnostic
           only: libssh2 still owns authentication and RSA signing. Do not
           print private-key material or passphrases. */
        {
            mbedtls_pk_context parsed;
            int parse_rc;
            char message[160];
            mbedtls_pk_init(&parsed);
            parse_rc=mbedtls_pk_parse_keyfile(&parsed,a->form.key_file,
                a->form.password[0]?a->form.password:NULL,NULL,NULL);
            if(parse_rc!=0){
                char explanation[80]={0};
                mbedtls_strerror(parse_rc,explanation,sizeof(explanation));
                snprintf(message,sizeof(message),
                    "Key stage=mbedtls-parse rc=%d: %.75s",parse_rc,explanation);
                mbedtls_pk_free(&parsed);
                status(a,message);
                return -1;
            }
            if(!mbedtls_pk_can_do(&parsed,MBEDTLS_PK_RSA)){
                mbedtls_pk_free(&parsed);
                status(a,"Key stage=mbedtls-parse: key is not RSA");
                return -1;
            }
            mbedtls_pk_free(&parsed);
        }
        /* The original libssh2 authentication call remains authoritative.
           Its return value is captured immediately; its session error is
           sampled before any further libssh2 calls. */
        auth_result=libssh2_userauth_publickey_fromfile_ex(a->session,
            a->form.user,(unsigned int)strlen(a->form.user),NULL,
            a->form.key_file,a->form.password[0]?a->form.password:NULL);
        if(auth_result) {
            char *detail=NULL;
            int len=0;
            int code=libssh2_session_last_error(a->session,&detail,&len,0);
            char message[160];
            if(detail && len>0){
                if(len>52)len=52;
                snprintf(message,sizeof(message),"RSA parsed; auth rc=%d last=%d: %.*s",
                         auth_result,code,len,detail);
            }else{
                snprintf(message,sizeof(message),"RSA parsed; auth rc=%d last=%d (sign/server?)",
                         auth_result,code);
            }
            status(a,message);
            return -1;
        }
    } else {
        auth_result=libssh2_userauth_password(a->session,a->form.user,a->form.password);
        if(auth_result){status(a,"Password authentication failed");return -1;}
    }
    xssh_form_zero_password(&a->form);
    /* A moderate receive window reduces memory pressure on the 64 MiB Xbox.
       RFC 4254 permits any positive initial window; 256 KiB is ample for a
       terminal and much smaller than libssh2's usual multi-MiB default. */
    a->channel=libssh2_channel_open_ex(a->session,"session",7,
                                      256u*1024u,32u*1024u,NULL,0);
    if(!a->channel){
        char *detail=NULL;
        int len=0;
        int code=libssh2_session_last_error(a->session,&detail,&len,0);
        char message[160];
        if(detail && len>0){
            if(len>92)len=92;
            snprintf(message,sizeof(message),"SSH channel failed (%d): %.*s",code,len,detail);
        }else{
            snprintf(message,sizeof(message),"SSH channel failed (%d): no server details",code);
        }
        status(a,message);
        return -1;
    }
    if(libssh2_channel_request_pty_ex(a->channel,"xterm",5,NULL,0,
                                       a->term.cols,a->term.rows,0,0)!=0){
        status(a,"PTY request failed");return -1;
    }
    if(libssh2_channel_shell(a->channel)!=0){status(a,"Shell request failed");return -1;}
    {unsigned long nonblocking=1;
     if(ioctlsocket(a->sock,FIONBIO,&nonblocking)!=0){
         status(a,"Cannot enable nonblocking socket");return -1;
     }}
    libssh2_session_set_blocking(a->session,0);
    /* Keep idle sessions alive through wireless extenders and NAT devices.
       Nonblocking sessions must explicitly service keepalives in ssh_pump. */
    libssh2_keepalive_config(a->session,1,SSH_KEEPALIVE_INTERVAL_SECONDS);
    a->ssh_last_tx_progress_ms=SDL_GetTicks();
    a->ssh_last_keepalive_poll_ms=SDL_GetTicks();
    a->ssh_tx_stall_reported=0;
    term_set_size(&a->term,a->term.cols,a->term.rows);
    status(a,"Connected - remote shell ready");
    return 0;
}
/* 0 connected, 1 awaiting explicit fingerprint confirmation, -1 failed. */
static int ssh_connect(App *a) {
    struct sockaddr_in addr;
    const char *hostkey=NULL;
    size_t keylen=0;
    int keytype=0;
    unsigned char digest[32];
    char fingerprint[65];
    char line[160];
    int result=-1;

    disconnect(a);
    status(a,"Connecting to SSH server...");render(a);
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;addr.sin_port=htons((uint16_t)xssh_form_port(&a->form));
    addr.sin_addr.s_addr=inet_addr(a->form.host);
    if(addr.sin_addr.s_addr==INADDR_NONE){status(a,"Enter a numeric IPv4 server address");goto out;}
    a->sock=socket(AF_INET,SOCK_STREAM,0);
    if(a->sock<0){status(a,"socket() failed");goto out;}
    /* Disable Nagle: Vim and other full-screen apps need single-keystroke
       SSH packets to be sent promptly, even with pending small packets. */
    {int one=1;
     /* TCP keepalive complements SSH keepalives; leave platform defaults
        for probe timing because not all nxdk/lwIP builds expose TCP_KEEP*. */
     (void)setsockopt(a->sock,SOL_SOCKET,SO_KEEPALIVE,&one,sizeof(one));
     if(setsockopt(a->sock,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one))!=0)
         status(a,"Warning: TCP_NODELAY unavailable; input may lag");}
    if(connect(a->sock,(struct sockaddr*)&addr,sizeof(addr))<0){status(a,"TCP connection failed");goto out;}
    a->session=libssh2_session_init();
    if(!a->session){status(a,"libssh2_session_init failed");goto out;}
    libssh2_session_set_timeout(a->session,12000);
    status(a,"SSH handshake...");render(a);
    if(libssh2_session_handshake(a->session,a->sock)){
        char *msg=NULL;int len=0;
        libssh2_session_last_error(a->session,&msg,&len,0);
        snprintf(line,sizeof(line),"SSH handshake error: %.*s",len>85?85:len,msg?msg:"");
        status(a,line);goto out;
    }
    hostkey=libssh2_session_hostkey(a->session,&keylen,&keytype);
    if(!hostkey || !keylen || mbedtls_sha256((const unsigned char*)hostkey,keylen,digest,0)!=0){
        status(a,"Could not read SSH host key");goto out;
    }
    for(int i=0;i<32;i++)sprintf(fingerprint+i*2,"%02x",digest[i]);
    fingerprint[64]=0;
    /* The legacy xssh.cfg pin applies only to its configured host and port.
       Manually entered other servers have independent TOFU records. */
    if(a->settings.pin[0] && !strcmp(a->form.host,a->settings.host) &&
       xssh_form_port(&a->form)==a->settings.port) {
        if(!pin_is_valid(a->settings.pin) || !match_pin(a->settings.pin,fingerprint)) {
            status(a,"HOST KEY MISMATCH with xssh.cfg pin. Refusing connection.");goto out;
        }
    } else {
        int known=xssh_known_host_check(KNOWN_HOSTS_FILE,a->form.host,
                                        xssh_form_port(&a->form),fingerprint);
        if(known==2){status(a,"HOST KEY CHANGED! Connection refused.");goto out;}
        if(known<0){status(a,"Could not read trusted SSH host keys");goto out;}
        if(known==0){
            memcpy(a->pending_hostkey,fingerprint,sizeof(fingerprint));
            a->stage=ST_VERIFY_HOST;
            status(a,"Unknown server key: verify SHA-256 fingerprint before trusting");
            return 1; /* Stay connected, but DO NOT authenticate yet. */
        }
    }
    status(a,"Host key verified; authenticating...");render(a);
    if(ssh_auth_complete(a)==0){result=0;goto out;}
out:
    xssh_form_zero_password(&a->form);
    if(result!=0)disconnect(a);
    return result;
}
static void reject_unknown_host(App *a) {
    disconnect(a);
    xssh_form_zero_password(&a->form);
    memset(a->pending_hostkey,0,sizeof(a->pending_hostkey));
    a->stage=ST_FORM;
    a->form.field=XSSH_FORM_PASSWORD;
    status(a,"Host key not trusted; authentication was not sent");
}
static void trust_host_and_authenticate(App *a) {
    if(xssh_known_host_add(KNOWN_HOSTS_FILE,a->form.host,
                           xssh_form_port(&a->form),a->pending_hostkey)!=0){
        status(a,"Cannot save trusted host key (writable HDD required)");
        a->stage=ST_FAILED;
        disconnect(a);
    } else if(ssh_auth_complete(a)==0) {
        a->stage=ST_CONNECTED;
        a->cache_valid=0;
        a->last_render_ms=0;
    } else {
        a->stage=ST_FAILED;
        disconnect(a);
    }
    xssh_form_zero_password(&a->form);
    memset(a->pending_hostkey,0,sizeof(a->pending_hostkey));
}
static void ssh_flush_outgoing(App *a);
static void enqueue(App *a,const unsigned char *data,size_t n) {
    if(a->stage!=ST_CONNECTED||n==0)return;
    if(n>TX_CAP-a->tx_len){status(a,"Outgoing buffer full");return;}
    if(a->tx_len==0){
        a->ssh_last_tx_progress_ms=SDL_GetTicks();
        a->ssh_tx_queued_ms=a->ssh_last_tx_progress_ms;
        a->ssh_tx_stall_reported=0;
        a->ssh_tx_queued_since_ms=a->ssh_last_tx_progress_ms;
    }
    memcpy(a->outgoing+a->tx_len,data,n);a->tx_len+=n;
}
static void type_byte(App *a,unsigned char c) {
    if(a->stage==ST_FORM)xssh_form_type(&a->form,c);
    else if(a->stage==ST_CONNECTED)enqueue(a,&c,1);
}
static void send_sequence(App*a,const char *s) {
    if(a->stage==ST_CONNECTED)enqueue(a,(const unsigned char*)s,strlen(s));
    else if(a->stage==ST_FORM && s && s[0]==27 && s[1]=='[') {
        if(s[2]=='A' && a->form.field>0)a->form.field--;
        if(s[2]=='B' && a->form.field<XSSH_FORM_PASSWORD)a->form.field++;
        if(s[2]=='Z' && a->form.field>0)a->form.field--;
    }
}
static void backspace(App *a) {
    if(a->stage==ST_VERIFY_HOST){reject_unknown_host(a);return;}
    if(a->stage==ST_FORM)xssh_form_backspace(&a->form);
    else if(a->stage==ST_CONNECTED)type_byte(a,127);
}
static void enter(App*a) {
    if(a->stage==ST_VERIFY_HOST){trust_host_and_authenticate(a);return;}
    if(a->stage==ST_FORM) {
        if(!xssh_form_validate(&a->form,a->form.field)) {
            char message[100];
            snprintf(message,sizeof(message),"Invalid %s; correct it before continuing",
                     xssh_form_field_name(a->form.field));
            status(a,message);return;
        }
        if(a->form.field<XSSH_FORM_PASSWORD){
            a->form.field++;
            status(a,"Enter the next field, then B / Enter");return;
        }
        if(!a->ready){status(a,"Network/seed not ready; check configuration");return;}
        xssh_network_poll(&a->network);
        if(!xssh_network_ready(&a->network)) {
            status(a,"No Xbox IP address yet; check Ethernet/DHCP");return;
        }
        int r=ssh_connect(a);
        if(r==0){
            a->stage=ST_CONNECTED;
            a->cache_valid=0;
            a->last_render_ms=0;
        } else if(r<0)a->stage=ST_FAILED;
        /* r==1: first-use host fingerprint displayed for confirmation. */
    } else if(a->stage==ST_FAILED) {
        a->stage=ST_FORM;
        a->form.field=XSSH_FORM_HOST;
        xssh_form_zero_password(&a->form);
        status(a,"Edit connection details; B / Enter advances");
    } else type_byte(a,'\r');
}
static void virtual_key(App *a) {
    if(a->stage==ST_FAILED){enter(a);return;}
    if(a->key_row<4){
        const char *keys=a->shift?keyboard_shift[a->key_row]:keyboard[a->key_row];
        if(a->key_col>=(int)strlen(keys))return;
        type_byte(a,(unsigned char)keys[a->key_col]);
    } else {
        switch(key_index(a)){
        case 0:type_byte(a,' ');break;
        case 1:enter(a);break;
        case 2:backspace(a);break;
        case 3:if(a->stage==ST_FORM)enter(a);else type_byte(a,'\t');break;
        case 4:if(a->stage==ST_VERIFY_HOST)reject_unknown_host(a);else type_byte(a,27);break;
        case 5:type_byte(a,3);break;
        }
    }
}
/* Never let a busy program's stdout starve interactive keyboard input.
   This function is always called before the read burst and once afterward.
   It never blocks: libssh2's session and socket are both nonblocking. */
/* Retry a blocked write only when socket readiness changes, or periodically
   as a fallback for buffered libssh2 protocol work. No blocking wait. */
static int ssh_tx_can_retry(App *a,unsigned int now) {
    if(!a->ssh_tx_waiting)return 1;
    if((unsigned)(now-a->ssh_tx_last_attempt_ms)>=SSH_TX_RETRY_INTERVAL_MS)return 1;
    if(a->sock<0)return 0;
    fd_set rd,wr;
    struct timeval tv={0,0};
    int directions=libssh2_session_block_directions(a->session);
    FD_ZERO(&rd);FD_ZERO(&wr);
    if(directions&LIBSSH2_SESSION_BLOCK_INBOUND)FD_SET(a->sock,&rd);
    if(directions&LIBSSH2_SESSION_BLOCK_OUTBOUND)FD_SET(a->sock,&wr);
    if(!directions)return 1;
    return select(a->sock+1,&rd,&wr,NULL,&tv)>0;
}
static void ssh_flush_outgoing(App *a) {
    if(a->stage!=ST_CONNECTED || !a->channel || !a->tx_len)return;
    unsigned int now=SDL_GetTicks();
    if(!ssh_tx_can_retry(a,now))return;
    a->ssh_tx_last_attempt_ms=now;
    ssize_t n=libssh2_channel_write(a->channel,(const char*)a->outgoing,a->tx_len);
    if(n>0){
        if((size_t)n<a->tx_len)a->ssh_tx_partial_count++;
        a->ssh_tx_waiting=0;
        a->ssh_last_tx_progress_ms=SDL_GetTicks();
        if(a->ssh_tx_stall_reported){
            char msg[160];
            snprintf(msg,sizeof(msg),"SSH input resumed; queued %ums, EAGAIN %u, partial %u",
                     (unsigned)(a->ssh_last_tx_progress_ms-a->ssh_tx_queued_ms),
                     a->ssh_tx_eagain_count,a->ssh_tx_partial_count);
            status_bar_only(a,msg);
            a->ssh_tx_stall_reported=0;
        }
        a->tx_len-=(size_t)n;
        if(a->tx_len)memmove(a->outgoing,a->outgoing+n,a->tx_len);
        else a->ssh_tx_queued_ms=0;
    } else if(n==LIBSSH2_ERROR_EAGAIN || n==0){
        a->ssh_tx_waiting=1;
        if(n==LIBSSH2_ERROR_EAGAIN)a->ssh_tx_eagain_count++;
    } else {
        a->stage=ST_FAILED;
        status(a,"SSH write error");
        disconnect(a);
    }
}
static void ssh_pump(App *a) {
    if(a->stage!=ST_CONNECTED || !a->channel)return;
    /* Interactive SSH input gets the FIRST opportunity to use the socket. */
    ssh_flush_outgoing(a);
    if(a->stage!=ST_CONNECTED)return;
    /* A delayed network must not block keyboard processing or rendering.
       Do not discard queued keystrokes merely because the socket is busy. */
    {unsigned int now=SDL_GetTicks();
     if(a->tx_len && !a->ssh_tx_stall_reported &&
        (unsigned)(now-a->ssh_last_tx_progress_ms)>=SSH_TX_STALL_NOTICE_MS){
         a->ssh_tx_stall_reported=1;
         {char msg[160];
          snprintf(msg,sizeof(msg),"SSH input queued %ums; EAGAIN %u; partial %u",
                   (unsigned)(now-a->ssh_tx_queued_since_ms),
                   a->ssh_tx_eagain_count,a->ssh_tx_partial_count);
          status_bar_only(a,msg);}
     }}

    unsigned char data[4096];
    const unsigned int begin=SDL_GetTicks();
    for(int j=0;j<SSH_READ_BURST_MAX;j++) {
        if((unsigned)(SDL_GetTicks()-begin)>=SSH_READ_TIME_BUDGET_MS)break;
        ssize_t n=libssh2_channel_read(a->channel,(char*)data,sizeof(data));
        if(n>0){
            term_feed(&a->term,data,(unsigned int)n);
            unsigned char reply[128];
            unsigned int r=term_take_response(&a->term,reply,sizeof(reply));
            if(r)enqueue(a,reply,r);
        }
        else if(n==LIBSSH2_ERROR_EAGAIN || n==0)break;
        else {a->stage=ST_FAILED;status(a,"SSH read error");disconnect(a);return;}
        /* Bound SSH decoding per frame so input and screen redraw get CPU time. */
    }
    /* Answers to terminal status queries can be sent in the same frame. */
    ssh_flush_outgoing(a);
    if(a->stage!=ST_CONNECTED)return;
    /* Periodic nonblocking SSH keepalive. EAGAIN means retry next tick;
       a temporary wireless stall is not grounds to drop the connection. */
    {unsigned int now=SDL_GetTicks();
     if((unsigned)(now-a->ssh_last_keepalive_poll_ms)>=SSH_KEEPALIVE_POLL_MS){
         int seconds_to_next=0;
         int rc;
         a->ssh_last_keepalive_poll_ms=now;
         rc=libssh2_keepalive_send(a->session,&seconds_to_next);
         if(rc!=0 && rc!=LIBSSH2_ERROR_EAGAIN){
             a->stage=ST_FAILED;
             status(a,"SSH keepalive failed; connection lost");
             disconnect(a);
             return;
         }
     }}
    if(libssh2_channel_eof(a->channel)){
        a->stage=ST_FAILED;status(a,"Remote shell closed");disconnect(a);
    }
}
static void nav(App *a,int dx,int dy) {
    a->key_row=(a->key_row+dy+5)%5;
    if(dx) a->key_col=(a->key_col+dx+14)%14;
    int limit=(a->key_row==0)?12:(a->key_row==1)?13:
              (a->key_row==2)?11:(a->key_row==3)?10:14;
    if(a->key_col>=limit)a->key_col=limit-1;
}
static void music_toggle(App *a) {
    char msg[160];
    xssh_mp3_toggle(&a->music);
    xssh_mp3_get_message(&a->music,msg,sizeof(msg));
    status_bar_only(a,msg);
}
static void music_volume(App *a,int direction) {
    char msg[160];
    xssh_mp3_volume(&a->music,direction);
    xssh_mp3_get_message(&a->music,msg,sizeof(msg));
    snprintf(a->volume_status,sizeof(a->volume_status),"%s",msg);
    a->volume_status_until_ms=SDL_GetTicks()+2000u;
    a->ui_status_dirty=1;
}
static void music_skip(App *a,int direction){
    xssh_mp3_skip(&a->music,direction);
}
static void music_seek(App *a,int direction){
    xssh_mp3_seek(&a->music,direction);
}
static void toggle_theme(App *a){
    a->settings.theme=(a->settings.theme==XSSH_THEME_XBOX)?
        XSSH_THEME_INVERTED_XBOX:XSSH_THEME_XBOX;
    /* Theme colors affect all terminal cells, not only changed text. */
    a->cache_valid=0;
    a->term.dirty=1;
    a->ui_status_dirty=1;
}
static void drum_notice(App *a){
    snprintf(a->volume_status,sizeof(a->volume_status),"DRUM %s %d%%",a->drums.enabled?"ON":"OFF",a->drums.volume);
    a->volume_status_until_ms=SDL_GetTicks()+2500u;a->ui_status_dirty=1;
}
static void drum_toggle(App *a){xssh_drums_toggle(&a->drums);drum_notice(a);}
static int drum_pad(App *a,int pad){
    if(!a->drums.enabled)return 0;
    if(pad>0)xssh_drums_play(&a->drums,pad);
    return 1;
}
static int drum_volume(App *a,int direction){
    if(!a->drums.enabled)return 0;
    xssh_drums_volume(&a->drums,direction);drum_notice(a);return 1;
}
static void keyboard_event(App*a,const SDL_KeyboardEvent *e) {
    if((e->keysym.mod & (KMOD_CTRL|KMOD_SHIFT))==(KMOD_CTRL|KMOD_SHIFT) &&
       e->keysym.sym==SDLK_o){
        if(!e->repeat){
            wallpaper_step(a);a->wallpaper_hold=1;
            a->wallpaper_next_step=SDL_GetTicks()+130u;
        }
        return;
    }
    if(e->repeat)return;
    if((e->keysym.mod & (KMOD_CTRL|KMOD_SHIFT))==(KMOD_CTRL|KMOD_SHIFT) &&
       e->keysym.sym==SDLK_m){wallpaper_margin_step(a);return;}
    if((e->keysym.mod & (KMOD_CTRL|KMOD_SHIFT))==(KMOD_CTRL|KMOD_SHIFT) &&
       e->keysym.sym==SDLK_b){wallpaper_next(a);return;}
    if((e->keysym.mod & KMOD_CTRL) && e->keysym.sym==SDLK_SPACE){drum_toggle(a);return;}
    if(e->keysym.sym>=SDLK_KP_1 && e->keysym.sym<=SDLK_KP_9 && drum_pad(a,(int)(e->keysym.sym-SDLK_KP_1)+1))return;
    if(e->keysym.sym==SDLK_KP_0 && drum_pad(a,0))return;
    if(e->keysym.sym==SDLK_KP_PLUS && drum_volume(a,1))return;
    if(e->keysym.sym==SDLK_KP_MINUS && drum_volume(a,-1))return;
    if((e->keysym.mod & KMOD_CTRL) &&
       (e->keysym.sym==SDLK_EQUALS || e->keysym.sym==SDLK_PLUS ||
        e->keysym.sym==SDLK_KP_PLUS)){
        change_font_size(a,1);return;
    }
    if((e->keysym.mod & KMOD_CTRL) &&
       (e->keysym.sym==SDLK_MINUS || e->keysym.sym==SDLK_KP_MINUS)){
        change_font_size(a,-1);return;
    }
    if((e->keysym.mod & KMOD_CTRL) &&
       (e->keysym.sym==SDLK_LEFT || e->keysym.sym==SDLK_RIGHT)){
        int dir=e->keysym.sym==SDLK_LEFT?-1:1;
        if(e->keysym.mod & KMOD_SHIFT)music_seek(a,dir);
        else music_skip(a,dir);
        return;
    }
    if((e->keysym.mod & KMOD_CTRL) && e->keysym.sym==SDLK_t){toggle_theme(a);return;}
    if((e->keysym.mod & KMOD_CTRL) && e->keysym.sym==SDLK_p){music_toggle(a);return;}
    if((e->keysym.mod & KMOD_CTRL) && e->keysym.sym==SDLK_UP){music_volume(a,1);return;}
    if((e->keysym.mod & KMOD_CTRL) && e->keysym.sym==SDLK_DOWN){music_volume(a,-1);return;}
    if((e->keysym.mod & KMOD_CTRL) && e->keysym.sym==SDLK_f){change_font_style(a);return;}
    if(a->stage==ST_POWER_CONFIRM){
        if(e->keysym.sym==SDLK_y || e->keysym.sym==SDLK_RETURN || e->keysym.sym==SDLK_KP_ENTER)confirm_power(a,1);
        else if(e->keysym.sym==SDLK_n || e->keysym.sym==SDLK_ESCAPE)confirm_power(a,0);
        return;
    }
    if(a->stage==ST_FORM){
        if(e->keysym.sym==SDLK_F9){startup_action(a,1);return;}
        if(e->keysym.sym==SDLK_F10){startup_action(a,2);return;}
        if(e->keysym.sym==SDLK_F11){startup_action(a,3);return;}
        if(e->keysym.sym==SDLK_ESCAPE){a->menu_focus=!a->menu_focus;return;}
        if(e->keysym.sym==SDLK_TAB){a->menu_focus=!a->menu_focus;return;}
        if(a->menu_focus){
            if(e->keysym.sym==SDLK_LEFT){a->menu_action=(a->menu_action+3)%4;return;}
            if(e->keysym.sym==SDLK_RIGHT){a->menu_action=(a->menu_action+1)%4;return;}
            if(e->keysym.sym==SDLK_RETURN || e->keysym.sym==SDLK_KP_ENTER){startup_action(a,a->menu_action);return;}
        }
    }
    switch(e->keysym.sym){
    case SDLK_RETURN:case SDLK_KP_ENTER:enter(a);break;
    case SDLK_BACKSPACE:backspace(a);break;
    case SDLK_TAB:if(a->stage==ST_FORM){
        if(e->keysym.mod & KMOD_SHIFT){if(a->form.field>0)a->form.field--;}
        else enter(a);
    }else type_byte(a,9);break;
    case SDLK_ESCAPE:if(a->stage==ST_VERIFY_HOST)reject_unknown_host(a);else type_byte(a,27);break;
    case SDLK_UP:send_sequence(a,"\033[A");break;
    case SDLK_DOWN:send_sequence(a,"\033[B");break;
    case SDLK_RIGHT:send_sequence(a,"\033[C");break;
    case SDLK_LEFT:send_sequence(a,"\033[D");break;
    case SDLK_a: if(e->keysym.mod & KMOD_CTRL)type_byte(a,1);break;
    case SDLK_c: if(e->keysym.mod & KMOD_CTRL)type_byte(a,3);break;
    default:break;
    }
}
/* HID reports are decoded on the main thread, NEVER in the USB IRQ callback.
   Password mode accepts printable ASCII only; connected mode forwards control
   sequences, including Ctrl+C, Ctrl+D, arrows, Home/End and function keys. */
static void usb_key_event(void *ctx,XsshKeyEvent event) {
    App *a=(App*)ctx;
    /* Drum controls must remain playable without waking the screensaver. */
    if(event.kind==XSSH_KEY_WALLPAPER_MARGIN){wallpaper_margin_step(a);return;}
    if(event.kind==XSSH_KEY_WALLPAPER_NEXT){wallpaper_next(a);return;}
    if(event.kind==XSSH_KEY_WALLPAPER_OPACITY){
        wallpaper_step(a);return;
    }
    if(event.kind==XSSH_KEY_DRUM_TOGGLE){drum_toggle(a);return;}
    if(a->drums.enabled && event.kind==XSSH_KEY_DRUM_PAD){drum_pad(a,event.byte);return;}
    if(a->drums.enabled && event.kind==XSSH_KEY_DRUM_UP){drum_volume(a,1);return;}
    if(a->drums.enabled && event.kind==XSSH_KEY_DRUM_DOWN){drum_volume(a,-1);return;}
    a->last_input_ms=SDL_GetTicks();
    if(a->saver_active){saver_stop(a);return;}
    if(event.kind==XSSH_KEY_DRUM_PAD){
        if(!drum_pad(a,event.byte))type_byte(a,(unsigned char)('0'+event.byte));
        return;
    }
    if(event.kind==XSSH_KEY_DRUM_UP){if(!drum_volume(a,1))type_byte(a,'+');return;}
    if(event.kind==XSSH_KEY_DRUM_DOWN){if(!drum_volume(a,-1))type_byte(a,'-');return;}
    if(event.kind==XSSH_KEY_MUSIC_PREVIOUS){music_skip(a,-1);return;}
    if(event.kind==XSSH_KEY_MUSIC_NEXT){music_skip(a,1);return;}
    if(event.kind==XSSH_KEY_MUSIC_SEEK_BACK){music_seek(a,-1);return;}
    if(event.kind==XSSH_KEY_MUSIC_SEEK_FORWARD){music_seek(a,1);return;}
    if(event.kind==XSSH_KEY_MUSIC_TOGGLE){music_toggle(a);return;}
    if(event.kind==XSSH_KEY_MUSIC_VOLUME_UP){music_volume(a,1);return;}
    if(event.kind==XSSH_KEY_MUSIC_VOLUME_DOWN){music_volume(a,-1);return;}
    if(event.kind==XSSH_KEY_FONT_CYCLE){change_font_style(a);return;}
    if(event.kind==XSSH_KEY_THEME_TOGGLE){toggle_theme(a);return;}
    if(a->stage==ST_POWER_CONFIRM){
        if(event.kind==XSSH_KEY_ENTER || (event.kind==XSSH_KEY_TEXT && (event.byte=='y'||event.byte=='Y')))confirm_power(a,1);
        else if(event.kind==XSSH_KEY_TEXT && (event.byte=='n'||event.byte=='N'||event.byte==27))confirm_power(a,0);
        return;
    }
    if(a->stage==ST_FORM){
        if(event.kind==XSSH_KEY_TEXT && event.byte==27){a->menu_focus=!a->menu_focus;return;}
        if(event.kind==XSSH_KEY_TEXT && event.byte==9){a->menu_focus=!a->menu_focus;return;}
        if(event.kind==XSSH_KEY_SEQUENCE && event.sequence){
            if(!strcmp(event.sequence,"\033[20~")){startup_action(a,1);return;}
            if(!strcmp(event.sequence,"\033[21~")){startup_action(a,2);return;}
            if(!strcmp(event.sequence,"\033[23~")){startup_action(a,3);return;}
        }
        if(a->menu_focus){
            if(event.kind==XSSH_KEY_ENTER){startup_action(a,a->menu_action);return;}
            if(event.kind==XSSH_KEY_SEQUENCE && event.sequence){
                if(!strcmp(event.sequence,"\033[D"))a->menu_action=(a->menu_action+3)%4;
                else if(!strcmp(event.sequence,"\033[C"))a->menu_action=(a->menu_action+1)%4;
                return;
            }
        }
    }
    switch(event.kind) {
    case XSSH_KEY_TEXT:
        if(event.byte==27 && a->stage==ST_VERIFY_HOST)reject_unknown_host(a);
        else if(event.byte==9 && a->stage==ST_FORM)enter(a);
        else type_byte(a,event.byte);
        break;
    case XSSH_KEY_FONT_INCREASE: change_font_size(a,1);break;
    case XSSH_KEY_FONT_DECREASE: change_font_size(a,-1);break;
    case XSSH_KEY_FONT_CYCLE: change_font_style(a);break;
    case XSSH_KEY_THEME_TOGGLE: toggle_theme(a);break;
    case XSSH_KEY_MUSIC_PREVIOUS: music_skip(a,-1);break;
    case XSSH_KEY_MUSIC_NEXT: music_skip(a,1);break;
    case XSSH_KEY_MUSIC_SEEK_BACK: music_seek(a,-1);break;
    case XSSH_KEY_MUSIC_SEEK_FORWARD: music_seek(a,1);break;
    case XSSH_KEY_MUSIC_TOGGLE: music_toggle(a);break;
    case XSSH_KEY_MUSIC_VOLUME_UP: music_volume(a,1);break;
    case XSSH_KEY_MUSIC_VOLUME_DOWN: music_volume(a,-1);break;
    case XSSH_KEY_ENTER: enter(a);break;
    case XSSH_KEY_BACKSPACE: backspace(a);break;
    case XSSH_KEY_SEQUENCE: if(event.sequence)send_sequence(a,event.sequence);break;
    case XSSH_KEY_DRUM_TOGGLE:
    case XSSH_KEY_DRUM_PAD:
    case XSSH_KEY_DRUM_UP:
    case XSSH_KEY_DRUM_DOWN:
    case XSSH_KEY_WALLPAPER_NEXT:
    case XSSH_KEY_WALLPAPER_OPACITY:
    case XSSH_KEY_WALLPAPER_MARGIN:
        /* Already handled before this switch. */
        break;
    }
}
static void poll_pad(App*a) {
    if(a->pad && !SDL_GameControllerGetAttached(a->pad)){
        SDL_GameControllerClose(a->pad);
        a->pad=NULL;
        a->pad_old=0;
    }
    if(!a->pad){
        int count=SDL_NumJoysticks();
        for(int i=0;i<count && !a->pad;i++){
            if(SDL_IsGameController(i) && SDL_JoystickGetDevicePlayerIndex(i)==1)
                a->pad=SDL_GameControllerOpen(i);
        }
        /* Other ports may remain connected throughout a player-one swap.
           Keep waiting for port one if it was the terminal's controller. */
        if(!a->pad && !a->wait_for_port_one){
            for(int i=0;i<count && !a->pad;i++){
                if(SDL_IsGameController(i))a->pad=SDL_GameControllerOpen(i);
            }
        }
        if(a->pad)
            a->wait_for_port_one=SDL_JoystickGetPlayerIndex(SDL_GameControllerGetJoystick(a->pad))==1;
    }
    if(!a->pad)return;
    const SDL_GameControllerButton buttons[]={
        SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_START,SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN
    };
    unsigned int now=0;
    for(int i=0;i<10;i++)if(SDL_GameControllerGetButton(a->pad,buttons[i]))now |= (1u<<i);
    unsigned int pressed=now & ~a->pad_old;
    a->pad_old=now;
    if(pressed){
        a->last_input_ms=SDL_GetTicks();
        if(a->saver_active){saver_stop(a);return;}
    }
    /* Do not activate invisible virtual keys while a USB keyboard is present.
       Controller Enter/Backspace/Ctrl-C/disconnect still work. */
    int software_keyboard=xssh_software_keyboard_visible();
    if(a->stage==ST_POWER_CONFIRM){
        if(pressed&(1u<<1))confirm_power(a,1);
        if(pressed&(1u<<2))confirm_power(a,0);
        return;
    }
    if(a->stage==ST_FORM && a->menu_focus){
        if(pressed&(1u<<0) || pressed&(1u<<1))startup_action(a,a->menu_action);
        if(pressed&(1u<<6))a->menu_action=(a->menu_action+3)%4;
        if(pressed&(1u<<7))a->menu_action=(a->menu_action+1)%4;
        if(pressed&(1u<<4) || pressed&(1u<<5))a->menu_focus=0;
        return;
    }
    if(software_keyboard && (pressed&(1u<<0)))virtual_key(a);
    if(pressed&(1u<<1))enter(a);
    if(pressed&(1u<<2))backspace(a);
    if(software_keyboard && (pressed&(1u<<3)))a->shift=!a->shift;
    if(pressed&(1u<<4)){
        if(a->stage==ST_FORM && a->form.field>0)a->form.field--;
        else type_byte(a,3);
    }
    if(pressed&(1u<<5)){
        if(a->stage==ST_CONNECTED){
            disconnect(a);a->stage=ST_FORM;a->cache_valid=0;
            a->last_render_ms=0;
            xssh_form_zero_password(&a->form);a->form.field=XSSH_FORM_HOST;
            status(a,"Disconnected; enter a new server or reconnect");
        } else if(a->stage==ST_VERIFY_HOST)reject_unknown_host(a);
        else if(a->stage==ST_FORM)a->menu_focus=1;
    }
    if(software_keyboard && (pressed&(1u<<6)))nav(a,-1,0);
    if(software_keyboard && (pressed&(1u<<7)))nav(a,1,0);
    if(software_keyboard && (pressed&(1u<<8)))nav(a,0,-1);
    if(software_keyboard && (pressed&(1u<<9)))nav(a,0,1);
}
int main(void) {
    App *a=(App*)calloc(1,sizeof(App));
    if(!a)return 1;
    a->sock=-1;a->running=1;
    xssh_mp3_init(&a->music);
    a->drums.volume=70;
    a->last_input_ms=SDL_GetTicks();
    term_init(&a->term);
    /* Read configuration before initializing video. This also leaves the safe
       auto default in place when config is absent or SSH fields are invalid. */
    int settings_result = load_settings(&a->settings);
    int settings_ok = settings_result == 0;
    int cols=TERM_COLS, rows=TERM_ROWS;
    if(!xssh_video_initialize(a->settings.video_mode,&a->video))return 1;
    g_output_width=a->video.width;
    g_output_height=a->video.height;
    g_screen_width=g_output_width-2*(g_output_width*a->settings.wallpaper_margin/100);
    g_screen_height=g_output_height-2*(g_output_height*a->settings.wallpaper_margin/100);
    a->term_height=PAD_TERMINAL_HEIGHT;
    xssh_font_layout_for_area(a->settings.font_size,WIDTH,a->term_height,
                     &a->terminal_glyph_w,&a->terminal_glyph_h,&cols,&rows);
    if(!term_set_size(&a->term,cols,rows))return 1;
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)<0)return 1;
    a->last_input_ms=SDL_GetTicks();
    /* Keep SDL's event/keyboard subsystem, but render to a RAM surface when
       using pbGL. Only pbGL/pbkit owns scanout in GPU mode. */
    /* At 1920x1080 the 40 GPU tiles, software staging surface and pbkit
       scanout buffers can exhaust NV2A's available video/contiguous memory.
       Use the stable SDL renderer for 1080i pending validated paging support. */
    if(a->settings.gpu_acceleration && a->video.width<1920 &&
       a->video.height<1080){
        a->gpu_surface=SDL_CreateRGBSurface(0,a->video.width,a->video.height,32,
                                           0x000000ff,0x0000ff00,0x00ff0000,0xff000000);
        if(a->gpu_surface)
            a->renderer=SDL_CreateSoftwareRenderer(a->gpu_surface);
        if(a->renderer && xssh_gpu_init(&a->gpu,a->video.width,a->video.height)==0)
            a->gpu_active=1;
        else {
            if(a->renderer){SDL_DestroyRenderer(a->renderer);a->renderer=NULL;}
            if(a->gpu_surface){SDL_FreeSurface(a->gpu_surface);a->gpu_surface=NULL;}
        }
    }
    if(!a->gpu_active){
        a->window=SDL_CreateWindow("XSSH",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                                   a->video.width,a->video.height,SDL_WINDOW_SHOWN);
        if(!a->window)return 1;
        a->renderer=SDL_CreateRenderer(a->window,-1,0);
        if(!a->renderer)return 1;
    }
    /* Render at the selected dashboard video mode, without 640x480 upscaling.
       Terminal rows/columns are calculated from the actual pixel dimensions. */
    /* Native coordinates; margins use an inset viewport without scaling. */
    /* Nearest-neighbor sampling must be set before creating font textures. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    if(init_font(a))return 1;
    a->wallpaper_opacity=a->settings.wallpaper_opacity;
    a->wallpaper_margin=a->settings.wallpaper_margin;
    a->margin_effects_wallpaper=a->settings.margin_effects_wallpaper;
    if(a->settings.wallpaper_file[0]){
        if(!wallpaper_load(a,a->settings.wallpaper_file))wallpaper_next(a);
    }else wallpaper_next(a);
    if(!a->gpu_active){
        a->term_cache=SDL_CreateTexture(a->renderer,SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET,WIDTH,USB_TERMINAL_HEIGHT);
        if(a->term_cache)SDL_SetTextureBlendMode(a->term_cache,SDL_BLENDMODE_NONE);
    }
    /* Always show an editable login form, even if configuration, network,
       or seed initialization fails. The status bar explains the blocker. */
    a->stage=ST_FORM;
    logline(a,"XSSH: original Xbox SSH2 terminal / interactive login v3");
    logline(a,a->gpu_active?"Video renderer: NV2A pbGL (GPU tiles)":
                          "Video renderer: SDL2 software (GPU disabled/fallback)");
    logline(a,XSSH_BUILD_ID);
    {char info[100];
     snprintf(info,sizeof(info),"Video: %s; font: %d; terminal: %dx%d",
              a->video.description,a->settings.font_size,a->term.cols,a->term.rows);
     logline(a,info);
    }
    if(a->video.warning[0])logline(a,a->video.warning);
    xssh_form_init(&a->form,a->settings.host,a->settings.port,a->settings.user);
    snprintf(a->form.key_file,sizeof(a->form.key_file),"%s",a->settings.key_file);
    a->form.replace_on_type[XSSH_FORM_KEY_FILE]=a->form.key_file[0]!=0;
    if(!settings_ok){
        status(a,settings_result == -5 ?
               "Invalid LED or screensaver setting in D:\\xssh.cfg" :
               settings_result == -4 ?
               "Invalid font, style or text_antialiasing in D:\\xssh.cfg" :
               settings_result == -3 ?
               "Invalid theme: use xbox or inverted_xbox in D:\\xssh.cfg" :
               settings_result == -2 ?
               "Invalid network settings in D:\\xssh.cfg" :
               "Missing/invalid D:\\xssh.cfg; deploy it beside default.xbe");
    } else {
        char error[120];
        if(xssh_network_initialize(&a->settings.network,&a->network,error,sizeof(error))!=0)
            status(a,error[0]?error:"Network initialization failed");
        else {
            char line[120];
            snprintf(line,sizeof(line),"Network: %s, Xbox IP: %s",
                     xssh_network_mode_name(a->network.mode),a->network.address);
            logline(a,line);
            if(xssh_rng_prepare()!=0)
                status(a,"Cannot read/update D:\\xssh-seed.bin (64 bytes; writable HDD required)");
            else if(libssh2_init(0)!=0)
                status(a,"libssh2_init failed");
            else {
                a->ready=1; a->stage=ST_FORM;
                status(a,"Enter server, user, optional key file and secret; B/Enter advances");
            }
        }
    }
    /* SDL's Xbox controller backend initialized usbh_core_init() above.
       Register the HID class before the first USB hub poll: the Makefile
       disables SDL's 500ms early joystick enumeration for this reason. */
    xssh_usb_keyboard_init();
    SDL_StartTextInput();
    snprintf(a->cpu_status,sizeof(a->cpu_status),"CPU --%%");
    a->cpu_sample_start_ticks=SDL_GetPerformanceCounter();
    a->last_rendered_stage=(Stage)-1;
    while(a->running){
        uint64_t cpu_loop_start=SDL_GetPerformanceCounter();
        SDL_Event ev;
        while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_TEXTINPUT && a->swallow_drum_text){
                a->swallow_drum_text=0;continue;
            }
            if(ev.type==SDL_KEYUP && (ev.key.keysym.sym==SDLK_o ||
               ev.key.keysym.sym==SDLK_LCTRL || ev.key.keysym.sym==SDLK_RCTRL ||
               ev.key.keysym.sym==SDLK_LSHIFT || ev.key.keysym.sym==SDLK_RSHIFT))
                a->wallpaper_hold=0;
            if(ev.type==SDL_KEYDOWN)a->swallow_drum_text=0;
            if(ev.type==SDL_TEXTINPUT && a->swallow_wake_text){
                a->swallow_wake_text=0;
                continue;
            }
            /* Process active drum controls before the generic screensaver wake
               handler.  They neither dismiss the saver nor reset idle time. */
            if(ev.type==SDL_KEYDOWN){
                SDL_Keycode sym=ev.key.keysym.sym;
                int wallpaper_shortcut=(ev.key.keysym.mod & (KMOD_CTRL|KMOD_SHIFT))==
                    (KMOD_CTRL|KMOD_SHIFT) && (sym==SDLK_b || sym==SDLK_o || sym==SDLK_m);
                if(wallpaper_shortcut){a->swallow_drum_text=1;keyboard_event(a,&ev.key);continue;}
                int toggle=(ev.key.keysym.mod & KMOD_CTRL) && sym==SDLK_SPACE;
                int drum_key=a->drums.enabled &&
                    ((sym>=SDLK_KP_0 && sym<=SDLK_KP_9) ||
                     sym==SDLK_KP_PLUS || sym==SDLK_KP_MINUS);
                if(toggle || drum_key){
                    a->swallow_drum_text=1;
                    keyboard_event(a,&ev.key);
                    continue;
                }
            }
            if(ev.type==SDL_KEYDOWN || ev.type==SDL_TEXTINPUT || ev.type==SDL_CONTROLLERBUTTONDOWN){
                a->last_input_ms=SDL_GetTicks();
                if(a->saver_active){
                    a->swallow_wake_text=(ev.type==SDL_KEYDOWN);
                    saver_stop(a);continue;
                }
                if(ev.type==SDL_KEYDOWN)a->swallow_wake_text=0;
            }
            if(ev.type==SDL_QUIT)a->running=0;
            else if(ev.type==SDL_KEYDOWN){
                SDL_Keycode sym=ev.key.keysym.sym;
                if((ev.key.keysym.mod & KMOD_CTRL) && sym==SDLK_SPACE)
                    a->swallow_drum_text=1;
                else if(a->drums.enabled && ((sym>=SDLK_KP_0 && sym<=SDLK_KP_9) ||
                        sym==SDLK_KP_PLUS || sym==SDLK_KP_MINUS))a->swallow_drum_text=1;
                keyboard_event(a,&ev.key);
            }
            else if(ev.type==SDL_TEXTINPUT && (a->stage==ST_FORM || a->stage==ST_CONNECTED)){
                for(const unsigned char *p=(const unsigned char*)ev.text.text;*p;p++)
                    if(*p>=32&&*p<127 && !(a->stage==ST_FORM && a->menu_focus))type_byte(a,*p);
            }
        }
        if(a->wallpaper_hold){
            unsigned int t=SDL_GetTicks();
            if((int32_t)(t-a->wallpaper_next_step)>=0){
                wallpaper_step(a);a->wallpaper_next_step=t+130u;
            }
        }
        /* Send SDL keyboard input before USB polling, network maintenance,
           terminal reads, audio work or rendering can delay it. */
        ssh_flush_outgoing(a);
        {unsigned int now=SDL_GetTicks();
         if(now-a->last_network_poll>=500){
             a->last_network_poll=now;
             xssh_network_poll(&a->network);
         }
         xssh_usb_keyboard_poll(now,usb_key_event,a);}
        poll_pad(a);
        /* USB HID and controller input are also sent in the same iteration. */
        ssh_flush_outgoing(a);
        resize_for_keyboard(a,0);
        ssh_pump(a);
        xssh_drums_pump(&a->drums);
        xssh_mp3_pump(&a->music);
        {char mp3_notice[160];
         if(xssh_mp3_take_status(&a->music,mp3_notice,sizeof(mp3_notice)))
             status_bar_only(a,mp3_notice);}
        {unsigned int now=SDL_GetTicks();
         if(now-a->last_ram_poll_ms>=1000u){
             MM_STATISTICS mem;
             memset(&mem,0,sizeof(mem));
             mem.Length=sizeof(mem);
             MmQueryStatistics(&mem);
             if(mem.TotalPhysicalPages && mem.AvailablePages<=mem.TotalPhysicalPages){
                 unsigned used=(unsigned)(((uint64_t)(mem.TotalPhysicalPages-mem.AvailablePages)*100u+
                                         mem.TotalPhysicalPages/2u)/mem.TotalPhysicalPages);
                 snprintf(a->ram_status,sizeof(a->ram_status),"RAM %u%%",used);
             }else snprintf(a->ram_status,sizeof(a->ram_status),"RAM --%%");
             a->last_ram_poll_ms=now;
             a->ui_status_dirty=1;
         }
         if(a->volume_status[0] &&
            (int32_t)(now-a->volume_status_until_ms)>=0){
             a->volume_status[0]=0;
             a->ui_status_dirty=1;
         }
        }
        {unsigned int now=SDL_GetTicks();
         if(!a->saver_active &&
            (unsigned int)(now-a->last_input_ms)>=(unsigned int)a->settings.screensaver_seconds*1000u)
             saver_start(a,now);
         update_front_led(a);
         /* Skip unnecessary full-screen redraws while the terminal is idle.
            Keystrokes and incoming SSH text still trigger an immediate redraw
            on the next ~30 FPS boundary. */
         /* Limit animation work during music, but keep drawing even if
            the audio queue stalls or drains at the end of a song. */
         /* The GPU screensaver blits one native-size texture; do not force
            it down to ~6 FPS just because MP3 audio is playing. */
         /* Leave time for USB polling between input and status changes.
            Even at 720p the GPU path rasterizes the form on the CPU. */
         int high_res_sdl=!a->gpu_active && a->video.width>=1920 && a->video.height>=1080;
         unsigned render_interval=a->saver_active ? (high_res_sdl?50u:33u) :
                                  (high_res_sdl?33u:16u);
         if(now-a->last_render_ms>=render_interval){
             if(a->saver_active){
                 saver_draw(a,now);a->last_render_ms=now;
             }
             else if(a->stage!=ST_CONNECTED ?
                 (a->stage!=a->last_rendered_stage ||
                  a->last_render_ms==0 || a->last_input_ms>=a->last_render_ms ||
                  a->ui_status_dirty || a->font_resize_pending) :
                 (a->term.dirty || !a->cache_valid || a->cache_scan_active || a->ui_status_dirty)){
                 /* Never gate keyboard echo or terminal repaint on audio queue
                    occupancy: a low/failed queue could otherwise freeze the
                    display indefinitely. Audio has its own worker. */
                 render(a);a->ui_status_dirty=0;a->last_render_ms=now;
             }
         }
        }
        if(!a->running && !xssh_drums_shutdown(&a->drums)){
            a->running=1;
            status_bar_only(a,"Drum audio shutdown pending; exit again when stopped");
        }
        if(!a->running && !xssh_mp3_stop(&a->music)){
            a->running=1;
            status_bar_only(a,"Audio shutdown pending; exit again when stopped");
        }
        /* Approximate application CPU load from time spent working versus
           time yielded by SDL_Delay. This is not whole-system CPU usage. */
        uint64_t cpu_before_yield=SDL_GetPerformanceCounter();
        SDL_Delay(2);
        uint64_t cpu_after_yield=SDL_GetPerformanceCounter();
        a->cpu_busy_ticks+=cpu_before_yield-cpu_loop_start;
        a->cpu_total_ticks+=cpu_after_yield-cpu_loop_start;
        uint64_t cpu_frequency=SDL_GetPerformanceFrequency();
        if(cpu_frequency &&
           cpu_after_yield-a->cpu_sample_start_ticks>=cpu_frequency){
            unsigned percent=a->cpu_total_ticks ?
                (unsigned)((a->cpu_busy_ticks*100u+a->cpu_total_ticks/2u)/a->cpu_total_ticks) : 0u;
            if(percent>100u)percent=100u;
            snprintf(a->cpu_status,sizeof(a->cpu_status),"CPU %u%%",percent);
            a->cpu_busy_ticks=0;
            a->cpu_total_ticks=0;
            a->cpu_sample_start_ticks=cpu_after_yield;
            a->ui_status_dirty=1;
        }
    }
    saver_stop(a);
    xssh_led_restore();
    disconnect(a);
    libssh2_exit();
    if(a->pad)SDL_GameControllerClose(a->pad);
    if(a->term_cache)SDL_DestroyTexture(a->term_cache);
    if(a->wallpaper_texture)SDL_DestroyTexture(a->wallpaper_texture);
    if(a->font)SDL_DestroyTexture(a->font);
    if(a->font_aa)SDL_DestroyTexture(a->font_aa);
    SDL_DestroyRenderer(a->renderer);
    if(a->gpu_active)xssh_gpu_shutdown(&a->gpu);
    if(a->gpu_surface)SDL_FreeSurface(a->gpu_surface);
    if(a->window)SDL_DestroyWindow(a->window);
    SDL_Quit();free(a);
    return 0;
}
