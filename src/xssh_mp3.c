/* HDD MP3 streaming into nxdk-audio double buffers. */
#include "xssh_mp3.h"
#include "xssh_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#define DR_MP3_NO_STDIO
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define MUSIC_DIR "D:\\mp3s"
/* Give short video/terminal stalls room without accumulating seconds of lag. */
#define MUSIC_URGENT_MS 300u
#define MUSIC_DECODE_FRAMES 1152
/* Combine rapid track presses before touching the Xbox audio backend. */
#define MUSIC_SKIP_SETTLE_MS 160u
#define MUSIC_LOG_PATH "D:\\xssh-mp3.log"
#define MUSIC_WORKER_STALL_MS 3000u
#define MUSIC_DECODE_STALL_MS 30000u
#define MUSIC_RECOVERY_MAX_ATTEMPTS 3
#define MUSIC_RECOVERY_DELAY_MS 750u
/* Hard ceiling for PCM buffers managed by XSSH (decoder internals excluded). */
#define MUSIC_OUTPUT_RATE 48000
#define MUSIC_OUTPUT_CHANNELS 2
#define MUSIC_OUTPUT_BPS (MUSIC_OUTPUT_RATE*MUSIC_OUTPUT_CHANNELS*2u)
/* Start quickly, then refill full 512 ms hardware slots. */
#define MUSIC_START_BYTES (24u*1024u)
#define MUSIC_IO_BUDGET_MS 2000u

static uint32_t next_rand(XsshMp3 *m) {
    uint32_t x=m->rng ? m->rng : 0x67fac19bu;
    x^=x<<13;x^=x>>17;x^=x<<5;
    m->rng=x;return x;
}
static int is_mp3(const char *n) {
    size_t l=strlen(n);
    return l>4 && n[l-4]=='.' &&
        tolower((unsigned char)n[l-3])=='m' &&
        tolower((unsigned char)n[l-2])=='p' && n[l-1]=='3';
}
static void shuffle(XsshMp3 *m) {
    for(int i=m->track_count-1;i>0;--i){
        int j=(int)(next_rand(m)%(uint32_t)(i+1));
        char tmp[XSSH_MP3_NAME_SIZE];
        memcpy(tmp,m->names[i],sizeof(tmp));
        memcpy(m->names[i],m->names[j],sizeof(tmp));
        memcpy(m->names[j],tmp,sizeof(tmp));
    }
    m->next_track=0;
}
static int collect_tracks(XsshMp3 *m) {
    WIN32_FIND_DATAA fd;
    HANDLE h=FindFirstFileA(MUSIC_DIR "\\*",&fd);
    m->track_count=0;
    if(h==INVALID_HANDLE_VALUE)return 0;
    do {
        const char *n=fd.cFileName;
        size_t len=strlen(n);
        if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           len<XSSH_MP3_NAME_SIZE && is_mp3(n) &&
           m->track_count<XSSH_MP3_MAX_TRACKS)
            memcpy(m->names[m->track_count++],n,len+1);
    }while(FindNextFileA(h,&fd));
    FindClose(h);
    return m->track_count;
}
static int should_stop(XsshMp3 *m);
static int io_ready(XsshMp3 *m){
    if(should_stop(m))return 0;
    if(m->io_deadline && (Sint32)(SDL_GetTicks()-m->io_deadline)>=0){
        m->io_timed_out=1;
        return 0;
    }
    /* Decoder frame searches can issue many reads in a single call. */
    if((++m->io_calls & 15u)==0)SDL_Delay(1);
    return 1;
}
static void io_budget(XsshMp3 *m){
    m->io_timed_out=0;
    m->io_deadline=(SDL_GetTicks()+MUSIC_IO_BUDGET_MS)|1u;
}
/* Custom file callbacks avoid nxdk's missing fopen_s CRT entry point. */
static size_t mp3_read(void *user,void *out,size_t bytes){
    XsshMp3 *m=user;
    return io_ready(m)?fread(out,1,bytes,m->track_file):0;
}
static drmp3_bool32 mp3_seek(void *user,int offset,drmp3_seek_origin origin){
    XsshMp3 *m=user;
    if(!io_ready(m))return DRMP3_FALSE;
    int whence=origin==DRMP3_SEEK_CUR?SEEK_CUR:origin==DRMP3_SEEK_END?SEEK_END:SEEK_SET;
    return fseek(m->track_file,(long)offset,whence)==0;
}
static drmp3_bool32 mp3_tell(void *user,drmp3_int64 *pos){
    XsshMp3 *m=user;
    if(!io_ready(m))return DRMP3_FALSE;
    long n=ftell(m->track_file);
    if(n<0)return DRMP3_FALSE;
    *pos=(drmp3_int64)n;
    return DRMP3_TRUE;
}
static int should_stop(XsshMp3 *m){
    int stop;
    SDL_LockMutex(m->mutex); stop=m->stop_requested; SDL_UnlockMutex(m->mutex);
    return stop;
}
static void worker_message(XsshMp3 *m,const char *msg){
    SDL_LockMutex(m->mutex);
    snprintf(m->message,sizeof(m->message),"%s",msg);
    SDL_UnlockMutex(m->mutex);
}
/* Stage tracking is cheap; a separate logger performs diagnostic disk I/O. */
static void worker_stage(XsshMp3 *m,const char *stage){
    SDL_LockMutex(m->mutex);
    snprintf(m->worker_stage,sizeof(m->worker_stage),"%s",stage);
    m->worker_stage_since_ms=SDL_GetTicks();
    m->worker_heartbeat_ms=m->worker_stage_since_ms;
    SDL_UnlockMutex(m->mutex);
}
/* One detached logger owns a copied record. Disk stalls cannot block the UI.
   While it is busy, subsequent records are dropped to keep memory bounded. */
