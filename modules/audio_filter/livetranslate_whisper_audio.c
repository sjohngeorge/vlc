/*****************************************************************************
 * livetranslate_whisper_audio.c : Audio capture for Whisper transcription
 *****************************************************************************
 * This audio filter captures audio data and sends it to the livetranslate_whisper
 * subtitle filter for transcription.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_block.h>
#include <vlc_aout.h>
#include <vlc_variables.h>
#include <vlc_threads.h>

#include <string.h>
#include <stdatomic.h>

#include "whisper_shared_vlc.h"

struct filter_sys_t
{
    // For resampling
    float resample_ratio;
    float *resample_buffer;
    size_t resample_buffer_size;
    
    // Temporary buffer for mono conversion
    float *mono_buffer;
    size_t mono_buffer_size;
    
    // Shared buffer
    whisper_shared_buffer_t *shared_buffer;
};

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int OpenFilter(vlc_object_t *);
static void CloseFilter(filter_t *);
static block_t *Process(filter_t *, block_t *);

static const struct vlc_filter_operations filter_ops = {
    .filter_audio = Process,
    .close = CloseFilter,
};

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin()
    set_description(N_("Whisper audio capture"))
    set_shortname(N_("Whisper Audio"))
    set_capability("audio filter", 0)
    set_subcategory(SUBCAT_AUDIO_AFILTER)
    set_callback(OpenFilter)
vlc_module_end()

/*****************************************************************************
 * Shared buffer management using VLC variables
 *****************************************************************************/
static whisper_shared_buffer_t *whisper_shared_get_buffer(vlc_object_t *obj)
{
    // Get the root libvlc object
    libvlc_int_t *p_libvlc = vlc_object_instance(obj);
    if (!p_libvlc)
        return NULL;
        
    // Check if buffer already exists
    var_Create(p_libvlc, "whisper-shared-buffer", VLC_VAR_ADDRESS);
    void *ptr = var_GetAddress(p_libvlc, "whisper-shared-buffer");
    
    if (ptr) {
        whisper_shared_buffer_t *buffer = (whisper_shared_buffer_t *)ptr;
        atomic_fetch_add(&buffer->ref_count, 1);
        return buffer;
    }
    
    // Create new buffer
    whisper_shared_buffer_t *buffer = malloc(sizeof(whisper_shared_buffer_t));
    if (!buffer)
        return NULL;
        
    buffer->samples = malloc(WHISPER_BUFFER_SIZE * sizeof(float));
    if (!buffer->samples) {
        free(buffer);
        return NULL;
    }
    
    memset(buffer->samples, 0, WHISPER_BUFFER_SIZE * sizeof(float));
    atomic_init(&buffer->write_pos, 0);
    atomic_init(&buffer->read_pos, 0);
    atomic_init(&buffer->active, true);
    atomic_init(&buffer->ref_count, 1);
    var_SetAddress(p_libvlc, "whisper-shared-buffer", buffer);
    
    return buffer;
}

static void whisper_shared_release_buffer(vlc_object_t *obj, whisper_shared_buffer_t *buffer)
{
    if (!buffer)
        return;
        
    int count = atomic_fetch_sub(&buffer->ref_count, 1);
    if (count == 1) {
        // Last reference, clean up
        libvlc_int_t *p_libvlc = vlc_object_instance(obj);
        if (p_libvlc) {
            var_SetAddress(p_libvlc, "whisper-shared-buffer", NULL);
            var_Destroy(p_libvlc, "whisper-shared-buffer");
        }
        
        atomic_store(&buffer->active, false);
        free(buffer->samples);
        free(buffer);
    }
}

static int whisper_shared_write_buffer(whisper_shared_buffer_t *buffer, const float *samples, size_t count)
{
    if (!buffer || !atomic_load(&buffer->active))
        return -1;
        
    size_t write_pos = atomic_load(&buffer->write_pos);
    
    for (size_t i = 0; i < count; i++) {
        buffer->samples[write_pos] = samples[i];
        write_pos = (write_pos + 1) % WHISPER_BUFFER_SIZE;
    }
    
    atomic_store(&buffer->write_pos, write_pos);
    return 0;
}

/*****************************************************************************
 * OpenFilter: initialize audio filter
 *****************************************************************************/
