#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <nxaudio.h>
#include "../src/xssh_mp3.c"
#include "../src/xssh_audio.c"

static XsshMp3 music;
static uint32_t clock_ms;
static unsigned voice_starts, allocations, priority_requests, completions;
static uint64_t consumed;
static uint32_t next_due, last_end, next_start;
static const nxAudioBuffer *pending[2];
static unsigned count;
static nxAudioVoice *voice;
static long initial_file_position;
static int fault, seek_test;
static unsigned seek_stage;

Uint32 SDL_GetTicks(void){return clock_ms;}
int SDL_SetThreadPriority(SDL_ThreadPriority p){
    assert(p==SDL_THREAD_PRIORITY_TIME_CRITICAL);
    priority_requests++;return 0;
}
void SDL_Delay(Uint32 delay){
    clock_ms+=delay;
    if(count && voice && voice->state==NX_PLAYING && clock_ms>=next_due){
        const nxAudioBuffer *done=pending[0];
        pending[0]=pending[1];count--;
        for(unsigned i=0;i<2;i++)if(voice->buffers_hardware[i]==done)voice->buffers_hardware[i]=NULL;
        consumed+=done->size_bytes;completions++;
        if(voice->callback)voice->callback(voice,done,voice->user_context);
        last_end=next_due;
        if(count)next_due=clock_ms+(uint32_t)((uint64_t)pending[0]->size_bytes*1000/192000);
        if(!seek_test && voice_starts>=2)music.stop_requested=1;
        if(seek_test && completions>=500 && seek_stage==0){
            music.seek_request=-1;seek_stage=1;
        }else if(seek_test && completions>=503 && seek_stage==1){
            music.seek_request=1;seek_stage=2;
        }else if(seek_test && completions>=506 && seek_stage==2){
            music.skip_request=1;seek_stage=3;
        }else if(seek_test && completions>=508 && seek_stage==3)music.stop_requested=1;
    }
    assert(clock_ms<400000);
}
void *MmAllocateContiguousMemoryEx(size_t n,uintptr_t l,uintptr_t h,size_t a,unsigned p){
    (void)l;(void)h;(void)a;(void)p;allocations++;return malloc(n);
}
void MmFreeContiguousMemory(void *p){free(p);}
bool nxAudioInit(const nxAudioInitParams *p){(void)p;return !fault;}
void nxAudioShutdown(void){}
bool nxAudioFaulted(void){return fault!=0;}
void nxAudioAbort(void){fault=1;}
bool nxAudioVoiceCreate(nxAudioVoice *v,const nxAudioFormat *f){
    memset(v,0,sizeof(*v));v->format=*f;v->state=NX_STOPPED;voice=v;return true;
}
void nxAudioBufferSetCallback(nxAudioVoice *v,nxAudioVoiceCallback cb,void *ctx){v->callback=cb;v->user_context=ctx;}
bool nxAudioBufferInitialize(nxAudioBuffer *b,const void *p,uint32_t n){b->buffer=p;b->size_bytes=n;return true;}
bool nxAudioBufferQueue(nxAudioVoice *v,const nxAudioBuffer *b){
    assert(count<2);
    pending[count++]=b;
    for(unsigned i=0;i<2;i++)if(!v->buffers_hardware[i]){v->buffers_hardware[i]=b;break;}
    if(count==1)next_due=clock_ms+(uint32_t)((uint64_t)b->size_bytes*1000/192000);
    return true;
}
bool nxAudioVoiceStart(nxAudioVoice *v){
    v->state=NX_PLAYING;voice_starts++;
    if(voice_starts==1){
        initial_file_position=ftell(music.track_file);
        assert(clock_ms<500);
    }else if(voice_starts==2)next_start=clock_ms;
    return true;
}
bool nxAudioVoiceStop(nxAudioVoice *v){
    v->state=NX_STOPPED;count=0;v->buffers_hardware[0]=v->buffers_hardware[1]=NULL;return true;
}
nxAudioVoiceState nxAudioVoiceGetState(const nxAudioVoice *v){return v->state;}
void nxAudioVoiceDestroy(nxAudioVoice *v){assert(v->state==NX_STOPPED);}
bool nxAudioVoiceSetMasterGain(nxAudioVoice *v,float gain){(void)v;assert(gain>=0 && gain<=1);return true;}

