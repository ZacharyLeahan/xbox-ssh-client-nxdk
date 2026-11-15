// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Ryzee119

#include "audio_internal.h"
#include <nxaudio.h>

#include <math.h>
#include <stdio.h>

static __thread nxAudioResult g_last_error = 0;

volatile mcpx_voice_t *g_hw_voices = NULL;
static volatile mcpx_ssl_t *g_hw_ssls = NULL;
volatile mcpx_notifier_t *g_hw_notifiers = NULL;
static volatile mcpx_sge_t *g_hw_gp_sges = NULL;
static volatile mcpx_sge_t *g_hw_vp_sges = NULL;
static volatile mcpx_sge_t *g_hw_gp_fifos = NULL;
static void *g_hw_ac97_buffer = NULL;
static void *g_hw_gp_pmem = NULL;
static void *g_hw_hrtf_targets = NULL;
static void *g_hw_hrtf_currents = NULL;
nxAudioVoice *g_tracked_voices[MCPX_HW_MAX_VOICES];
uint32_t g_voice_allocation_mask[(MCPX_HW_MAX_VOICES + 31) / 32];
static KINTERRUPT g_apu_interrupt;
static KDPC g_apu_dpc;
static bool g_isr_dpc_initialized = false;
static ac97_descriptor_t spdif_output_descriptor[1];
static ac97_descriptor_t pcm_output_descriptor[1];

typedef struct _NX_IDLE_VOICE_ENTRY
{
    SINGLE_LIST_ENTRY list_entry;
    uint32_t voice_index;
} NX_IDLE_VOICE_ENTRY, *PNX_IDLE_VOICE_ENTRY;

#define IDLE_VOICE_QUEUE_SIZE 256
NX_IDLE_VOICE_ENTRY g_idle_voice_entries[IDLE_VOICE_QUEUE_SIZE];
SLIST_HEADER g_idle_voice_free_list;
SLIST_HEADER g_idle_voice_active_list;

#include "passthrough_words.h"
volatile LONG g_audio_fault = 0;

/* Fail closed. Keep DMA allocations pinned after a hardware fault. */
void apu_fault(void)
{
    InterlockedExchange(&g_audio_fault, 1);
    *(volatile uint32_t *)(APU_MMIO_BASE + NV_PAPU_IEN) = 0;
    *(volatile uint32_t *)(APU_MMIO_BASE + NV_PAPU_FECTL) = 0;
    *(volatile uint32_t *)(APU_MMIO_BASE + NV_PAPU_SECTL) = 0;
    *(volatile uint32_t *)(APU_MMIO_BASE + APU_GP_OFFSET + NV_PAPU_GPRST) = 0;
    *(volatile uint32_t *)(APU_MMIO_BASE + APU_EP_OFFSET + NV_PAPU_GPRST) = 0;
    volatile ac97_busmaster_regs_t *bm = (void *)AC97_BUSMASTER_MMIO;
    bm->pcm_out_control = 0;
    bm->spdif_control = 0;
    (void)apu_read_reg(NV_PAPU_SECTL);
}

bool nxAudioFaulted(void) { return g_audio_fault != 0; }
void nxAudioAbort(void) { apu_fault(); }

void apu_wait_for_pio(uint32_t count)
{
    if(g_audio_fault)return;
    if(count > 32){apu_fault();return;}
    /* May run at DISPATCH_LEVEL. Never sleep or spin indefinitely here. */
    for(unsigned us=0; us<1000; ++us){
        if(apu_read_reg(APU_VP_OFFSET + NV1BA0_PIO_FREE) >= count*4)return;
        KeStallExecutionProcessor(1);
    }
    apu_fault();
}

void apu_set_last_error (nxAudioResult result)
{
    g_last_error = result;
}

