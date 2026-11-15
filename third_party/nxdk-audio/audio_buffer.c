// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Ryzee119
#include "audio_internal.h"
#include <nxaudio.h>



uint32_t apu_format_bytes_to_blocks (const nxAudioFormat *format, uint32_t bytes)
{
    if (format->codec == NX_AUDIO_CODEC_ADPCM) {
        return bytes / (36 * format->channels);
    }
    const uint32_t bytes_per_sample = (format->bytes_per_sample == 3) ? 4 : format->bytes_per_sample;
    return bytes / (bytes_per_sample * format->channels);
}

uint32_t apu_format_blocks_to_bytes (const nxAudioFormat *format, uint32_t blocks)
{
    if (format->codec == NX_AUDIO_CODEC_ADPCM) {
        return blocks * 36 * format->channels;
    }
    const uint32_t bytes_per_sample = (format->bytes_per_sample == 3) ? 4 : format->bytes_per_sample;
    return blocks * bytes_per_sample * format->channels;
}

static void get_memory_properties (const void *virtual_pointer, uint32_t max_length, uint32_t *out_physical_address,
                                   uint32_t *out_contiguous_length)
{
    const uint8_t *virtual_address = (const uint8_t *)virtual_pointer;
    const uint32_t physical_address = (uint32_t)MmGetPhysicalAddress((PVOID)virtual_address);
    uint32_t contiguous_bytes;

    // If the pointer is in the direct-mapped region, it is guaranteed
    // to be physically contiguous.
    if ((uintptr_t)virtual_address >= 0x80000000 && (uintptr_t)virtual_address < 0x90000000) {
        contiguous_bytes = max_length;
    } else {
        // Standard Page-Table Probing (For paged memory like malloc)
        contiguous_bytes = PAGE_SIZE - (physical_address & (PAGE_SIZE - 1));
        if (contiguous_bytes > max_length) {
            contiguous_bytes = max_length;
        }

        const uint8_t *cursor = virtual_address + contiguous_bytes;
        uint32_t remaining = max_length - contiguous_bytes;
        uint32_t current_address = physical_address + contiguous_bytes;

        while (remaining > 0) {
            const uint32_t next_address = (uint32_t)MmGetPhysicalAddress((PVOID)cursor);
            if (next_address != current_address) {
                break;
            }

            uint32_t next_contiguous = PAGE_SIZE - (next_address & (PAGE_SIZE - 1));
            if (next_contiguous > remaining) {
                next_contiguous = remaining;
            }

            contiguous_bytes += next_contiguous;
            cursor += next_contiguous;
            remaining -= next_contiguous;
            current_address += next_contiguous;
        }
    }
    *out_physical_address = physical_address;
    *out_contiguous_length = contiguous_bytes;
}