static SDL_atomic_t logger_busy;
static char logger_record[512];
static int log_worker(void *unused){
    (void)unused;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    FILE *f=fopen(MUSIC_LOG_PATH,"a");
    if(f){fputs(logger_record,f);fclose(f);}
    SDL_AtomicSet(&logger_busy,0);
    return 0;
}
static void log_event_main(const char *event,const char *stage,const char *track){
    if(!SDL_AtomicCAS(&logger_busy,0,1))return;
    snprintf(logger_record,sizeof(logger_record),
             "tick=%lu event=%s stage=%s track=%s\n",
             (unsigned long)SDL_GetTicks(),event,stage,track);
    SDL_Thread *thread=SDL_CreateThread(log_worker,"mp3-log",NULL);
    if(thread)SDL_DetachThread(thread);
    else SDL_AtomicSet(&logger_busy,0);
}
/* Checkpoints preserve the bit reservoir, synthesis history, pending PCM and
   gapless counters. Byte offsets alone cannot restore an MP3 decoder exactly.
   This depends on the pinned dr_mp3 layout; the regression test compares PCM.
   No compressed read-ahead is copied: restore rereads it from its file offset. */
#define MUSIC_CHECKPOINTS 128u
typedef struct {
    drmp3 state;
    drmp3_uint64 frame;
} MusicCheckpoint;
typedef struct {
    MusicCheckpoint points[MUSIC_CHECKPOINTS];
    unsigned count;
    drmp3_uint64 spacing;
} MusicSeekCache;

static void remember_position(XsshMp3 *m){
    drmp3 *mp=m->decoder;
    MusicSeekCache *cache=m->seek_points;
    if(!cache || !mp || mp->atEnd || m->io_timed_out)return;
    drmp3_uint64 frame=drmp3_get_cursor_in_pcm_frames(mp);
    if(cache->count && frame<cache->points[cache->count-1].frame+cache->spacing)return;
    /* Keep coverage of the entire played region as a long track progresses.
       Retaining every other point doubles spacing without growing memory. */
    if(cache->count==MUSIC_CHECKPOINTS){
        for(unsigned i=1;i<MUSIC_CHECKPOINTS/2;i++)
            cache->points[i]=cache->points[i*2];
        cache->count=MUSIC_CHECKPOINTS/2;
        cache->spacing*=2;
        if(frame<cache->points[cache->count-1].frame+cache->spacing)return;
    }
    MusicCheckpoint *point=&cache->points[cache->count++];
    point->frame=frame;
    point->state=*mp;
    point->state.streamCursor=mp->streamCursor-mp->dataSize;
    point->state.dataSize=point->state.dataConsumed=0;
    point->state.pData=NULL;
    point->state.dataCapacity=0;
}

