#ifndef XSSH_AUDIO_H
#define XSSH_AUDIO_H
#include <stddef.h>
#include <stdint.h>

/* Worker-owned producer API. Acquire returns 1 for a writable slot, 0 when
   both are in hardware, -1 on error. Commit transfers ownership to hardware.
   Poll reclaims completed slots. Stop cancels any uncommitted acquisition. */
int xssh_audio_open(void);
int xssh_audio_begin(int volume);
int xssh_audio_acquire(uint8_t **data, size_t *capacity);
int xssh_audio_commit(size_t bytes);
int xssh_audio_poll(int volume, uint64_t *played, uint32_t *queued);
int xssh_audio_stop(void);
int xssh_audio_close(void);
int xssh_audio_faulted(void);
#endif
