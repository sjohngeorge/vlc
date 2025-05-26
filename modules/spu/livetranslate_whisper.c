/*****************************************************************************
 * livetranslate_whisper.c : live transcription with Whisper for VLC
 *****************************************************************************
 * This is a version that integrates with whisper.cpp for real transcription
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_subpicture.h>
#include <vlc_aout.h>
#include <vlc_variables.h>
#include <vlc_threads.h>

/* Note: To use this, you need to:
 * 1. Install whisper.cpp: https://github.com/ggerganov/whisper.cpp
 * 2. Build it and install the library
 * 3. Download a model (e.g., base model)
 * 4. Link against libwhisper when building this plugin
 */

#include <whisper.h>
#include "whisper_shared_vlc.h"

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int CreateFilter( filter_t * );
static void DestroyFilter( filter_t * );
static subpicture_t *Filter( filter_t *, vlc_tick_t );

#define WHISPER_MODEL_TEXT N_("Whisper model path")
#define WHISPER_MODEL_LONGTEXT N_("Path to the Whisper model file (e.g., /path/to/models/ggml-base.bin)")

#define LANGUAGE_TEXT N_("Language")  
#define LANGUAGE_LONGTEXT N_("Language code for transcription (e.g., 'en', 'ja', 'auto' for auto-detect)")

#define TRANSLATE_TEXT N_("Translate to English")
#define TRANSLATE_LONGTEXT N_("Translate audio to English instead of transcribing in original language")

#define CFG_PREFIX "livetranslate-whisper-"

vlc_module_begin()
    set_shortname( N_("Live Transcribe") )
    set_description( N_("Live transcription using Whisper") )
    set_capability( "sub source", 100 )
    set_subcategory( SUBCAT_VIDEO_SUBPIC )
    set_callback_sub_source( CreateFilter, 0 )
    
    add_string( CFG_PREFIX "model", "/home/sharathg/vlc/whisper-models/ggml-base.bin", WHISPER_MODEL_TEXT, WHISPER_MODEL_LONGTEXT )
    add_string( CFG_PREFIX "language", "auto", LANGUAGE_TEXT, LANGUAGE_LONGTEXT )
    add_bool( CFG_PREFIX "translate", true, TRANSLATE_TEXT, TRANSLATE_LONGTEXT )
    add_integer( CFG_PREFIX "position", 8, "Position", "Subtitle position" )
    add_integer( CFG_PREFIX "size", 0, "Font size", "Font size in pixels" )
    add_rgb( CFG_PREFIX "color", 0xFFFFFF, "Color", "Text color" )
    add_integer_with_range( CFG_PREFIX "opacity", 255, 0, 255, "Opacity", "Text opacity" )
    
    add_shortcut( "livetranscribe", "whisper" )
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

static int whisper_shared_read_buffer(whisper_shared_buffer_t *buffer, float *samples, size_t count)
{
    if (!buffer || !atomic_load(&buffer->active))
        return -1;
        
    size_t write_pos = atomic_load(&buffer->write_pos);
    size_t read_pos = atomic_load(&buffer->read_pos);
    
    // Calculate available samples
    size_t available_samples = 0;
    if (write_pos >= read_pos) {
        available_samples = write_pos - read_pos;
    } else {
        available_samples = WHISPER_BUFFER_SIZE - read_pos + write_pos;
    }
    
    if (available_samples < count)
        return -1;
        
    for (size_t i = 0; i < count; i++) {
        samples[i] = buffer->samples[(read_pos + i) % WHISPER_BUFFER_SIZE];
    }
    
    atomic_store(&buffer->read_pos, (read_pos + count) % WHISPER_BUFFER_SIZE);
    return 0;
}