static int restore_position(XsshMp3 *m,drmp3_uint64 target){
    drmp3 *mp=m->decoder;
    MusicSeekCache *cache=m->seek_points;
    if(cache){
        for(unsigned i=cache->count;i>0;i--){
            MusicCheckpoint *point=&cache->points[i-1];
            if(point->frame>target)continue;
            /* Seek before replacing state so failure leaves a valid decoder. */
            if(!drmp3__on_seek_64(mp,point->state.streamCursor,DRMP3_SEEK_SET))return 0;
            drmp3_uint8 *data=mp->pData;
            size_t capacity=mp->dataCapacity;
            *mp=point->state;
            mp->pData=data;
            mp->dataCapacity=capacity;
            return 1;
        }
    }
    return drmp3_seek_to_pcm_frame(mp,0);
}

/* Decoder and file are owned exclusively by the MP3 worker. */
static void close_decoder(XsshMp3 *m){
    worker_stage(m,"decoder cleanup");
    /* Checkpoints contain non-owning decoder snapshots. */
    if(m->decoder){drmp3_uninit((drmp3*)m->decoder);free(m->decoder);m->decoder=NULL;}
    if(m->track_file){fclose(m->track_file);m->track_file=NULL;}
    if(m->seek_points){free(m->seek_points);m->seek_points=NULL;}
}
/* Decoder and converter are reused for the duration of each streamed song. */
static void close_track(XsshMp3 *m){
    close_decoder(m);
    if(m->stream){SDL_FreeAudioStream(m->stream);m->stream=NULL;}
    m->track_draining=0;
}
static int open_next(XsshMp3 *m, int rejected){
    worker_stage(m,"track transition");
    close_track(m);
    int open_errors=0, decode_errors=0, format_errors=0, device_errors=0;
    for(int tries=0;tries<m->track_count && !should_stop(m);tries++){
        if(m->next_track>=m->track_count){
            if(rejected || tries) m->next_track=0;
            else shuffle(m);
        }
        const char *name=m->names[m->next_track++];
        SDL_LockMutex(m->mutex);snprintf(m->active_track,sizeof(m->active_track),"%s",name);SDL_UnlockMutex(m->mutex);
        char path[sizeof(MUSIC_DIR)+XSSH_MP3_NAME_SIZE+2];
        snprintf(path,sizeof(path),"%s\\%s",MUSIC_DIR,name);
        drmp3 *mp=(drmp3*)calloc(1,sizeof(*mp));
        if(!mp){worker_message(m,"MP3 decoder allocation failed");return 0;}
        worker_stage(m,"fopen MP3");
        FILE *f=fopen(path,"rb");
        if(!f){open_errors++;free(mp);continue;}
        worker_stage(m,"drmp3_init");
        m->track_file=f;
        io_budget(m);
        if(!drmp3_init(mp,mp3_read,mp3_seek,mp3_tell,NULL,m,NULL)){
            decode_errors++;fclose(f);m->track_file=NULL;free(mp);continue;
        }
        if((mp->channels!=1 && mp->channels!=2) || mp->sampleRate<8000 || mp->sampleRate>48000){
            format_errors++;drmp3_uninit(mp);free(mp);fclose(f);m->track_file=NULL;continue;
        }
        /* Convert every MP3 to the session's single 48 kHz stereo device.
           SDL_AudioStream preserves resampler state between decode chunks. */
        worker_stage(m,"SDL_NewAudioStream");
        SDL_AudioStream *stream=SDL_NewAudioStream(AUDIO_S16SYS,(Uint8)mp->channels,
              (int)mp->sampleRate,AUDIO_S16SYS,MUSIC_OUTPUT_CHANNELS,MUSIC_OUTPUT_RATE);
        if(!stream){device_errors++;drmp3_uninit(mp);free(mp);fclose(f);m->track_file=NULL;continue;}
        m->stream=stream;
        m->decoder=mp;m->track_file=f;
        MusicSeekCache *cache=malloc(sizeof(*cache));
        m->seek_points=cache;
        if(cache){cache->count=0;cache->spacing=mp->sampleRate;remember_position(m);}
        worker_stage(m,"playback loop");
        return 1;
    }
    {char error[160];
     snprintf(error,sizeof(error),"MP3 open failed: file=%d decode=%d format=%d audio=%d",
              open_errors,decode_errors,format_errors,device_errors);
     worker_message(m,error);}
    return 0;
}
static void worker_heartbeat(XsshMp3 *m){
    SDL_LockMutex(m->mutex);
    m->worker_heartbeat_ms=SDL_GetTicks();
    SDL_UnlockMutex(m->mutex);
}
static void set_loading(XsshMp3 *m, int loading){
    SDL_LockMutex(m->mutex);
    m->loading=loading;
    m->buffered_ms=0;
    if(!m->stop_requested){
        snprintf(m->message,sizeof(m->message),"%s",
                 loading ? "loading next mp3" : "MP3 playback started");
        m->status_pending=1;
    }
    SDL_UnlockMutex(m->mutex);
}
/* Keep playlist navigation serialized on the decoder worker. */
static void select_relative_track(XsshMp3 *m, int skip){
    int index=(m->next_track-1+skip)%m->track_count;
    m->next_track=index<0?index+m->track_count:index;
}
static int mp3_worker(void *arg){
    XsshMp3 *m=arg;
    if(SDL_SetThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL)<0)
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    drmp3_int16 *pcm=malloc(MUSIC_DECODE_FRAMES*2*sizeof(*pcm));
    if(!pcm){worker_message(m,"MP3 decode buffer allocation failed");goto worker_exit;}
    worker_stage(m,"nxAudio initialization");
    if(!xssh_audio_open()){
        worker_message(m,"MP3 hardware audio initialization failed");goto worker_exit;
    }

    int need_track=1, eof=0, started=0, rejected=0, pending_skip=0;
    int track_had_audio=0, loading_notice=1;
    Uint32 skip_last_ms=0;
    uint64_t position_base=0, discard_frames=0;
    uint8_t *slot=NULL;
    size_t slot_capacity=0, slot_used=0;
    unsigned decode_steps=0;
    while(!should_stop(m)){
        worker_heartbeat(m);
        int skip,seek,volume;
        SDL_LockMutex(m->mutex);
        skip=m->skip_request;m->skip_request=0;
        seek=m->seek_request;m->seek_request=0;
        volume=m->volume;
        SDL_UnlockMutex(m->mutex);
        if(skip){
            pending_skip+=skip;
            if(pending_skip>m->track_count)pending_skip=m->track_count;
            if(pending_skip<-m->track_count)pending_skip=-m->track_count;
            skip_last_ms=SDL_GetTicks();
        }
        if(pending_skip){
            if((Uint32)(SDL_GetTicks()-skip_last_ms)<MUSIC_SKIP_SETTLE_MS){
                SDL_Delay(5);continue;
            }
            worker_stage(m,"nxAudio stop for skip");
            if(!xssh_audio_stop()){worker_message(m,"MP3 hardware stop failed");break;}
            close_track(m);
            select_relative_track(m,pending_skip);
            pending_skip=0;rejected=0;need_track=1;
        }
        if(need_track){
            set_loading(m,1);
            if(!open_next(m,rejected))break;
            worker_stage(m,"nxAudio prepare streaming voice");
            if(!xssh_audio_begin(volume)){worker_message(m,"MP3 hardware start failed");break;}
            position_base=0;discard_frames=0;
            eof=0;started=0;m->track_draining=0;
            track_had_audio=0;loading_notice=1;
            slot=NULL;slot_used=0;need_track=0;
        }

        uint64_t played=0;
        uint32_t queued=0;
        worker_stage(m,"nxAudio streaming service");
        if(!xssh_audio_poll(volume,&played,&queued)){
            worker_message(m,"MP3 hardware audio stalled");break;
        }
        SDL_LockMutex(m->mutex);
        m->buffered_ms=(unsigned)((uint64_t)queued*1000u/MUSIC_OUTPUT_BPS);
        SDL_UnlockMutex(m->mutex);

        drmp3 *mp=m->decoder;
        if(seek){
            uint64_t destination=position_base+played;
            uint64_t step=(uint64_t)MUSIC_OUTPUT_BPS*5u;
            if(seek<0)destination=destination>step?destination-step:0;
            else destination+=step;
            uint64_t target=destination/MUSIC_OUTPUT_BPS*mp->sampleRate +
                (destination%MUSIC_OUTPUT_BPS)*mp->sampleRate/MUSIC_OUTPUT_BPS;
            worker_stage(m,"nxAudio stop for seek");
            if(!xssh_audio_begin(volume)){worker_message(m,"MP3 hardware seek stop failed");break;}
            SDL_AudioStreamClear(m->stream);
            uint64_t cursor=drmp3_get_cursor_in_pcm_frames(mp);
            if(target<cursor){
                worker_stage(m,"MP3 rewind decoder reset");
                io_budget(m);
                if(!restore_position(m,target)){
                    worker_message(m,"MP3 rewind reset failed");break;
                }
                cursor=drmp3_get_cursor_in_pcm_frames(mp);
            }
            discard_frames=target-cursor;
            position_base=target/mp->sampleRate*MUSIC_OUTPUT_BPS +
                (target%mp->sampleRate)*MUSIC_OUTPUT_BPS/mp->sampleRate;
            slot=NULL;slot_used=0;eof=0;started=0;m->track_draining=0;
        }
        if(discard_frames){
            worker_stage(m,"MP3 incremental seek");
            io_budget(m);
            drmp3_uint64 count=discard_frames>MUSIC_DECODE_FRAMES ?
                MUSIC_DECODE_FRAMES:discard_frames;
            drmp3_uint64 frames=drmp3_read_pcm_frames_s16(mp,count,pcm);
            if(frames>count || m->io_timed_out || ferror(m->track_file)){
                worker_message(m,"MP3 seek read failed");break;
            }
            if(!frames){discard_frames=0;eof=1;}
            else {discard_frames-=frames;track_had_audio=1;remember_position(m);}
            if((++decode_steps & 3u)==0)SDL_Delay(1);
            continue;
        }
        if(eof){
            if(queued){SDL_Delay(5);continue;}
            if(!track_had_audio && ++rejected>=m->track_count){
                worker_message(m,"MP3 playlist has no decodable audio");break;
            }
            if(track_had_audio)rejected=0;
            need_track=1;
            continue;
        }

        if(!slot){
            int available=xssh_audio_acquire(&slot,&slot_capacity);
            if(available<0){worker_message(m,"MP3 hardware buffer unavailable");break;}
            if(!available){SDL_Delay(5);continue;}
            slot_used=0;
        }
        size_t target_bytes=started?slot_capacity:MUSIC_START_BYTES;
        if(target_bytes>slot_capacity)target_bytes=slot_capacity;
        int available=SDL_AudioStreamAvailable(m->stream);
        if(available<0){worker_message(m,"MP3 conversion failed");break;}
        if(available){
            size_t take=(size_t)available;
            if(take>target_bytes-slot_used)take=target_bytes-slot_used;
            take&=~(size_t)3;
            if(!take){worker_message(m,"MP3 unaligned converted audio");break;}
            int got=SDL_AudioStreamGet(m->stream,slot+slot_used,(int)take);
            if(got!=(int)take){worker_message(m,"MP3 conversion read failed");break;}
            slot_used+=take;
        }else if(m->track_draining){
            eof=1;
        }else{
            worker_stage(m,"MP3 HDD streaming decode");
            io_budget(m);
            drmp3_uint64 frames=drmp3_read_pcm_frames_s16(mp,MUSIC_DECODE_FRAMES,pcm);
            if(frames>MUSIC_DECODE_FRAMES || m->io_timed_out || ferror(m->track_file)){
                worker_message(m,"MP3 HDD read or decode failed");break;
            }
            if(!frames){
                if(SDL_AudioStreamFlush(m->stream)!=0){
                    worker_message(m,"MP3 conversion flush failed");break;
                }
                m->track_draining=1;
            }else{
                track_had_audio=1;
                remember_position(m);
                if(SDL_AudioStreamPut(m->stream,pcm,(int)(frames*mp->channels*sizeof(*pcm)))!=0){
                    worker_message(m,"MP3 conversion write failed");break;
                }
            }
            if((++decode_steps & 3u)==0)SDL_Delay(1);
            continue;
        }
        if(slot_used && (slot_used==target_bytes || eof)){
            worker_stage(m,"nxAudio submit decoded buffer");
            if(!xssh_audio_commit(slot_used)){worker_message(m,"MP3 hardware queue failed");break;}
            slot=NULL;slot_used=0;
            SDL_LockMutex(m->mutex);m->last_audio_success_ms=SDL_GetTicks();SDL_UnlockMutex(m->mutex);
            if(!started){
                started=1;
                if(loading_notice){set_loading(m,0);loading_notice=0;}
            }
        }
    }
