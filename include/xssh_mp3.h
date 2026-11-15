#ifndef XSSH_MP3_H
#define XSSH_MP3_H
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
/* D: is the running application's directory on the original Xbox. */
#define XSSH_MP3_DIR "D:\\mp3s"
#define XSSH_MP3_MAX_TRACKS 512
#define XSSH_MP3_NAME_SIZE 128

typedef struct {
    char names[XSSH_MP3_MAX_TRACKS][XSSH_MP3_NAME_SIZE];
    int track_count, next_track, playing, volume;
    uint32_t rng;
    SDL_Thread *thread;
    SDL_mutex *mutex;
    int stop_requested, worker_finished, device_started, track_draining;
    int restart_requested;
    int loading; /* protected by mutex; initial streaming fill */
    int recovery_pending, recovery_attempts, status_pending;
    int stop_timeout_reported;
    Uint32 recovery_due_ms, stop_started_ms, last_audio_success_ms;
    Uint32 worker_heartbeat_ms;
    int worker_stall_reported;
    int skip_request, seek_request;
    uint64_t track_total_frames;
    uint64_t frame_cursor;
    unsigned buffered_ms;
    int audio_rate, audio_channels; /* current SDL device format */
    SDL_AudioStream *stream; /* per-track rate/channel converter */
    void *decoder; /* drmp3, private to the player implementation */
    void *seek_points; /* bounded decoder checkpoint cache owned by the worker */
    FILE *track_file; /* kept open only while streaming */
    Uint32 io_deadline;
    unsigned io_calls;
    int io_timed_out;
    char message[160];
    char last_error[160];
    char worker_stage[64];
    char active_track[128];
    Uint32 worker_stage_since_ms;
    int log_pending;
    char log_event[192];
} XsshMp3;
void xssh_mp3_init(XsshMp3 *m);
void xssh_mp3_toggle(XsshMp3 *m);
void xssh_mp3_volume(XsshMp3 *m, int direction);
void xssh_mp3_skip(XsshMp3 *m, int direction);
void xssh_mp3_seek(XsshMp3 *m, int direction);
void xssh_mp3_pump(XsshMp3 *m);
int xssh_mp3_stop(XsshMp3 *m);
int xssh_mp3_is_playing(XsshMp3 *m);
int xssh_mp3_audio_urgent(XsshMp3 *m);
int xssh_mp3_take_status(XsshMp3 *m, char *out, size_t capacity);
void xssh_mp3_get_message(XsshMp3 *m, char *out, size_t capacity);
#endif