static BOOLEAN NTAPI apu_isr (PKINTERRUPT Interrupt, PVOID ServiceContext)
{
    (void)Interrupt;
    (void)ServiceContext;
    if(g_audio_fault)return FALSE;
    const uint32_t ists = apu_read_reg(NV_PAPU_ISTS);
    apu_write_reg(NV_PAPU_ISTS, ists);

    // Force a flush of ISTS write
    (void)apu_read_reg(NV_PAPU_ISTS);

    // debugPrint("APU ISR: ISTS=0x%08X\n", ists);

    if (!ists) {
        return FALSE;
    }

    // Check for Front-End Trap interrupt
    if (ists & NV_PAPU_ISTS_FETINTSTS) {
        uint32_t fectl = apu_read_reg(NV_PAPU_FECTL);
        const uint32_t meth = apu_read_reg(NV_PAPU_FEDECMETH) & 0xFFFF;
        const uint32_t param = apu_read_reg(NV_PAPU_FEDECPARAM);
        const uint32_t reason = fectl & NV_PAPU_FECTL_FETRAPREASON;

        if (reason != 0 && reason != NV_PAPU_FECTL_FETRAPREASON_REQUESTED) {
            apu_fault();
            return TRUE;

        } else if (reason == NV_PAPU_FECTL_FETRAPREASON_REQUESTED && meth == SE2FE_IDLE_VOICE) {
            // Looks like retail manually removes voices from hardware list on this idle ISR.
            // These needs to be in ISR while VP is trapped to prevent race conditions. The VP maintains a voice
            // list, tvl = top, cvl = current, nvl = next. Each voice is a single linked list that maintains a
            // pointer to the next voice if its on the processing list. To remove the idle voice we have to find it
            // on the hardware processing list, find its predecessor and remove it from the single linked list. We
            // also need to update the hardware pointers to ensure they dont refer to our now idle voice
            const uint32_t idle_voice = param;
            if(idle_voice>=MCPX_HW_MAX_VOICES){apu_fault();return TRUE;}
            uint32_t cfg_fmt;

            // Real hardware Voice Processor cache can sometimes be stale when the trap fires.
            // Read from our tracked software state instead to get the most accurate PERSIST bit.
            if (g_tracked_voices[idle_voice]) {
                cfg_fmt = g_tracked_voices[idle_voice]->cfg_fmt;
            } else {
                cfg_fmt = g_hw_voices[idle_voice].cfg_fmt;
            }

            if (!(cfg_fmt & NV_PAVS_VOICE_CFG_FMT_PERSIST)) {
                const bool is_3d = idle_voice < MCPX_HW_MAX_3D_VOICES;
                const uint32_t reg_tvl = is_3d ? NV_PAPU_TVL3D : NV_PAPU_TVL2D;
                const uint32_t reg_cvl = is_3d ? NV_PAPU_CVL3D : NV_PAPU_CVL2D;
                const uint32_t reg_nvl = is_3d ? NV_PAPU_NVL3D : NV_PAPU_NVL2D;
                uint32_t tvl_2d = apu_read_reg(reg_tvl);
                uint32_t cvl_2d = apu_read_reg(reg_cvl);
                uint32_t nvl_2d = apu_read_reg(reg_nvl);
                uint32_t voice_next = 0xFFFF;
                uint32_t voice_prev = 0xFFFF;
                uint32_t temp;

                voice_next = g_hw_voices[idle_voice].tar_pitch_link & NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE;
                if(voice_next!=0xFFFF && voice_next>=MCPX_HW_MAX_VOICES){apu_fault();return TRUE;}
                if (voice_next == idle_voice) {
                    voice_next = 0xFFFF;
                }

                uint32_t current_voice = tvl_2d;
                unsigned traversed=0;
                while (current_voice != 0xFFFF) {
                    if(current_voice>=MCPX_HW_MAX_VOICES || ++traversed>MCPX_HW_MAX_VOICES){
                        apu_fault();return TRUE;
                    }
                    if (current_voice == idle_voice) {
                        break;
                    }
                    voice_prev = current_voice;
                    current_voice =
                        g_hw_voices[current_voice].tar_pitch_link & NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE;
                }

                if (current_voice != 0xFFFF) {
                    if (voice_prev != 0xFFFF) {
                        temp = g_hw_voices[voice_prev].tar_pitch_link &
                               ~((uint32_t)NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE);
                        temp |= voice_next;
                        g_hw_voices[voice_prev].tar_pitch_link = temp;
                    } else if (tvl_2d == idle_voice) {
                        tvl_2d = voice_next;
                        apu_write_reg(reg_tvl, tvl_2d);
                    }
                }

                temp = g_hw_voices[idle_voice].tar_pitch_link &
                       ~((uint32_t)NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE);
                temp |= idle_voice;
                g_hw_voices[idle_voice].tar_pitch_link = temp;

                if (cvl_2d == idle_voice) {
                    if (voice_next != 0xFFFF) {
                        apu_write_reg(reg_cvl, voice_next);
                        uint32_t voice_next_next =
                            g_hw_voices[voice_next].tar_pitch_link & NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE;
                        apu_write_reg(reg_nvl, (voice_next_next == 0xFFFF) ? tvl_2d : voice_next_next);
                    } else {
                        apu_write_reg(reg_cvl, 0xFFFF);
                        apu_write_reg(reg_nvl, tvl_2d);
                    }
                }
                if (nvl_2d == idle_voice) {
                    apu_write_reg(reg_nvl, (voice_next != 0xFFFF) ? voice_next : tvl_2d);
                }

                const uint32_t notifier_index = MCPX_HW_NOTIFIER_BASE_OFFSET + (idle_voice * MCPX_HW_NOTIFIER_COUNT);
                g_hw_notifiers[notifier_index + 0].status = NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS;
                g_hw_notifiers[notifier_index + 1].status = NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS;

                // Queue the now idle voice to be cleaned up by the DPC
                // We always defer to the DPC because FEVINTSTS is not guaranteed to fire
                // (e.g., if we manually stop a voice), but we still need the DPC to run
                // to free buffers.
                PNX_IDLE_VOICE_ENTRY idle_entry =
                    (PNX_IDLE_VOICE_ENTRY)InterlockedPopEntrySList(&g_idle_voice_free_list);
                if (idle_entry) {
                    idle_entry->voice_index = idle_voice;
                    InterlockedPushEntrySList(&g_idle_voice_active_list, &idle_entry->list_entry);
                    KeInsertQueueDpc(&g_apu_dpc, NULL, NULL);
                } else {apu_fault();return TRUE;}
            }
        }

        // Reset FECTL to recover engine. Retail seems to set halted, then free running sequentially.
        fectl &= ~((uint32_t)NV_PAPU_FECTL_FEMETHMODE);
        fectl |= NV_PAPU_FECTL_FEMETHMODE_HALTED;
        apu_write_reg(NV_PAPU_FECTL, fectl);

        fectl &= ~((uint32_t)NV_PAPU_FECTL_FEMETHMODE);
        fectl |= NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING;
        apu_write_reg(NV_PAPU_FECTL, fectl);
    }

    if (ists & NV_PAPU_ISTS_FEVINTSTS) {
        KeInsertQueueDpc(&g_apu_dpc, NULL, NULL);
    }

    return TRUE;
}