/* Compare restored decoder output with an uninterrupted decode, including
   the bit reservoir, pending partial frames, VBR and checkpoint compaction. */
static void test_checkpoints(const char *filename){
    xssh_mp3_init(&music);
    music.mutex=SDL_CreateMutex();
    music.track_count=1;music.playing=1;
    strcpy(music.names[0],filename);
    assert(open_next(&music,0));
    drmp3 *mp=music.decoder;
    const size_t capacity=(size_t)mp->sampleRate*340;
    drmp3_int16 *reference=malloc(capacity*mp->channels*sizeof(*reference));
    assert(reference);
    size_t total=0;
    while(total<capacity){
        io_budget(&music);
        size_t frames=drmp3_read_pcm_frames_s16(mp,997,reference+total*mp->channels);
        if(!frames)break;
        total+=frames;remember_position(&music);
    }
    assert(total<capacity);
    MusicSeekCache *cache=music.seek_points;
    assert(cache && cache->count<=MUSIC_CHECKPOINTS);
    if(total>300u*mp->sampleRate)assert(cache->spacing>mp->sampleRate);
    drmp3_uint64 targets[]={total-6000,total/2,12345,0,total-10000,total/3};
    drmp3_int16 result[4096*2];
    for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);i++){
        drmp3_uint64 target=targets[i];
        io_budget(&music);
        assert(restore_position(&music,target));
        drmp3_uint64 cursor=drmp3_get_cursor_in_pcm_frames(mp);
        assert(cursor<=target);
        assert(target-cursor<=cache->spacing+MUSIC_DECODE_FRAMES);
        while(cursor<target){
            size_t count=target-cursor>997?997:(size_t)(target-cursor);
            assert(drmp3_read_pcm_frames_s16(mp,count,result)==count);
            cursor+=count;
        }
        assert(drmp3_read_pcm_frames_s16(mp,4096,result)==4096);
        assert(!memcmp(result,reference+target*mp->channels,4096*mp->channels*sizeof(*result)));
    }
    printf("PASS: %s checkpoint PCM matches linear decode; cache=%zu bytes, spacing=%llu frames\n",
        filename,sizeof(*cache),(unsigned long long)cache->spacing);
    free(reference);close_track(&music);SDL_DestroyMutex(music.mutex);
}

int main(int argc,char **argv){
    assert(argc>=2);
    if(argc>2 && !strcmp(argv[2],"checkpoints")){
        assert(chdir(argv[1])==0);
        test_checkpoints("long.mp3");
        test_checkpoints("vbr.mp3");
        test_checkpoints("short.mp3");
        return 0;
    }
    seek_test=argc>2;
    assert(chdir(argv[1])==0);
    struct stat info;assert(stat("long.mp3",&info)==0);
    xssh_mp3_init(&music);
    music.mutex=SDL_CreateMutex();
    assert(music.mutex);
    music.track_count=2;music.playing=1;
    strcpy(music.names[0],"long.mp3");strcpy(music.names[1],"short.mp3");
    mp3_worker(&music);
    assert(!fault);
    assert(allocations==2 && priority_requests==1);
    assert(voice_starts>=2 && music.worker_finished);
    assert(initial_file_position<info.st_size/10);
    if(!seek_test)assert(consumed>50u*1024u*1024u);
    else assert(seek_stage==3);
    SDL_DestroyMutex(music.mutex);
    printf("PASS: real decoder %s, bytes=%llu, first_read=%ld/%ld, voices=%u, simulated_ms=%u\n",
           seek_test?"seek/skip":"long-song and mixed-format transition",
           (unsigned long long)consumed,initial_file_position,(long)info.st_size,voice_starts,clock_ms);
}