static size_t whisper_shared_available_buffer(whisper_shared_buffer_t *buffer)
{
    if (!buffer || !atomic_load(&buffer->active))
        return 0;
        
    size_t write_pos = atomic_load(&buffer->write_pos);
    size_t read_pos = atomic_load(&buffer->read_pos);
    
    if (write_pos >= read_pos) {
        return write_pos - read_pos;
    } else {
        return WHISPER_BUFFER_SIZE - read_pos + write_pos;
    }
}

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static subpicture_t *Filter( filter_t *, vlc_tick_t );
static void DestroyFilter( filter_t * );

static const struct vlc_filter_operations filter_ops = {
    .source_sub = Filter, .close = DestroyFilter,
};

// Use shared definitions from audio filter

struct filter_sys_t
{
    /* Whisper context */
    struct whisper_context *whisper_ctx;
    
    /* Shared buffer */
    whisper_shared_buffer_t *shared_buffer;
    
    /* Transcription */
    char current_text[1024];
    vlc_tick_t last_update;
    vlc_mutex_t text_mutex;
    
    /* Configuration */
    char *model_path;
    char *language;
    bool b_translate;
    int i_pos;
    int i_size;
    text_style_t *p_style;
    
    /* Processing thread */
    vlc_thread_t processing_thread;
    bool b_processing_active;
};


/*****************************************************************************
 * Audio processing thread
 *****************************************************************************/
static void *ProcessingThread(void *data)
{
    filter_t *p_filter = (filter_t *)data;
    struct filter_sys_t *p_sys = p_filter->p_sys;
    
    msg_Info(p_filter, "Whisper processing thread started");
    
    // Test counter for demo
    int test_counter = 0;
    const char *test_messages[] = {
        "Whisper transcription initializing...",
        "Waiting for audio input...",
        "Audio capture not yet implemented",
        "This is a test of the subtitle system",
        "Real transcription will appear here"
    };
    
    // Allocate processing buffer
    float *process_buffer = malloc(WHISPER_SAMPLE_RATE * 30 * sizeof(float));
    if (!process_buffer) {
        msg_Err(p_filter, "Failed to allocate processing buffer");
        return NULL;
    }
    
    while (p_sys->b_processing_active) {
        vlc_tick_sleep(VLC_TICK_FROM_SEC(2)); // Process every 2 seconds
        
        // Check available audio samples
        size_t available_samples = whisper_shared_available_buffer(p_sys->shared_buffer);
        
        // Log available samples periodically
        static int check_counter = 0;
        if (check_counter++ % 5 == 0) { // Every 10 seconds
            msg_Info(p_filter, "Available audio samples: %zu (need %d)", available_samples, WHISPER_SAMPLE_RATE * 2);
        }
        
        // Need at least 2 seconds of audio
        if (available_samples < WHISPER_SAMPLE_RATE * 2) {
            // Show waiting message
            if (test_counter < 5) {
                vlc_mutex_lock(&p_sys->text_mutex);
                strncpy(p_sys->current_text, test_messages[test_counter % 5], sizeof(p_sys->current_text) - 1);
                p_sys->current_text[sizeof(p_sys->current_text) - 1] = '\0';
                p_sys->last_update = vlc_tick_now();
                vlc_mutex_unlock(&p_sys->text_mutex);
                test_counter++;
            }
            continue;
        }
        
        // Limit to what we can process
        size_t samples_to_process = available_samples;
        if (samples_to_process > WHISPER_SAMPLE_RATE * 5) {
            samples_to_process = WHISPER_SAMPLE_RATE * 5; // Process max 5 seconds at a time
        }
        
        // Read audio for processing
        if (whisper_shared_read_buffer(p_sys->shared_buffer, process_buffer, samples_to_process) != 0) {
            msg_Err(p_filter, "Failed to read audio from shared buffer");
            continue;
        }
        
        msg_Info(p_filter, "Processing %zu audio samples with Whisper", samples_to_process);
        
        // Process with Whisper
        if (p_sys->whisper_ctx) {
            struct whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
            params.language = p_sys->language;
            params.translate = p_sys->b_translate;  // Enable translation if requested
            params.n_threads = 4;
            params.print_progress = false;
            params.print_realtime = false;
            
            if (whisper_full(p_sys->whisper_ctx, params, process_buffer, (int)samples_to_process) == 0) {
                const int n_segments = whisper_full_n_segments(p_sys->whisper_ctx);
                if (n_segments > 0) {
                    // Get the last segment's text
                    const char *text = whisper_full_get_segment_text(p_sys->whisper_ctx, n_segments - 1);
                    
                    vlc_mutex_lock(&p_sys->text_mutex);
                    strncpy(p_sys->current_text, text, sizeof(p_sys->current_text) - 1);
                    p_sys->current_text[sizeof(p_sys->current_text) - 1] = '\0';
                    p_sys->last_update = vlc_tick_now();
                    vlc_mutex_unlock(&p_sys->text_mutex);
                    
                    msg_Info(p_filter, "Whisper transcription: %s", text);
                }
            } else {
                msg_Err(p_filter, "Whisper processing failed");
            }
        }
    }
    
    free(process_buffer);
    msg_Info(p_filter, "Whisper processing thread ended");
    return NULL;
}