static void NTAPI apu_dpc (PKDPC Dpc, PVOID DeferredContext, PVOID arg1, PVOID arg2)
{
    (void)Dpc;
    (void)DeferredContext;
    (void)arg1;
    (void)arg2;
    if(g_audio_fault)return;

    for (uint32_t i = 0; i < (MCPX_HW_MAX_VOICES + 31) / 32; i++) {
        // Small optimisation to only check allocated voices,
        uint32_t allocated_voices = ~g_voice_allocation_mask[i];
        while (allocated_voices) {
            const uint32_t allocated_voice = (uint32_t)__builtin_ctz(allocated_voices);
            allocated_voices &= ~(1U << allocated_voice);

            const uint32_t voice_index = (i * 32) + allocated_voice;
            nxAudioVoice *voice = g_tracked_voices[voice_index];
            if (!voice) {
                continue;
            }
            const uint32_t notifier_index = MCPX_HW_NOTIFIER_BASE_OFFSET + (voice_index * MCPX_HW_NOTIFIER_COUNT);

            // Check SSLA and SSLB notifiers for this voice. Handle it
            for (uint32_t j = 0; j < 2; j++) {
                if (g_hw_notifiers[notifier_index + j].status == NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS) {
                    const nxAudioBuffer *buffer = voice->buffers_hardware[j];
                    if (!buffer) {
                        continue;
                    }
                    MmLockUnlockBufferPages((PVOID)buffer->buffer, buffer->size_bytes, TRUE);
                    if (voice->streaming) {
                        voice->buffers_hardware[j] = NULL;
                        if (voice->callback) {
                            voice->callback(voice, buffer, voice->user_context);
                        }
                    }
                }
            }
        }
    }

    // Process the deferred idle voice list
    PSINGLE_LIST_ENTRY entry;
    unsigned idle_count=0;
    while ((entry = InterlockedPopEntrySList(&g_idle_voice_active_list)) != NULL) {
        if(++idle_count>IDLE_VOICE_QUEUE_SIZE){apu_fault();return;}
        const PNX_IDLE_VOICE_ENTRY idle_entry =
            (PNX_IDLE_VOICE_ENTRY)(void *)((char *)entry - __builtin_offsetof(NX_IDLE_VOICE_ENTRY, list_entry));
        const uint32_t voice_idx = idle_entry->voice_index;
        if(voice_idx>=MCPX_HW_MAX_VOICES){apu_fault();return;}
        nxAudioVoice *voice = g_tracked_voices[voice_idx];
        if (voice) {
            g_tracked_voices[voice_idx] = NULL;
            voice->state = NX_STOPPED;
        }
        InterlockedPushEntrySList(&g_idle_voice_free_list, entry);
    }
}

