// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Ryzee119
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <xboxkrnl/xboxkrnl.h>

typedef enum
{
    NX_AUDIO_SUCCESS = 0,
    NX_AUDIO_ERR_OUT_OF_MEMORY,
    NX_AUDIO_ERR_OUT_OF_VOICES,
    NX_AUDIO_ERR_INVALID_PARAM,
    NX_AUDIO_ERR_INVALID_STATE,
    NX_AUDIO_ERR_UNSUPPORTED,
    NX_AUDIO_ERR_QUEUE_FULL,
    NX_AUDIO_ERR_TIMEOUT,
    NX_AUDIO_ERR_BUFFER_IN_USE,
} nxAudioResult;

typedef enum
{
    NX_PLAYING,
    NX_PAUSED,
    NX_STOPPING,
    NX_STOPPED
} nxAudioVoiceState;

typedef enum
{
    NX_VOICE_TYPE_2D_STATIC,
    NX_VOICE_TYPE_2D_STREAM,
    NX_VOICE_TYPE_3D_STATIC,
    NX_VOICE_TYPE_3D_STREAM
} nxAudioVoiceType;

typedef enum
{
    NX_AUDIO_CODEC_PCM,
    NX_AUDIO_CODEC_ADPCM
} nxAudioCodec;

#define NX_AUDIO_HRTF_NONE 0xFFFF

typedef struct
{
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bytes_per_sample;
    nxAudioCodec codec;
    nxAudioVoiceType type;
} nxAudioFormat;

// Some of this is talked about in https://xboxdevwiki.net/APU
typedef enum
{
    NX_FILTER_MODE_BYPASS = 0x00,

    // These apply for mono voices
    NX_FILTER_MODE_MONO_DLS2_LOW_PASS = 0x01,
    NX_FILTER_MODE_MONO_PARAMETRIC_EQ = 0x02,
    NX_FILTER_MODE_MONO_DLS2_LOW_PASS_PARAMETRIC_EQ = 0x03,

    // These apply for stereo voices
    NX_FILTER_MODE_STEREO_DLS2_LOW_PASS = 0x01,
    NX_FILTER_MODE_STEREO_PARAMETRIC_EQ = 0x02,

    // These apply for 3d voices
    NX_FILTER_MODE_3D_DLS2_LOW_PASS_I3DL2_REVERB = 0x01,
    NX_FILTER_MODE_3D_PARAMETRIC_EQ_I3DL2_REVERB = 0x02,
    NX_FILTER_MODE_3D_I3DL2_REVERB = 0x03
} nxAudioFilterMode;

typedef struct
{
    uint32_t unused; // FIXME. I guess 3d enable? max voices, max sges, etc.
} nxAudioInitParams;

typedef struct
{
    int8_t hrir_left[31];
    int8_t hrir_right[31];
    int16_t itd;
} nxAudioHRTFParams;

typedef struct nxAudioBuffer
{
    const void *buffer;
    uint32_t size_bytes;
} nxAudioBuffer;

/**
 * @brief ADSR Envelope parameters.
 * Time values are specified in units of 512 samples.
 * 1 unit (512 samples) is approximately 10.66 milliseconds.
 *
 *                            HOLD
 *                   +---------------------+
 *                  /|                     |\
 *          ATTACK / |                     | \ DECAY
 *                /  |                     |  \    SUSTAIN LEVEL
 *               /   |                     |   +-------------------+
 *              /    |                     |   |                   |\
 *             /     |                     |   |                   | \
 *            /      |                     |   |                   |  \  RELEASE
 *           /       |                     |   |                   |   \
 *          /        |                     |   |                   |    \
 * 0 ------+---------+---------------------+---+-------------------+-----+----> Time
 *   DELAY
 */