/*****************************************************************************
 * CreateFilter: allocate whisper transcription filter
 *****************************************************************************/
static int CreateFilter( filter_t *p_filter )
{
    struct filter_sys_t *p_sys;
    
    p_sys = p_filter->p_sys = calloc(1, sizeof(struct filter_sys_t));
    if (!p_sys)
        return VLC_ENOMEM;
    
    // Initialize mutexes
    vlc_mutex_init(&p_sys->text_mutex);
    
    // Initialize timing
    p_sys->last_update = 0;
    p_sys->current_text[0] = '\0';
    
    // Get configuration
    p_sys->model_path = var_InheritString(p_filter, CFG_PREFIX "model");
    p_sys->language = var_InheritString(p_filter, CFG_PREFIX "language");
    p_sys->b_translate = var_InheritBool(p_filter, CFG_PREFIX "translate");
    p_sys->i_pos = var_InheritInteger(p_filter, CFG_PREFIX "position");
    p_sys->i_size = var_InheritInteger(p_filter, CFG_PREFIX "size");
    
    // Style
    p_sys->p_style = text_style_Create(STYLE_NO_DEFAULTS);
    if (!p_sys->p_style) {
        free(p_sys);
        return VLC_ENOMEM;
    }
    
    p_sys->p_style->i_font_color = var_InheritInteger(p_filter, CFG_PREFIX "color");
    p_sys->p_style->i_features |= STYLE_HAS_FONT_COLOR;
    p_sys->p_style->i_font_alpha = var_InheritInteger(p_filter, CFG_PREFIX "opacity");
    p_sys->p_style->i_features |= STYLE_HAS_FONT_ALPHA;
    
    if (p_sys->i_size > 0) {
        p_sys->p_style->i_font_size = p_sys->i_size;
    }
    
    // Get shared buffer
    p_sys->shared_buffer = whisper_shared_get_buffer(VLC_OBJECT(p_filter));
    if (!p_sys->shared_buffer) {
        msg_Err(p_filter, "Failed to get shared audio buffer");
        text_style_Delete(p_sys->p_style);
        free(p_sys->language);
        free(p_sys->model_path);
        // VLC mutexes don't need explicit destruction
        free(p_sys);
        return VLC_EGENERIC;
    }
    
    // Initialize Whisper
    if (p_sys->model_path && *p_sys->model_path) {
        msg_Info(p_filter, "Loading Whisper model from: %s", p_sys->model_path);
        
        // Initialize Whisper with default parameters
        struct whisper_context_params cparams = whisper_context_default_params();
        p_sys->whisper_ctx = whisper_init_from_file_with_params(p_sys->model_path, cparams);
        if (!p_sys->whisper_ctx) {
            msg_Err(p_filter, "Failed to load Whisper model from %s", p_sys->model_path);
        } else {
            msg_Info(p_filter, "Whisper model loaded successfully");
            msg_Info(p_filter, "Translation mode: %s", p_sys->b_translate ? "enabled (to English)" : "disabled (transcribe only)");
        }
        
        strcpy(p_sys->current_text, p_sys->b_translate ? "Whisper translator ready - waiting for audio..." : "Whisper transcriber ready - waiting for audio...");
    } else {
        msg_Warn(p_filter, "No Whisper model path specified");
        strcpy(p_sys->current_text, "Please configure Whisper model path");
    }
    
    // Start processing thread
    p_sys->b_processing_active = true;
    if (vlc_clone(&p_sys->processing_thread, ProcessingThread, p_filter) != 0) {
        msg_Err(p_filter, "Failed to create processing thread");
        p_sys->b_processing_active = false;
    }
    
    msg_Info(p_filter, "Whisper live transcription filter created");
    
    
    // Set filter operations
    p_filter->ops = &filter_ops;
    
    return VLC_SUCCESS;
}