nxAudioResult nxAudioGetLastError (void)
{
    return g_audio_fault ? NX_AUDIO_ERR_TIMEOUT : g_last_error;
}

bool nxAudioInit (const nxAudioInitParams *parameters)
{
    (void)parameters;
    if(g_audio_fault)return false;
    volatile ac97_mixer_regs_t *mixer = (ac97_mixer_regs_t *)AC97_MIXER_MMIO;
    volatile ac97_busmaster_regs_t *bm = (ac97_busmaster_regs_t *)AC97_BUSMASTER_MMIO;

    g_idle_voice_free_list.Alignment = 0;
    g_idle_voice_active_list.Alignment = 0;

    for (int i = 0; i < IDLE_VOICE_QUEUE_SIZE; i++) {
        InterlockedPushEntrySList(&g_idle_voice_free_list, &g_idle_voice_entries[i].list_entry);
    }

#define APU_ALLOC(ptr, size, align)                                                                                    \
    ptr = (void *)MmAllocateContiguousMemoryEx((size), 0, 0xFFFFFFFF, (align), PAGE_READWRITE);                        \
    if (!(ptr)) {                                                                                                      \
        nxAudioShutdown();                                                                                             \
        apu_set_last_error(NX_AUDIO_ERR_OUT_OF_MEMORY);                                                                \
        return false;                                                                                                  \
    }                                                                                                                  \
    memset((void *)(ptr), 0, (size))

    APU_ALLOC(g_hw_voices, HW_VOICE_ARRAY_SIZE, 0x8000);
    APU_ALLOC(g_hw_ssls, HW_VOICE_SSL_ARRAY_SIZE, 0x4000);
    APU_ALLOC(g_hw_notifiers, HW_VOICE_NOTIFIERS_ARRAY_SIZE, 0x4000);
    APU_ALLOC(g_hw_gp_sges, MCPX_HW_GP_SGE_ARRAY_SIZE, 0x4000);
    APU_ALLOC(g_hw_vp_sges, HW_VOICE_SGE_ARRAY_SIZE, 0x4000);
    APU_ALLOC(g_hw_gp_fifos, GP_FIFO_OUTPUT_SIZE, 0x4000);
    APU_ALLOC(g_hw_ac97_buffer, AC97_BUFFER_SIZE, 0x4000);
    APU_ALLOC(g_hw_gp_pmem, MAX_DSP_PAYLOAD, 0x4000);
    APU_ALLOC(g_hw_hrtf_targets, HW_HRTF_TARGET_ARRAY_SIZE, 0x4000);
    APU_ALLOC(g_hw_hrtf_currents, HW_HRTF_CURRENT_ARRAY_SIZE, 0x4000);
#undef APU_ALLOC

    memset(g_voice_allocation_mask, 0xFF, sizeof(g_voice_allocation_mask));
    memset(g_tracked_voices, 0, sizeof(g_tracked_voices));

    // Reset all notifiers to "In Progress".
    for (int i = 0; i < MCPX_HW_MAX_NOTIFIERS; i++) {
        g_hw_notifiers[i].status = NV1BA0_NOTIFICATION_STATUS_IN_PROGRESS;
    }

    // All voices should link to themselves
    for (int i = 0; i < MCPX_HW_MAX_VOICES; i++) {
        g_hw_voices[i].tar_pitch_link = (i & 0xFFFF);
    }

    // Disable interrupts and front-end traps
    apu_write_reg(NV_PAPU_IEN, 0);
    apu_write_reg(NV_PAPU_FECTL, 0);

    // Disable the sound engine
    apu_write_reg(NV_PAPU_SECTL, 0x00000000);

    // This is required or we trap. Enable writing for the below registers?
    apu_write_reg(0x00001510, 0x00000001);

    // Disable GP and EP
    apu_write_reg(APU_GP_OFFSET + NV_PAPU_GPRST, 0x00000000);
    apu_write_reg(APU_EP_OFFSET + NV_PAPU_GPRST, 0x00000000);

    // Clear voice lists
    apu_write_reg(NV_PAPU_TVL2D, 0xFFFF);
    apu_write_reg(NV_PAPU_TVL3D, 0xFFFF);
    apu_write_reg(NV_PAPU_TVLMP, 0xFFFF);
    apu_write_reg(NV_PAPU_CVL2D, 0xFFFF);
    apu_write_reg(NV_PAPU_CVL3D, 0xFFFF);
    apu_write_reg(NV_PAPU_CVLMP, 0xFFFF);
    apu_write_reg(NV_PAPU_NVL2D, 0xFFFF);
    apu_write_reg(NV_PAPU_NVL3D, 0xFFFF);
    apu_write_reg(NV_PAPU_NVLMP, 0xFFFF);

    // Set up something for the front end traps. Enable IDLE_VOICE trap?
    apu_write_reg(NV_PAPU_FETFORCE0, 0x00000000);
    apu_write_reg(NV_PAPU_FETFORCE1, NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE);
    apu_write_reg(0x1508, 0x00800040);
    apu_write_reg(0x150c, 0x00000000);

    // Set Counters - FIXME: Confirm values
    apu_write_reg(0x2008, 0x00000800);
    apu_write_reg(NV_PAPU_XGSCNT, 0x00000000);
    apu_write_reg(0x2010, 0x00000800);
    apu_write_reg(0x2014, 0x00000400);
    apu_write_reg(0x2018, 0x000007ff);
    apu_write_reg(0x201c, 0x00000000);
    apu_write_reg(0x2020, 0x00000600);
    apu_write_reg(0x2024, 0x00000100);
    apu_write_reg(0x2028, 0x00000100);

    // FIXME: What are these
    apu_write_reg(0x1104, 0x000000FF);
    apu_write_reg(0x1108, 0x0000003F);
    apu_write_reg(0x111c, 0x0000007F);
    apu_write_reg(0x1124, 0x00001FFF);
    apu_write_reg(0x1138, 0x000007FF);
    apu_write_reg(0x1158, 0x00000020);
    apu_write_reg(0x112c, 0x00000000);
    apu_write_reg(0x1130, 0x08000000);
    apu_write_reg(0x1140, 0x00000000);
    apu_write_reg(0x1144, 0x08000000);
    apu_write_reg(0x1150, 0x00000000);
    apu_write_reg(0x1154, 0x08000000);

    apu_write_reg(0x1510, 0x00000000); // Undo what we did earlier

    // Soft power up? Maybe turn on VP but no processing
    apu_write_reg(NV_PAPU_SECTL, 0x00000007);

    // Setup voice Processor Voice Address to voice structures
    apu_write_reg(NV_PAPU_VPVADDR, (uint32_t)MmGetPhysicalAddress((void *)g_hw_voices));

    // Voice Process Scatter-Gather Stream List Address
    apu_write_reg(NV_PAPU_VPSSLADDR, (uint32_t)MmGetPhysicalAddress((void *)g_hw_ssls));

    // Voice Process Notifier Address
    apu_write_reg(NV_PAPU_FENADDR, (uint32_t)MmGetPhysicalAddress((void *)g_hw_notifiers));

    // HRTF Target and Current Address
    apu_write_reg(0x2038, (uint32_t)MmGetPhysicalAddress(g_hw_hrtf_targets));
    apu_write_reg(0x203C, (uint32_t)MmGetPhysicalAddress(g_hw_hrtf_currents));

    // GP FIFO Scatter-Gather Element Address
    apu_write_reg(NV_PAPU_GPFADDR, (uint32_t)MmGetPhysicalAddress((void *)g_hw_gp_fifos));

    // GP Scatter-Gather Element Address - In this driver its only purpose is to load in DSP assembler code into the
    // GP memory space.
    apu_write_reg(NV_PAPU_GPSADDR, (uint32_t)MmGetPhysicalAddress((void *)g_hw_gp_sges));

    // Initialize physical SGE array for GP and VP
    apu_write_reg(NV_PAPU_VPSGEADDR, (uint32_t)MmGetPhysicalAddress((void *)g_hw_vp_sges));

    // FIXME - What are these
    apu_write_reg(APU_VP_OFFSET + 0x2A0, 0x00000FFF); // ?
    apu_write_reg(APU_VP_OFFSET + 0x2A4, 0x00000FFF); // ?
    apu_write_reg(APU_VP_OFFSET + 0x2A8, 0x00000FFF); // ?
    apu_write_reg(APU_VP_OFFSET + 0x2AC, 0x00000FFF); // ?
    apu_write_reg(APU_VP_OFFSET + 0x2B0, 0x00000FFF); // ?

    // Initalise AC97 hardware and point them to our DMA descriptors.
    {
        pcm_output_descriptor[0].buffer_start_address = MmGetPhysicalAddress(g_hw_ac97_buffer);
        pcm_output_descriptor[0].buffer_sample_count = (AC97_BUFFER_SIZE / 2);
        pcm_output_descriptor[0].buffer_control = 0x0000;

        spdif_output_descriptor[0].buffer_start_address = MmGetPhysicalAddress(g_hw_ac97_buffer);
        spdif_output_descriptor[0].buffer_sample_count = (AC97_BUFFER_SIZE / 2);
        spdif_output_descriptor[0].buffer_control = 0x0000;

        // Trigger Reset and wait for the Codec to report Ready
        bm->global_control |= AC97_GLOBAL_CR_RESET;
        for(unsigned us=0; !(bm->global_status & AC97_GLOBAL_SR_CODEC_READY); ++us){
            if(us>=100000){apu_fault();return false;}
            KeStallExecutionProcessor(1);
        }

        // Turn everything on - Reset mixer powerdown and PCM/SPDIF control
        mixer->powerdown_control = AC97_POWER_NORMAL;
        bm->pcm_out_control = AC97_BM_CR_RESET;
        bm->spdif_control = AC97_BM_CR_RESET;

        // Set volumes
        mixer->master_volume = 0x0000;
        mixer->pcm_out_volume = 0x0808; // -12dB on left and right channel

        // Reset PCM out
        bm->pcm_out_control = AC97_BM_CR_RESET;
        for(unsigned us=0; bm->pcm_out_control & AC97_BM_CR_RESET; ++us){
            if(us>=100000){apu_fault();return false;}
            KeStallExecutionProcessor(1);
        }

        // Reset SPDIF out
        bm->spdif_control = AC97_BM_CR_RESET;
        for(unsigned us=0; bm->spdif_control & AC97_BM_CR_RESET; ++us){
            if(us>=100000){apu_fault();return false;}
            KeStallExecutionProcessor(1);
        }

        bm->pcm_out_buffer_base = (uint32_t)MmGetPhysicalAddress(pcm_output_descriptor);
        bm->pcm_out_current_index = 0;
        bm->pcm_out_last_valid_index = 0;

        bm->spdif_buffer_base = (uint32_t)MmGetPhysicalAddress(spdif_output_descriptor);
        bm->spdif_current_index = 0;
        bm->spdif_last_valid_index = 0;

        // ?
        bm->x7c_unk_control = AC97_7C_UNK;
    }

    // Set the GP FIFO size characteristics
    {
        apu_write_reg(NV_PAPU_GPFMAXSGE, (GP_FIFO_OUTPUT_SIZE / PAGE_SIZE) - 1); // How many pages
        apu_write_reg(0x1148, (GP_FIFO_OUTPUT_SIZE / PAGE_SIZE) - 1);            // ? Always seem to be same as above
        apu_write_reg(NV_PAPU_GPOFBASE0, 0);
        apu_write_reg(NV_PAPU_GPOFEND0, GP_FIFO_OUTPUT_SIZE);

        // Point the GP FIFO SGE table to our AC97 buffer in RAM
        for (uint32_t i = 0; i < (AC97_BUFFER_SIZE / PAGE_SIZE); i++) {
            g_hw_gp_fifos[i].phys_addr = MmGetPhysicalAddress(g_hw_ac97_buffer) + (i * PAGE_SIZE);
            g_hw_gp_fifos[i].length_and_format = PAGE_SIZE;

            apu_wait_for_pio(2);
            apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_CURRENT_OUTBUF_SGE, i);
            apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_CURRENT_OUTBUF_SGE_OFFSET, g_hw_gp_fifos[i].phys_addr);
        }

        // We are only using GP FIFO0, so set the base address in our SGE array (0 position) and length for the GP FIFO0
        const uint32_t gp_fifo_index = 0;
        apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_OUTBUF_BA + gp_fifo_index * 8, gp_fifo_index * PAGE_SIZE);
        apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_OUTBUF_LEN + gp_fifo_index * 8, AC97_BUFFER_SIZE);

        // Reset GP FIFO0 positions
        const uint32_t fifo_index_pos = gp_fifo_index * 0x10;
        apu_write_reg(0x2000 + fifo_index_pos, apu_read_reg(0x2000 + fifo_index_pos));
        apu_write_reg(0x2004 + fifo_index_pos, apu_read_reg(0x2004 + fifo_index_pos));
        apu_write_reg(0x2008 + fifo_index_pos, apu_read_reg(0x2008 + fifo_index_pos));
        apu_write_reg(0x200C + fifo_index_pos, apu_read_reg(0x200C + fifo_index_pos));
    }

    // Retail directs HRTF submixes to MIXBUF 6 and 8 for LEFT, and 7 and 9 for RIGHT audio. These are mixed to the
    // output MIXBINs within the DSP
    const uint32_t submix_bins = APU_MAKE_VALUE(0x0000001F, 6) | APU_MAKE_VALUE(0x00001F00, 8) |
                                 APU_MAKE_VALUE(0x001F0000, 7) | APU_MAKE_VALUE(0x1F000000, 9);
    apu_wait_for_pio(2);
    apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_HRTF_SUBMIXES, submix_bins);
    apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_HRTF_HEADROOM, 0);
    for (uint32_t i = 0; i < 31; i++) {
        apu_wait_for_pio(1);
        apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_SUBMIX_HEADROOM + (i * 4), 1);
    }
    apu_wait_for_pio(1);
    apu_write_reg(APU_VP_OFFSET + NV1BA0_PIO_SET_SUBMIX_HEADROOM + (31 * 4), 0);

    // Soft power on GP?
    apu_write_reg(APU_GP_OFFSET + NV_PAPU_GPRST, NV_PAPU_GPRST_GPRST);
    apu_write_reg(APU_GP_OFFSET + 0xFF10, 0x01); // ?
    apu_write_reg(APU_GP_OFFSET + 0xFF14, 0xFF); // ?

    // Prep DSP code for bootstrapping the GP. Seems like its hardcoded to read two pages of 4k each
    {
        apu_write_reg(NV_PAPU_GPSMAXSGE, MCPX_HW_GP_SGE_ARRAY_SIZE / sizeof(mcpx_sge_t));
        memcpy(g_hw_gp_pmem, passthrough_words, sizeof(passthrough_words));
        g_hw_gp_sges[0].phys_addr = MmGetPhysicalAddress(g_hw_gp_pmem);
        g_hw_gp_sges[0].length_and_format = PAGE_SIZE;
        g_hw_gp_sges[1].phys_addr = MmGetPhysicalAddress(g_hw_gp_pmem) + PAGE_SIZE;
        g_hw_gp_sges[1].length_and_format = PAGE_SIZE;
    }

    // Enable GP which automatically reads in the DSP code (Now setup in NV_PAPU_GPSADDR)
    apu_write_reg(APU_GP_OFFSET + NV_PAPU_GPRST, NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST);


    // Connect to the APU's IRQ line and setup our ISR and DPC
    KIRQL irql;
    const ULONG vector = HalGetInterruptVector(XBOX_APU_IRQ, &irql);
    KeInitializeDpc(&g_apu_dpc, apu_dpc, NULL);
    KeInitializeInterrupt(&g_apu_interrupt, &apu_isr, NULL, vector, irql, LevelSensitive, TRUE);
    if(!KeConnectInterrupt(&g_apu_interrupt)){apu_fault();return false;}
    g_isr_dpc_initialized = true;
    // Install the handler before enabling hardware interrupts.
    apu_write_reg(NV_PAPU_ISTS, 0xFFFFFFFF);
    apu_write_reg(NV_PAPU_IEN,
                  NV_PAPU_ISTS_GINTSTS | NV_PAPU_ISTS_FETINTSTS | NV_PAPU_ISTS_FENINTSTS | NV_PAPU_ISTS_FEVINTSTS);
    apu_write_reg(NV_PAPU_FECTL, 0x0000100F);


    if(g_audio_fault)return false;

    // Start the Sound Engine
    apu_write_reg(NV_PAPU_SECTL, 0x0000000F);

    // Start AC97
    bm->spdif_control = AC97_BM_CR_START;
    bm->pcm_out_control = AC97_BM_CR_START;
    return true;
}

