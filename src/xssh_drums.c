/* Nonblocking drum-pad playback using separate nxAudio hardware voices.
   Files are 44.1 kHz mono WAV; SDL converts them once to 48 kHz stereo PCM. */
#include "xssh_drums.h"
#include "xssh_audio.h"
#include <SDL.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define DRUM_COUNT 9
static const char *files[DRUM_COUNT]={"1_kick.wav","2_snare.wav","3_hi_hat_closed.wav", "4_hi_hat_open.wav","5_tom_low.wav","6_tom_mid.wav","7_tom_high.wav","8_cymbal_crash.wav","9_cymbal_ride.wav"};
static int load_one(XsshDrumSample *s,int n){
    char path[120]; SDL_AudioSpec spec; Uint8 *raw=NULL; Uint32 length=0;
    snprintf(path,sizeof(path),"D:\\drums\\%s",files[n]);
    if(!SDL_LoadWAV(path,&spec,&raw,&length))return 0;
    SDL_AudioCVT cvt;
    if(SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16SYS,2,48000)<0){SDL_FreeWAV(raw);return 0;}
    if(length>UINT32_MAX/(unsigned)cvt.len_mult){SDL_FreeWAV(raw);return 0;}
    cvt.len=(int)length;
    cvt.buf=(Uint8*)SDL_malloc((size_t)length*cvt.len_mult);
    if(!cvt.buf){SDL_FreeWAV(raw);return 0;}
    memcpy(cvt.buf,raw,length);SDL_FreeWAV(raw);
    if(SDL_ConvertAudio(&cvt)<0){SDL_free(cvt.buf);return 0;}
    unsigned bytes=(unsigned)cvt.len_cvt & ~3u;
    if(!bytes){SDL_free(cvt.buf);return 0;}
    s->pcm=MmAllocateContiguousMemoryEx(bytes,0,0xFFFFFFFF,PAGE_SIZE,PAGE_READWRITE);
    if(!s->pcm){SDL_free(cvt.buf);return 0;}
    memcpy(s->pcm,cvt.buf,bytes);SDL_free(cvt.buf);s->bytes=bytes;
    if(!nxAudioBufferInitialize(&s->buffer,s->pcm,bytes)){
        MmFreeContiguousMemory(s->pcm);s->pcm=NULL;s->bytes=0;
        return 0;
    }
    s->created=1;
    return 1;
}
void xssh_drums_toggle(XsshDrums *d){
    if(d->enabled){d->enabled=0;return;}
    if(!d->loaded){
        if(!xssh_audio_open())return;
        for(int i=0;i<DRUM_COUNT;i++)load_one(&d->sample[i],i);
        d->loaded=1;
        if(!d->volume)d->volume=70;
    }
    d->enabled=1;
}
/* Called on the main loop, never from the audio interrupt.  Duration is
   computed from the actual converted 48 kHz, stereo, 16-bit PCM byte count.
   Stop is asynchronous: only destroy after the APU reports NX_STOPPED. */
void xssh_drums_pump(XsshDrums *d){
    uint32_t now=SDL_GetTicks();
    XsshDrumVoice **link=&d->voices;
    while(*link){
        XsshDrumVoice *v=*link;
        if(!v->stopping && (int32_t)(now-v->started_ms)>=0){
            nxAudioVoiceStop(&v->voice);
            v->stopping=1;
        }
        if(v->stopping && nxAudioVoiceGetState(&v->voice)==NX_STOPPED){
            *link=v->next;
            nxAudioVoiceDestroy(&v->voice);
            free(v);
        }else link=&v->next;
    }
}
void xssh_drums_play(XsshDrums *d,int pad){
    if(!d->enabled||pad<1||pad>9)return;
    XsshDrumSample *s=&d->sample[pad-1];
    if(!s->created)return;
    XsshDrumVoice *v=(XsshDrumVoice*)calloc(1,sizeof(*v));
    if(!v)return;
    nxAudioFormat fmt={48000,2,2,NX_AUDIO_CODEC_PCM,NX_VOICE_TYPE_2D_STREAM};
    if(!nxAudioVoiceCreate(&v->voice,&fmt)){free(v);return;}
    nxAudioVoiceSetMasterGain(&v->voice,(float)d->volume/100.0f);
    if(!nxAudioBufferQueue(&v->voice,&s->buffer) || !nxAudioVoiceStart(&v->voice)){
        nxAudioVoiceStop(&v->voice);
        /* Keep failed starts tracked until the APU confirms they are stopped. */
        v->stopping=1;
    }
    /* One 48 kHz stereo 16-bit frame is 4 bytes.  Round duration upward,
       with a short hardware-drain margin before requesting asynchronous stop. */
    uint32_t duration_ms=(uint32_t)(((uint64_t)s->bytes*1000u+191999u)/192000u);
    v->started_ms=SDL_GetTicks()+duration_ms+20u;
    v->next=d->voices;
    d->voices=v;
}
void xssh_drums_volume(XsshDrums *d,int direction){
    int v=d->volume;v+=direction*5;
    if(v<0)v=0;if(v>100)v=100;d->volume=v;
    for(XsshDrumVoice *node=d->voices;node;node=node->next)
        if(!node->stopping)nxAudioVoiceSetMasterGain(&node->voice,(float)v/100.0f);
}
int xssh_drums_shutdown(XsshDrums *d){
    d->enabled=0;
    for(XsshDrumVoice *v=d->voices;v;v=v->next){
        if(!v->stopping){nxAudioVoiceStop(&v->voice);v->stopping=1;}
    }
    xssh_drums_pump(d);
    if(d->voices)return 0; /* Keep app alive until DMA is safe to release. */
    for(int i=0;i<DRUM_COUNT;i++){
        if(d->sample[i].pcm)MmFreeContiguousMemory(d->sample[i].pcm);
    }
    memset(d,0,sizeof(*d));
    return 1;
}