static bool insert_buffer_to_ssl (nxAudioVoice *voice, const nxAudioBuffer *buffer, uint32_t buffer_index)
{
    const uint32_t container_index = apu_format_get_container_size(&voice->format);
    const uint32_t ssl_base_index =
        (voice->voice_index * MCPX_HW_MAX_PRD_ENTRIES_PER_VOICE) + (buffer_index * MCPX_HW_MAX_PRD_ENTRIES_PER_SSL);

    const uint32_t bytes_per_block = apu_format_blocks_to_bytes(&voice->format, 1);
    const uint32_t max_bytes_per_ssl = apu_format_blocks_to_bytes(&voice->format, 65535);

    // Pre-compute all SSL entries before touching hardware
    mcpx_ssl_t hw_ssls[MCPX_HW_MAX_PRD_ENTRIES_PER_SSL];
    uint32_t ssl_segment_bases[MCPX_HW_MAX_PRD_ENTRIES_PER_SSL];
    uint32_t ssl_indices_in_segment[MCPX_HW_MAX_PRD_ENTRIES_PER_SSL];

    const uint8_t *virtual_address = (const uint8_t *)buffer->buffer;
    uint32_t bytes_left = buffer->size_bytes;
    uint32_t index = 0;

    while (bytes_left > 0) {
        if (index >= MCPX_HW_MAX_PRD_ENTRIES_PER_SSL) {
            apu_set_last_error(NX_AUDIO_ERR_OUT_OF_MEMORY);
            return false;
        }

        // Each SSL entry can be up to 65535 samples and it must be divided into block of contigous memory (if
        // the user did not provide a contiguous buffer). We can have 16 SSL entries in the SSL list. If your
        // memory is contiguous this is 16*65535 samples. If your memory is fragmented, it will be less than
        // that.
        uint32_t physical_address;
        uint32_t contiguous_bytes;
        get_memory_properties(virtual_address, bytes_left, &physical_address, &contiguous_bytes);

        if (contiguous_bytes > max_bytes_per_ssl) {
            contiguous_bytes = max_bytes_per_ssl;
        }

        contiguous_bytes -= (contiguous_bytes % bytes_per_block);
        if (contiguous_bytes == 0) {
            break;
        }

        virtual_address += contiguous_bytes;
        bytes_left -= contiguous_bytes;

        uint32_t block_samples = 0;
        if (voice->format.codec == NX_AUDIO_CODEC_ADPCM) {
            block_samples = (contiguous_bytes / bytes_per_block) * 64;
        } else {
            block_samples = contiguous_bytes / bytes_per_block;
        }
        assert(block_samples <= 65535);

        hw_ssls[index].phys_addr = physical_address;
        hw_ssls[index].length_and_format = 0;
        hw_ssls[index].length_and_format |= (block_samples & 0xFFFF);
        hw_ssls[index].length_and_format |= (container_index << 16U);
        hw_ssls[index].length_and_format |= ((uint32_t)(voice->format.channels - 1) << 18);
        hw_ssls[index].length_and_format |= ((voice->format.channels > 1 ? 1U : 0U) << 23);

        // The hardware needs to know what 64 SLL segment our target SSL is in then the offset within that
        // segment. i.e SSL at index 70 would have a segment base of 1, and an offset of 6 (70 / 64 = 1, 70 % 64
        // = 6) Within that base we push the physical address of the audio buffer.
        const uint32_t absolute_ssl_index = ssl_base_index + index;
        ssl_segment_bases[index] = absolute_ssl_index / 64;
        ssl_indices_in_segment[index] = absolute_ssl_index % 64;

        index++;
    }

    const uint32_t notifier_idx =
        MCPX_HW_NOTIFIER_BASE_OFFSET + (voice->voice_index * MCPX_HW_NOTIFIER_COUNT) + buffer_index;

    const uint32_t ssl_cmd_arg = APU_MAKE_VALUE(NV1BA0_PIO_SET_VOICE_SSL_A_BASE, ssl_base_index) |
                                 APU_MAKE_VALUE(NV1BA0_PIO_SET_VOICE_SSL_A_COUNT, index);

    for (uint32_t i = 0; i < index; i++) {
        apu_wait_for_pio(3);
        apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_CURRENT_SSL,
                      APU_MAKE_VALUE(NV1BA0_PIO_SET_CURRENT_SSL_BASE_PAGE, ssl_segment_bases[i]));

        apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_SSL_SEGMENT_OFFSET + (ssl_indices_in_segment[i] * 8),
                      hw_ssls[i].phys_addr);

        apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_SSL_SEGMENT_LENGTH + (ssl_indices_in_segment[i] * 8),
                      hw_ssls[i].length_and_format);
    }

    apu_wait_for_pio(2);
    g_hw_notifiers[notifier_idx].status = NV1BA0_NOTIFICATION_STATUS_IN_PROGRESS;
    apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_CURRENT_VOICE, voice->voice_index);
    if (buffer_index == 0) {
        apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_VOICE_SSL_A, ssl_cmd_arg);
    } else {
        apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_VOICE_SSL_B, ssl_cmd_arg);
    }
    return true;
}

bool nxAudioBufferInitialize (nxAudioBuffer *buffer, const void *user_buffer, uint32_t size_bytes)
{
    if (!buffer || !user_buffer || size_bytes == 0) {
        apu_set_last_error(NX_AUDIO_ERR_INVALID_PARAM);
        return false;
    }

    buffer->buffer = user_buffer;
    buffer->size_bytes = size_bytes;
    return true;
}

void nxAudioBufferSetCallback (nxAudioVoice *voice, nxAudioVoiceCallback callback, void *user_context)
{
    if (voice) {
        const KIRQL irql = KeRaiseIrqlToDpcLevel();
        voice->callback = callback;
        voice->user_context = user_context;
        KfLowerIrql(irql);
    }
}

bool nxAudioBufferQueue (nxAudioVoice *voice, const nxAudioBuffer *buffer)
{
    if (!voice || !buffer || !buffer->buffer || !buffer->size_bytes || (buffer->size_bytes & 3) || g_audio_fault) {
        apu_set_last_error(NX_AUDIO_ERR_INVALID_PARAM);
        return false;
    }

    if (!voice->streaming && (voice->state == NX_PLAYING || voice->state == NX_PAUSED)) {
        apu_set_last_error(NX_AUDIO_ERR_INVALID_STATE);
        return false;
    }

    const KIRQL irql = KeRaiseIrqlToDpcLevel();

    voice->streaming = true;
    voice->cfg_fmt |= APU_MAKE_VALUE(NV_PAVS_VOICE_CFG_FMT_DATA_TYPE, 1);

    apu_wait_for_pio(4);
    apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_CURRENT_VOICE, voice->voice_index);
    apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_VOICE_LOCK, 1);
    apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_VOICE_CFG_FMT, voice->cfg_fmt);
    apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_VOICE_LOCK, 0);

    uint8_t buffer_index = 0xFF;
    for (uint8_t j = 0; j < 2; j++) {
        if (voice->buffers_hardware[j] == NULL) {
            buffer_index = j;
            break;
        }
    }

    if (buffer_index < 2) {
        MmLockUnlockBufferPages((PVOID)buffer->buffer, buffer->size_bytes, FALSE);
        if (!insert_buffer_to_ssl(voice, buffer, buffer_index)) {
            MmLockUnlockBufferPages((PVOID)buffer->buffer, buffer->size_bytes, TRUE);
            KfLowerIrql(irql);
            return false;
        }
        voice->buffers_hardware[buffer_index] = buffer;
        KfLowerIrql(irql);
        return true;
    }

    KfLowerIrql(irql);
    apu_set_last_error(NX_AUDIO_ERR_QUEUE_FULL);
    return false;
}