worker_exit:
    worker_stage(m,"nxAudio shutdown");
    if(!xssh_audio_close())
        worker_message(m,"MP3 hardware fault; audio disabled until restart");
    close_track(m);
    free(pcm);
    SDL_LockMutex(m->mutex);
    m->loading=0;m->worker_finished=1;
    snprintf(m->worker_stage,sizeof(m->worker_stage),"exited");
    m->playing=0;m->buffered_ms=0;
    SDL_UnlockMutex(m->mutex);
    return 0;
}
void xssh_mp3_init(XsshMp3 *m){
    memset(m,0,sizeof(*m));m->volume=70;
    m->rng=(uint32_t)SDL_GetPerformanceCounter() ^ SDL_GetTicks() ^ 0xa521c81du;
}
/* Exit checks completion without waiting for a worker or disk operation. */
int xssh_mp3_stop(XsshMp3 *m){
    m->recovery_pending=0;
    if(m->mutex){
        if(SDL_TryLockMutex(m->mutex)!=0)return 0;
        m->stop_requested=1;
        int finished=m->worker_finished;
        SDL_UnlockMutex(m->mutex);
        if(m->thread && !finished)return 0;
        SDL_DestroyMutex(m->mutex);m->mutex=NULL;
    }
    m->thread=NULL;
    return SDL_AtomicGet(&logger_busy)==0;
}
/* Start/restart only after the previous worker publishes completion. On
   recovery retain the playlist and position instead of rescanning/shuffling. */