typedef struct
{
    uint32_t delay_time;    // Delay before the attack phase, in 512-sample units (0-8191)
    uint32_t attack_time;   // Duration of the attack phase, in 512-sample units (0-8191)
    uint32_t hold_time;     // Duration of the hold phase, in 512-sample units (0-8191)
    uint32_t decay_time;    // Duration of the decay phase, in 512-sample units (0-8191)
    uint32_t sustain_level; // Sustain level (0 to 255). 255 = 100% level
    uint32_t release_time;  // Duration of the release phase, in 512-sample units (0-8191)
} nxAudioADSR;

struct nxAudioVoice;
typedef void (*nxAudioVoiceCallback)(struct nxAudioVoice *voice, const nxAudioBuffer *completed, void *user_context);

typedef struct nxAudioVoice
{
    _Atomic nxAudioVoiceState state;
    nxAudioFormat format;

    const nxAudioBuffer *buffers_hardware[2];

    nxAudioVoiceCallback callback;
    void *user_context;

    float master_gain;
    float gain_front_left;
    float gain_front_right;
    float gain_center;
    float gain_lfe;
    float gain_rear_left;
    float gain_rear_right;
    float hrtf_front_gain;
    float hrtf_rear_gain;
    float pitch;
    bool looping;
    bool streaming;
    bool amplitude_adsr_enabled;
    bool filter_adsr_enabled;

    nxAudioADSR amplitude_adsr;
    nxAudioADSR filter_adsr;

    uint32_t sge_base;
    uint32_t sge_count;

    // Maintain a local copy of some hardware variables
    uint8_t voice_index;
    uint32_t cfg_vbin;
    uint32_t cfg_fmt;
    uint32_t cfg_env0;
    uint32_t cfg_enva;
    uint32_t cfg_env1;
    uint32_t cfg_envf;
    uint32_t cfg_misc;
    uint32_t hrtf_target;
    uint32_t tar_vola;
    uint32_t tar_volb;
    uint32_t tar_volc;
    uint32_t tar_lfo_env;
    uint32_t tar_fca;
    uint32_t tar_fcb;
    uint32_t tar_pitch_link;
    uint32_t voice_on_flags;
} nxAudioVoice;

/**
 * @brief Initializes the audio subsystem.
 * @param parameters Pointer to the configuration parameters for initialization.
 * @return true if the audio subsystem was successfully initialized, false otherwise.
 */
bool nxAudioInit (const nxAudioInitParams *parameters);

/**
 * @brief Shuts down the audio subsystem and releases associated resources.
 */
void nxAudioShutdown (void);

/**
 * @brief Initializes an audio buffer wrapping a user-provided memory region.
 * @param buffer Pointer to the audio buffer structure to initialize.
 * @param user_buffer Pointer to the user-allocated memory containing the raw audio data.
 * @param size_bytes The size of the user buffer in bytes.
 * @return true if the buffer was successfully initialized, false otherwise.
 */
bool nxAudioBufferInitialize (nxAudioBuffer *buffer, const void *user_buffer, uint32_t size_bytes);



/**
 * @brief Queues an audio buffer to a streaming voice's playback queue. You can queue up to two buffers. When the
 * buffer is complete the callback set by nxAudioBufferSetCallback() will be called, and the other queued buffer
 * will start playing. This is for streaming sounds.
 * @param voice Pointer to the audio voice.
 * @param buffer Pointer to the initialized audio buffer to enqueue.
 * @return true if the buffer was successfully queued, false otherwise.
 */
bool nxAudioBufferQueue (nxAudioVoice *voice, const nxAudioBuffer *buffer);

/**
 * @brief Sets the callback function that will be triggered when a streaming voice requires more data.
 *
 * @note The callback is invoked at DISPATCH_LEVEL from the APU's DPC. It is safe to call
 * nxAudioBufferQueue() from within the callback to double-buffer audio. Do NOT call
 * nxAudioVoiceStop() or nxAudioVoiceDestroy() from within the callback.
 *
 * @param voice Pointer to the audio voice.
 * @param callback The function to call when the voice requires more data.
 * @param user_context An optional context pointer to pass to the callback.
 */
