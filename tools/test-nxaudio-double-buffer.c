#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <nxaudio.h>
#include "xssh_audio.h"

#define CHUNK (96u*1024u)
static unsigned tick, allocations, freed, queued_count, destroyed;
static int fault, stop_stuck, queue_fail;
static float gain;
static nxAudioVoice *active;

unsigned SDL_GetTicks(void){return tick;}
void SDL_Delay(unsigned ms){tick+=ms;}
void *MmAllocateContiguousMemoryEx(size_t n,uintptr_t l,uintptr_t h,size_t a,unsigned p){
    (void)l;(void)h;(void)a;(void)p;allocations++;return malloc(n);
}
void MmFreeContiguousMemory(void *p){freed++;free(p);}
bool nxAudioInit(const nxAudioInitParams *p){(void)p;return !fault;}
void nxAudioShutdown(void){}
bool nxAudioFaulted(void){return fault!=0;}
void nxAudioAbort(void){fault=1;}
bool nxAudioVoiceCreate(nxAudioVoice *v,const nxAudioFormat *f){
    memset(v,0,sizeof(*v));v->format=*f;v->state=NX_STOPPED;active=v;return true;
}
void nxAudioBufferSetCallback(nxAudioVoice *v,nxAudioVoiceCallback cb,void *ctx){
    v->callback=cb;v->user_context=ctx;
}
bool nxAudioBufferInitialize(nxAudioBuffer *b,const void *p,uint32_t n){
    b->buffer=p;b->size_bytes=n;return true;
}
bool nxAudioBufferQueue(nxAudioVoice *v,const nxAudioBuffer *b){
    if(queue_fail)return false;
    for(unsigned i=0;i<2;i++)if(!v->buffers_hardware[i]){
        v->buffers_hardware[i]=b;queued_count++;return true;
    }
    assert(!"third buffer queued");return false;
}
bool nxAudioVoiceStart(nxAudioVoice *v){v->state=NX_PLAYING;return true;}
bool nxAudioVoiceStop(nxAudioVoice *v){
    assert(!v->callback);
    if(!stop_stuck)v->state=NX_STOPPED;
    return true;
}
nxAudioVoiceState nxAudioVoiceGetState(const nxAudioVoice *v){return v->state;}
void nxAudioVoiceDestroy(nxAudioVoice *v){
    assert(v->state==NX_STOPPED && !fault);destroyed++;
    v->buffers_hardware[0]=v->buffers_hardware[1]=NULL;
}
bool nxAudioVoiceSetMasterGain(nxAudioVoice *v,float g){(void)v;gain=g;return true;}
static void complete(unsigned i){
    const nxAudioBuffer *b=active->buffers_hardware[i];
    assert(b);active->buffers_hardware[i]=NULL;
    tick+=512;
    active->callback(active,b,active->user_context);
}
static void submit(size_t bytes, unsigned value){
    uint8_t *data=NULL;size_t capacity=0;
    assert(xssh_audio_acquire(&data,&capacity)==1 && capacity==CHUNK);
    memset(data,value,bytes);
    assert(xssh_audio_commit(bytes));
}
int main(int argc,char **argv){
    (void)argv;
    uint64_t played;
    uint32_t queued;
    uint8_t *data=NULL;size_t capacity=0;
    assert(xssh_audio_open() && allocations==2);
    assert(xssh_audio_begin(70) && queued_count==0);
    tick+=10000;
    assert(xssh_audio_poll(70,&played,&queued) && played==0 && queued==0);
    assert(xssh_audio_acquire(&data,&capacity)==1);
    assert(!xssh_audio_commit(0) && !xssh_audio_commit(3));
    assert(!xssh_audio_commit(CHUNK+4));
    submit(24u*1024u,1);
    assert(active->state==NX_PLAYING && queued_count==1);
    submit(CHUNK,2);
    assert(xssh_audio_acquire(&data,&capacity)==0);
    if(argc>1){
        unsigned before=destroyed;
        stop_stuck=1;
        unsigned before_tick=tick;
        assert(!xssh_audio_stop());
        assert(fault && tick-before_tick>=500 && tick-before_tick<=501);
        assert(destroyed==before && freed==0);
        assert(!xssh_audio_close() && freed==0);
        assert(!xssh_audio_open());
        puts("PASS: stop timeout retains DMA memory and blocks restart");
        return 0;
    }
    assert(xssh_audio_poll(70,&played,&queued));
    assert(played==0 && queued==CHUNK+24u*1024u);
    complete(0);
    assert(xssh_audio_poll(25,&played,&queued) && played==24u*1024u && queued==CHUNK);
    assert(gain==0.25f);
    submit(20,3);
    complete(1);
    assert(xssh_audio_poll(25,&played,&queued) && played==CHUNK+24u*1024u && queued==20);
    complete(0);
    assert(xssh_audio_poll(25,&played,&queued) && queued==0);
    /* Producer starvation differs from a hardware completion stall. */
    tick+=10000;
    assert(xssh_audio_poll(25,&played,&queued));
    assert(xssh_audio_begin(100));
    /* Over 6 GiB of simulated PCM, with only two fixed hardware allocations. */
    for(unsigned i=0;i<70000;i++){
        assert(xssh_audio_acquire(&data,&capacity)==1);
        data[0]=(uint8_t)i;
        assert(xssh_audio_commit(CHUNK));
        complete(0);
        assert(xssh_audio_poll(100,&played,&queued));
        assert(played==(uint64_t)(i+1)*CHUNK && queued==0);
    }
    assert(allocations==2);
    queue_fail=1;
    assert(xssh_audio_acquire(&data,&capacity)==1);
    assert(!xssh_audio_commit(4));
    queue_fail=0;
    assert(xssh_audio_stop());
    assert(xssh_audio_close() && freed==2);
    assert(xssh_audio_open() && xssh_audio_begin(100));
    submit(CHUNK,4);
    tick+=3001;
    assert(!xssh_audio_poll(100,&played,&queued) && fault);
    assert(!xssh_audio_close() && freed==2);
    puts("PASS: live producer, bounded buffers, short tail, volume, 64-bit progress, starvation and hardware timeout");
}