void nxAudioShutdown (void)
{
    if(g_audio_fault){
        /* CPU callbacks must be disconnected even when DMA memory is quarantined. */
        if(g_isr_dpc_initialized){
            KeDisconnectInterrupt(&g_apu_interrupt);
            KeRemoveQueueDpc(&g_apu_dpc);
            g_isr_dpc_initialized=false;
        }
        return;
    }
    volatile ac97_busmaster_regs_t *bm = (ac97_busmaster_regs_t *)AC97_BUSMASTER_MMIO;
    bm->spdif_control = AC97_BM_CR_RESET;
    bm->pcm_out_control = AC97_BM_CR_RESET;

    for(unsigned us=0; (bm->pcm_out_control|bm->spdif_control)&AC97_BM_CR_RESET; ++us){
        if(us>=100000){apu_fault();return;}
        KeStallExecutionProcessor(1);
    }
    apu_write_reg(NV_PAPU_IEN, 0x00000000);

    apu_write_reg(0x1510, 0x00000001);
    apu_write_reg(NV_PAPU_FECTL, 0);
    apu_write_reg(NV_PAPU_SECTL, 0);

    if (g_isr_dpc_initialized) {
        KeDisconnectInterrupt(&g_apu_interrupt);
        KeRemoveQueueDpc(&g_apu_dpc);
        g_isr_dpc_initialized = false;
    }

    apu_write_reg(APU_GP_OFFSET + NV_PAPU_GPRST, 0x00000000);
    apu_write_reg(APU_EP_OFFSET + NV_PAPU_GPRST, 0x00000000);

#define APU_FREE(ptr)                                                                                                  \
    if (ptr) {                                                                                                         \
        MmFreeContiguousMemory((void *)(ptr));                                                                         \
        ptr = NULL;                                                                                                    \
    }
    APU_FREE(g_hw_ac97_buffer);
    APU_FREE(g_hw_voices);
    APU_FREE(g_hw_ssls);
    APU_FREE(g_hw_notifiers);
    APU_FREE(g_hw_gp_sges);
    APU_FREE(g_hw_vp_sges);
    APU_FREE(g_hw_gp_fifos);
    APU_FREE(g_hw_gp_pmem);
    APU_FREE(g_hw_hrtf_targets);
    APU_FREE(g_hw_hrtf_currents);
#undef APU_FREE
}