static int start_worker(XsshMp3 *m,int fresh){
    if(xssh_audio_faulted()){
        snprintf(m->message,sizeof(m->message),"MP3 hardware fault; restart required");
        return 0;
    }
    if(fresh){
        if(!collect_tracks(m)){
            snprintf(m->message,sizeof(m->message),"No MP3 files in D:\\mp3s");return 0;
        }
        shuffle(m);
        m->recovery_attempts=0;
    }
    m->mutex=SDL_CreateMutex();
    if(!m->mutex){snprintf(m->message,sizeof(m->message),"MP3 mutex unavailable");return 0;}
    m->stop_requested=0;m->worker_finished=0;m->buffered_ms=0;m->playing=1;
    m->worker_stall_reported=0;
    m->worker_heartbeat_ms=SDL_GetTicks();
    snprintf(m->worker_stage,sizeof(m->worker_stage),"starting");
    m->worker_stage_since_ms=m->worker_heartbeat_ms;
    m->active_track[0]=0;
    m->last_audio_success_ms=0;
    m->seek_request=0;m->skip_request=0;
    /* Publish startup status before the worker can publish its loading status. */
    snprintf(m->message,sizeof(m->message),"loading next mp3");
    m->status_pending=1;
    m->loading=1;
    m->thread=SDL_CreateThread(mp3_worker,"xssh-mp3",m);
    if(!m->thread){
        m->playing=0;SDL_DestroyMutex(m->mutex);m->mutex=NULL;
        snprintf(m->message,sizeof(m->message),"MP3 worker unavailable: %.100s",SDL_GetError());
        return 0;
    }
    SDL_DetachThread(m->thread);
    return 1;
}
void xssh_mp3_toggle(XsshMp3 *m){
    if(m->thread){
        /* A second toggle during shutdown must not start another worker. */
        if(SDL_TryLockMutex(m->mutex)!=0)return;
        m->stop_requested=1;
        m->playing=0;
        m->recovery_pending=0;
        m->stop_started_ms=SDL_GetTicks();
        snprintf(m->message,sizeof(m->message),"MP3 stop requested");
        m->status_pending=1;
        SDL_UnlockMutex(m->mutex);
        return;
    }
    m->recovery_pending=0;
    start_worker(m,1);
}
void xssh_mp3_skip(XsshMp3 *m,int direction){
    if(!m->mutex)return;
    if(SDL_TryLockMutex(m->mutex)!=0)return;
    if(m->playing){
        /* Accumulate multiple presses; worker applies them in one transition. */
        m->skip_request+=direction<0?-1:1;
        if(m->skip_request>m->track_count)m->skip_request=m->track_count;
        if(m->skip_request<-m->track_count)m->skip_request=-m->track_count;
        m->seek_request=0;
    }
    SDL_UnlockMutex(m->mutex);
}
void xssh_mp3_seek(XsshMp3 *m,int direction){
    if(!m->mutex)return;
    if(SDL_TryLockMutex(m->mutex)!=0)return;
    if(m->playing && direction)m->seek_request=direction<0?-1:1;
    SDL_UnlockMutex(m->mutex);
}
void xssh_mp3_volume(XsshMp3 *m,int direction){
    if(m->mutex && SDL_TryLockMutex(m->mutex)!=0)return;
    m->volume+=direction*5;
    if(m->volume<0)m->volume=0;
    if(m->volume>100)m->volume=100;
    snprintf(m->message,sizeof(m->message),"MP3 volume: %d%%",m->volume);
    if(m->mutex)SDL_UnlockMutex(m->mutex);
}
int xssh_mp3_is_playing(XsshMp3 *m){
    int playing;
    if(m->mutex && SDL_TryLockMutex(m->mutex)!=0)return 0;
    playing=m->playing;
    if(m->mutex)SDL_UnlockMutex(m->mutex);
    return playing;
}
int xssh_mp3_audio_urgent(XsshMp3 *m){
    int urgent=0;
    if(m->mutex){
        if(SDL_TryLockMutex(m->mutex)!=0)return 0;
        urgent=m->playing && !m->loading && m->buffered_ms<MUSIC_URGENT_MS;
        SDL_UnlockMutex(m->mutex);
    }
    return urgent;
}
void xssh_mp3_pump(XsshMp3 *m){
    /* Release completed worker state without waiting for driver calls. */
    int finished=0,stopping=0;
    Uint32 now=SDL_GetTicks();
    if(m->mutex){
        if(SDL_TryLockMutex(m->mutex)!=0)return;
        finished=m->worker_finished;
        stopping=m->stop_requested;
        if(!finished && !stopping && m->playing &&
           (Uint32)(now-m->worker_heartbeat_ms)>
             (strstr(m->worker_stage,"track decode") ? MUSIC_DECODE_STALL_MS : MUSIC_WORKER_STALL_MS) &&
           !m->worker_stall_reported){
            m->worker_stall_reported=1;
            snprintf(m->message,sizeof(m->message),"MP3 stalled: %.110s",m->worker_stage);
            snprintf(m->log_event,sizeof(m->log_event),"worker stalled");m->log_pending=1;
            m->stop_requested=1;
            m->stop_started_ms=now;
            m->playing=0;
            m->status_pending=1;
        }
        if(!finished && m->stop_requested && m->stop_started_ms &&
           (Uint32)(now-m->stop_started_ms)>MUSIC_WORKER_STALL_MS &&
           !m->stop_timeout_reported){
            m->stop_timeout_reported=1;
            snprintf(m->message,sizeof(m->message),"MP3 stop timeout: %.105s",m->worker_stage);
            snprintf(m->log_event,sizeof(m->log_event),"stop timeout");m->log_pending=1;
            m->status_pending=1;
        }
        SDL_UnlockMutex(m->mutex);
    }
    /* Snapshot diagnostics under the mutex, then perform file I/O outside it.
       This prevents the watchdog from blocking the audio worker on disk I/O. */
    char event[192]={0},stage[64]={0},track[128]={0};
    if(m->mutex){
        if(SDL_TryLockMutex(m->mutex)!=0)return;
        if(m->log_pending){
            snprintf(event,sizeof(event),"%s",m->log_event);
            snprintf(stage,sizeof(stage),"%s",m->worker_stage);
            snprintf(track,sizeof(track),"%s",m->active_track);
            m->log_pending=0;
        }
        SDL_UnlockMutex(m->mutex);
    }
    if(event[0])log_event_main(event,stage,track);
    if(finished && m->thread){
        m->thread=NULL;
        SDL_DestroyMutex(m->mutex);m->mutex=NULL;
        m->playing=0;m->worker_finished=0;
        m->stop_started_ms=0;m->stop_timeout_reported=0;
        if(stopping){
            m->recovery_pending=0;
            snprintf(m->message,sizeof(m->message),"MP3 playback stopped");
            m->status_pending=1;
        }else{
            snprintf(m->last_error,sizeof(m->last_error),"%s",m->message);
            log_event_main(m->last_error,m->worker_stage,m->active_track);
            /* A clean EOF should advance within the worker. Exiting here
               indicates a failed decoder/device transition or I/O error. */
            if(!xssh_audio_faulted() && m->recovery_attempts<MUSIC_RECOVERY_MAX_ATTEMPTS){
                m->recovery_attempts++;
                m->recovery_pending=1;
                m->recovery_due_ms=now+MUSIC_RECOVERY_DELAY_MS*m->recovery_attempts;
                snprintf(m->message,sizeof(m->message),"MP3 recovering (%d/%d)",
                         m->recovery_attempts,MUSIC_RECOVERY_MAX_ATTEMPTS);
            }else{
                m->recovery_pending=0;
                snprintf(m->message,sizeof(m->message),"MP3 recovery exhausted: %.125s",m->last_error);
            }
            m->status_pending=1;
        }
    }
    if(m->recovery_pending && !m->thread && (int32_t)(now-m->recovery_due_ms)>=0){
        m->recovery_pending=0;
        if(!start_worker(m,0)){
            if(!xssh_audio_faulted() && m->recovery_attempts<MUSIC_RECOVERY_MAX_ATTEMPTS){
                m->recovery_attempts++;
                m->recovery_pending=1;
                m->recovery_due_ms=now+MUSIC_RECOVERY_DELAY_MS*m->recovery_attempts;
            }else snprintf(m->message,sizeof(m->message),"MP3 recovery failed: %.125s",m->last_error);
            m->status_pending=1;
        }
    }
}
int xssh_mp3_take_status(XsshMp3 *m,char *out,size_t capacity){
    int pending=0;
    if(m->mutex && SDL_TryLockMutex(m->mutex)!=0)return 0;
    pending=m->status_pending;
    if(pending){
        m->status_pending=0;
        if(out && capacity)snprintf(out,capacity,"%s",m->message);
    }
    if(m->mutex)SDL_UnlockMutex(m->mutex);
    return pending;
}

void xssh_mp3_get_message(XsshMp3 *m,char *out,size_t capacity){
    if(!out || !capacity)return;
    out[0]=0;
    if(m->mutex && SDL_TryLockMutex(m->mutex)!=0)return;
    snprintf(out,capacity,"%s",m->message);
    if(m->mutex)SDL_UnlockMutex(m->mutex);
}