void nxAudioBufferSetCallback (nxAudioVoice *voice, nxAudioVoiceCallback callback, void *user_context);

/**
 * @brief Creates a new audio voice for playback.
 * @param voice Pointer to the voice structure to be created.
 * @param format Pointer to the format specification for this voice.
 * @return true if the voice was successfully created, false otherwise.
 */
bool nxAudioVoiceCreate (nxAudioVoice *voice, const nxAudioFormat *format);

/**
 * @brief Destroys an audio voice and cleans up its allocated resources.
 * @param voice Pointer to the audio voice to destroy.
 */
void nxAudioVoiceDestroy (nxAudioVoice *voice);

/**
 * @brief Starts or resumes playback of submitted buffers on the specified voice.
 * @param voice Pointer to the audio voice to start.
 * @return true if playback was successfully started, false otherwise.
 */
bool nxAudioVoiceStart (nxAudioVoice *voice);



/**
 * @brief Stop voice will issue a command to hardware to stop it as soon as possible. Note, the voice is still active
 * and its status can be checked with nxAudioVoiceGetState().
 * @param voice Pointer to the audio voice to stop.
 * @return true if the command was successfully sent.
 */
bool nxAudioVoiceStop (nxAudioVoice *voice);

/**
 * @brief Returns the current state of the audio voice.
 * @param voice Pointer to the audio voice.
 * @return The current nxAudioVoiceState. Returns NX_STOPPED if voice is NULL.
 */
nxAudioVoiceState nxAudioVoiceGetState (const nxAudioVoice *voice);



/**
 * @brief Sets the output volume level for a specific voice.
 * @param voice Pointer to the audio voice to adjust.
 * @param gain Floating-point gain level clamped between 0.0f (muted) and 1.0f (maximum). You can overdrive gain
 * up to 2.0f but this will can cause clipping with many voices.
 * @return true if the volume was successfully updated, false otherwise.
 */
bool nxAudioVoiceSetMasterGain (nxAudioVoice *voice, float gain);

/**
 * @brief Sets the 6-channel output gains for a voice.
 *
 * These gains are combined with the master gain set by nxAudioVoiceSetMasterGain().
 * This applies to standard 2D voices.
 *
 * @param voice Pointer to the audio voice.
 * @param front_left Front left gain (0.0 = silent, 1.0 = full volume).
 * @param front_right Front right gain.
 * @param center Center gain.
 * @param lfe LFE gain.
 * @param rear_left Rear left gain.
 * @param rear_right Rear right gain.
 * @return true if successful, false otherwise.
 */
bool nxAudioVoiceSetChannelGain (nxAudioVoice *voice, float front_left, float front_right, float center, float lfe,
                                 float rear_left, float rear_right);



/**
 * @brief Sets the pitch (playback speed) of the voice.
 * @param voice Pointer to the audio voice to adjust.
 * @param pitch Pitch multiplier (1.0f = original pitch, 2.0f = double pitch, etc.).
 * @return true if the pitch was successfully updated, false otherwise.
 */
bool nxAudioVoiceSetPitch (nxAudioVoice *voice, float pitch);

/**
 * @brief Sets the audio format of the voice.
 * @param voice Pointer to the audio voice to adjust.
 * @param format Pointer to the new format specification for this voice.
 * @return true if the format was successfully updated, false otherwise.
 */
bool nxAudioVoiceSetFormat (nxAudioVoice *voice, const nxAudioFormat *format);















/**
 * @brief Retrieves the last error code generated by the audio subsystem.
 * @return The last nxAudioResult error code.
 */
nxAudioResult nxAudioGetLastError (void);





/* XSSH fork: latched fault, no automatic restart after uncertain DMA ownership. */
bool nxAudioFaulted(void);
void nxAudioAbort(void);