/*****************************************************************************
 * DestroyFilter: destroy whisper transcription filter
 *****************************************************************************/
static void DestroyFilter( filter_t *p_filter )
{
    struct filter_sys_t *p_sys = p_filter->p_sys;
    
    
    // Stop processing thread
    if (p_sys->b_processing_active) {
        p_sys->b_processing_active = false;
        vlc_join(p_sys->processing_thread, NULL);
    }
    
    // Clean up Whisper
    if (p_sys->whisper_ctx) {
        whisper_free(p_sys->whisper_ctx);
    }
    
    // Release shared buffer
    whisper_shared_release_buffer(VLC_OBJECT(p_filter), p_sys->shared_buffer);
    
    // Free resources
    free(p_sys->model_path);
    free(p_sys->language);
    text_style_Delete(p_sys->p_style);
    
    // Mutexes are automatically cleaned up
    
    free(p_sys);
}

/*****************************************************************************
 * Filter: output transcription subtitles
 *****************************************************************************/
static subpicture_t *Filter( filter_t *p_filter, vlc_tick_t date )
{
    struct filter_sys_t *p_sys = p_filter->p_sys;
    subpicture_t *p_spu = NULL;
    
    vlc_mutex_lock(&p_sys->text_mutex);
    
    // Check if we have text to display
    if (p_sys->current_text[0] == '\0') {
        vlc_mutex_unlock(&p_sys->text_mutex);
        return NULL;
    }
    
    // Don't update too frequently
    if (date - p_sys->last_update < VLC_TICK_FROM_MS(100)) {
        vlc_mutex_unlock(&p_sys->text_mutex);
        return NULL;
    }
    
    p_spu = filter_NewSubpicture(p_filter);
    if (!p_spu) {
        vlc_mutex_unlock(&p_sys->text_mutex);
        return NULL;
    }
    
    subpicture_region_t *p_region = subpicture_region_NewText();
    if (!p_region) {
        subpicture_Delete(p_spu);
        vlc_mutex_unlock(&p_sys->text_mutex);
        return NULL;
    }
    
    // Set text
    p_region->p_text = text_segment_New(p_sys->current_text);
    p_region->p_text->style = text_style_Duplicate(p_sys->p_style);
    
    vlc_mutex_unlock(&p_sys->text_mutex);
    
    // Position
    p_region->i_align = p_sys->i_pos;
    p_region->i_x = 0;
    p_region->i_y = 0;
    p_region->b_absolute = false;
    
    // Timing
    p_spu->i_start = date;
    p_spu->i_stop = date + VLC_TICK_FROM_SEC(5);
    p_spu->b_ephemer = true;
    
    vlc_spu_regions_push(&p_spu->regions, p_region);
    
    return p_spu;
}