static int OpenFilter(vlc_object_t *p_this)
{
    filter_t *p_filter = (filter_t *)p_this;
    
    // We only support float audio input
    if (p_filter->fmt_in.audio.i_format != VLC_CODEC_FL32)
    {
        msg_Warn(p_filter, "Whisper audio filter requires FL32 input, got %4.4s", 
                 (char*)&p_filter->fmt_in.audio.i_format);
        return VLC_EGENERIC;
    }
    
    struct filter_sys_t *p_sys = malloc(sizeof(*p_sys));
    if (!p_sys)
        return VLC_ENOMEM;
    
    p_filter->p_sys = p_sys;
    
    // Calculate resampling ratio
    p_sys->resample_ratio = (float)WHISPER_SAMPLE_RATE / p_filter->fmt_in.audio.i_rate;
    
    // Allocate resampling buffer
    p_sys->resample_buffer_size = 4096;
    p_sys->resample_buffer = malloc(p_sys->resample_buffer_size * sizeof(float));
    if (!p_sys->resample_buffer) {
        free(p_sys);
        return VLC_ENOMEM;
    }
    
    // Allocate mono buffer
    p_sys->mono_buffer_size = 4096;
    p_sys->mono_buffer = malloc(p_sys->mono_buffer_size * sizeof(float));
    if (!p_sys->mono_buffer) {
        free(p_sys->resample_buffer);
        free(p_sys);
        return VLC_ENOMEM;
    }
    
    // Get shared buffer
    p_sys->shared_buffer = whisper_shared_get_buffer(VLC_OBJECT(p_filter));
    if (!p_sys->shared_buffer) {
        msg_Err(p_filter, "Failed to get shared audio buffer");
        free(p_sys->mono_buffer);
        free(p_sys->resample_buffer);
        free(p_sys);
        return VLC_ENOMEM;
    }
    
    // Set output format same as input
    p_filter->fmt_out.audio = p_filter->fmt_in.audio;
    
    p_filter->ops = &filter_ops;
    
    msg_Info(p_filter, "Whisper audio capture initialized (input: %dHz, %d channels)",
             p_filter->fmt_in.audio.i_rate, p_filter->fmt_in.audio.i_channels);
    
    return VLC_SUCCESS;
}

/*****************************************************************************
 * CloseFilter: cleanup
 *****************************************************************************/
static void CloseFilter(filter_t *p_filter)
{
    struct filter_sys_t *p_sys = p_filter->p_sys;
    
    whisper_shared_release_buffer(VLC_OBJECT(p_filter), p_sys->shared_buffer);
    
    free(p_sys->mono_buffer);
    free(p_sys->resample_buffer);
    free(p_sys);
}

/*****************************************************************************
 * Process: capture and forward audio
 *****************************************************************************/
static block_t *Process(filter_t *p_filter, block_t *p_block)
{
    struct filter_sys_t *p_sys = p_filter->p_sys;
    
    if (!p_block)
        return NULL;
    
    // Convert to mono first
    float *input_samples = (float *)p_block->p_buffer;
    size_t input_count = p_block->i_buffer / (sizeof(float) * p_filter->fmt_in.audio.i_channels);
    
    // Ensure mono buffer is large enough
    if (input_count > p_sys->mono_buffer_size) {
        p_sys->mono_buffer_size = input_count * 2;
        p_sys->mono_buffer = realloc(p_sys->mono_buffer, p_sys->mono_buffer_size * sizeof(float));
        if (!p_sys->mono_buffer)
            return p_block;
    }
    
    // Convert to mono by averaging channels
    for (size_t i = 0; i < input_count; i++) {
        float mono_sample = 0.0f;
        for (int ch = 0; ch < p_filter->fmt_in.audio.i_channels; ch++) {
            mono_sample += input_samples[i * p_filter->fmt_in.audio.i_channels + ch];
        }
        p_sys->mono_buffer[i] = mono_sample / p_filter->fmt_in.audio.i_channels;
    }
    
    // Calculate output samples after resampling
    size_t output_count = (size_t)(input_count * p_sys->resample_ratio);
    
    // Ensure resample buffer is large enough
    if (output_count > p_sys->resample_buffer_size) {
        p_sys->resample_buffer_size = output_count * 2;
        p_sys->resample_buffer = realloc(p_sys->resample_buffer, p_sys->resample_buffer_size * sizeof(float));
        if (!p_sys->resample_buffer)
            return p_block;
    }
    
    // Simple linear interpolation resampling
    for (size_t i = 0; i < output_count; i++) {
        float src_idx = i / p_sys->resample_ratio;
        size_t idx = (size_t)src_idx;
        float frac = src_idx - idx;
        
        if (idx + 1 < input_count) {
            p_sys->resample_buffer[i] = p_sys->mono_buffer[idx] * (1.0f - frac) + 
                                       p_sys->mono_buffer[idx + 1] * frac;
        } else if (idx < input_count) {
            p_sys->resample_buffer[i] = p_sys->mono_buffer[idx];
        } else {
            p_sys->resample_buffer[i] = 0.0f;
        }
    }
    
    // Write to shared buffer
    if (whisper_shared_write_buffer(p_sys->shared_buffer, p_sys->resample_buffer, output_count) == 0) {
        static int log_counter = 0;
        if (log_counter++ % 100 == 0) { // Log every 100th call to avoid spam
            msg_Info(p_filter, "Wrote %zu audio samples to shared buffer", output_count);
        }
    }
    
    // Pass through the audio unchanged
    return p_block;
}