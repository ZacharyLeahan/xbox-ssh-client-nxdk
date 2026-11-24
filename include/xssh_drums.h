#ifndef XSSH_DRUMS_H
#define XSSH_DRUMS_H
#include <nxaudio.h>
#include <stdint.h>
typedef struct XsshDrumVoice {
    nxAudioVoice voice;
    struct XsshDrumVoice *next;
    uint32_t started_ms;
    int stopping;
} XsshDrumVoice;
typedef struct { nxAudioBuffer buffer; void *pcm; unsigned bytes; int created; } XsshDrumSample;
typedef struct { XsshDrumSample sample[9]; XsshDrumVoice *voices; int enabled,volume,loaded; } XsshDrums;
void xssh_drums_toggle(XsshDrums *d);
void xssh_drums_play(XsshDrums *d,int pad);
void xssh_drums_volume(XsshDrums *d,int direction);
void xssh_drums_pump(XsshDrums *d);
int xssh_drums_shutdown(XsshDrums *d);
#endif
