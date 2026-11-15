#include "xssh_audio.h"
#include <SDL.h>
#include <nxaudio.h>
#include <stdatomic.h>
#include <string.h>

/* 512 ms per slot at 48 kHz / stereo / s16. Each is physically contiguous,
   so the driver's streaming descriptor list needs only one entry. */
#define SLOT_BYTES (96u * 1024u)
#define STOP_TIMEOUT_MS 500u
#define PROGRESS_TIMEOUT_MS 3000u

typedef struct {
    nxAudioBuffer buffer;
    uint8_t *data;
    uint64_t start;
    size_t length;
    int in_use;
} AudioSlot;

static struct {
    nxAudioVoice voice;
    AudioSlot slots[2];
    atomic_uint completed;
    uint64_t submitted, played;
    int write_slot, started;
    Uint32 last_completion;
    int initialized, voice_created, volume;
} output;

static void completed_buffer(nxAudioVoice *voice, const nxAudioBuffer *buffer, void *context)
{
    (void)voice;
    (void)context;
    /* DISPATCH_LEVEL: no allocation, disk I/O, SDL calls or PCM copying. */
    if(buffer == &output.slots[0].buffer)
        atomic_fetch_or_explicit(&output.completed, 1u, memory_order_release);
    else if(buffer == &output.slots[1].buffer)
        atomic_fetch_or_explicit(&output.completed, 2u, memory_order_release);
}

int xssh_audio_faulted(void)
{
    return nxAudioFaulted();
}

int xssh_audio_open(void)
{
    if(nxAudioFaulted())return 0;
    if(output.initialized)return 1;
    nxAudioInitParams parameters = {0};
    if(!nxAudioInit(&parameters))return 0;
    output.initialized=1;
    for(unsigned i=0;i<2;i++){
        output.slots[i].data=MmAllocateContiguousMemoryEx(
            SLOT_BYTES,0,0xFFFFFFFF,PAGE_SIZE,PAGE_READWRITE);
        if(!output.slots[i].data){
            xssh_audio_close();
            return 0;
        }
    }
    return 1;
}

int xssh_audio_acquire(uint8_t **data, size_t *capacity)
{
    if(!output.voice_created || nxAudioFaulted())return -1;
    if(output.write_slot<0){
        for(unsigned i=0;i<2;i++){
            if(!output.slots[i].in_use){output.write_slot=(int)i;break;}
        }
    }
    if(output.write_slot<0)return 0;
    *data=output.slots[output.write_slot].data;
    *capacity=SLOT_BYTES;
    return 1;
}

int xssh_audio_commit(size_t length)
{
    if(nxAudioFaulted() || output.write_slot<0 || !length ||
       length>SLOT_BYTES || (length&3))return 0;
    AudioSlot *slot=&output.slots[output.write_slot];
    slot->start=output.submitted;
    slot->length=length;
    if(!nxAudioBufferInitialize(&slot->buffer,slot->data,(uint32_t)length) ||
       !nxAudioBufferQueue(&output.voice,&slot->buffer) || nxAudioFaulted())
        return 0;
    if(output.submitted==output.played)output.last_completion=SDL_GetTicks();
    slot->in_use=1;
    output.submitted+=length;
    output.write_slot=-1;
    if(!output.started){
        if(!nxAudioVoiceStart(&output.voice) || nxAudioFaulted())return 0;
        output.started=1;
    }
    return 1;
}

int xssh_audio_stop(void)
{
    if(nxAudioFaulted())return 0;
    if(!output.voice_created)return 1;
    nxAudioBufferSetCallback(&output.voice,NULL,NULL);
    if(!nxAudioVoiceStop(&output.voice) || nxAudioFaulted())return 0;
    Uint32 start=SDL_GetTicks();
    while(nxAudioVoiceGetState(&output.voice)!=NX_STOPPED){
        if(nxAudioFaulted())return 0;
        if((Uint32)(SDL_GetTicks()-start)>=STOP_TIMEOUT_MS){
            nxAudioAbort();
            return 0;
        }
        SDL_Delay(1);
    }
    nxAudioVoiceDestroy(&output.voice);
    if(nxAudioFaulted())return 0;
    output.voice_created=0;
    atomic_store_explicit(&output.completed,0,memory_order_relaxed);
    for(unsigned i=0;i<2;i++)output.slots[i].in_use=0;
    output.write_slot=-1;
    output.started=0;
    return 1;
}

int xssh_audio_begin(int volume)
{
    if(!output.initialized || nxAudioFaulted())return 0;
    if(!xssh_audio_stop())return 0;
    output.submitted=output.played=0;
    output.write_slot=-1;
    output.started=0;
    output.volume=volume;
    output.last_completion=SDL_GetTicks();
    nxAudioFormat format={
        .sample_rate=48000, .channels=2, .bytes_per_sample=2,
        .codec=NX_AUDIO_CODEC_PCM, .type=NX_VOICE_TYPE_2D_STREAM
    };
    if(!nxAudioVoiceCreate(&output.voice,&format) || nxAudioFaulted())return 0;
    output.voice_created=1;
    nxAudioBufferSetCallback(&output.voice,completed_buffer,NULL);
    return nxAudioVoiceSetMasterGain(&output.voice,(float)volume/100.0f) &&
           !nxAudioFaulted();
}

int xssh_audio_poll(int volume, uint64_t *played, uint32_t *queued)
{
    if(nxAudioFaulted())return 0;
    unsigned completed=atomic_exchange_explicit(&output.completed,0,memory_order_acquire);
    for(unsigned i=0;i<2;i++){
        AudioSlot *slot=&output.slots[i];
        if((completed&(1u<<i)) && slot->in_use){
            uint64_t end=slot->start+slot->length;
            if(end>output.played)output.played=end;
            slot->in_use=0;
            output.last_completion=SDL_GetTicks();
        }
    }
    /* A paused/broken device must not look healthy merely because polling runs. */
    if(output.started && output.played<output.submitted &&
       (Uint32)(SDL_GetTicks()-output.last_completion)>PROGRESS_TIMEOUT_MS){
        nxAudioAbort();
        return 0;
    }
    if(output.voice_created && volume!=output.volume){
        if(!nxAudioVoiceSetMasterGain(&output.voice,(float)volume/100.0f) ||
           nxAudioFaulted())return 0;
        output.volume=volume;
    }
    *played=output.played;
    *queued=(uint32_t)(output.submitted-output.played);
    return 1;
}

int xssh_audio_close(void)
{
    if(!xssh_audio_stop()){
        nxAudioShutdown(); /* detach CPU callbacks, retain uncertain DMA allocations */
        return 0;
    }
    if(output.initialized){
        nxAudioShutdown();
        if(nxAudioFaulted())return 0;
        output.initialized=0;
    }
    for(unsigned i=0;i<2;i++){
        if(output.slots[i].data)MmFreeContiguousMemory(output.slots[i].data);
        output.slots[i].data=NULL;
    }
    return 1;
}
