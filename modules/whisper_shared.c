/*****************************************************************************
 * whisper_shared.c : Shared memory implementation for Whisper audio
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>

#define WHISPER_SAMPLE_RATE 16000
#define WHISPER_BUFFER_SIZE (WHISPER_SAMPLE_RATE * 10) // 10 seconds

// Global shared buffer - single instance
static float *g_audio_samples = NULL;
static atomic_size_t g_write_pos = ATOMIC_VAR_INIT(0);
static atomic_size_t g_read_pos = ATOMIC_VAR_INIT(0);
static atomic_bool g_active = ATOMIC_VAR_INIT(false);
static atomic_int g_init_count = ATOMIC_VAR_INIT(0);

int whisper_shared_init(void)
{
    int count = atomic_fetch_add(&g_init_count, 1);
    
    if (count == 0) {
        // First initialization
        g_audio_samples = malloc(WHISPER_BUFFER_SIZE * sizeof(float));
        if (!g_audio_samples) {
            atomic_fetch_sub(&g_init_count, 1);
            return -1;
        }
        memset(g_audio_samples, 0, WHISPER_BUFFER_SIZE * sizeof(float));
        atomic_store(&g_write_pos, 0);
        atomic_store(&g_read_pos, 0);
    }
    
    atomic_store(&g_active, true);
    return 0;
}

void whisper_shared_cleanup(void)
{
    int count = atomic_fetch_sub(&g_init_count, 1);
    
    if (count == 1) {
        // Last cleanup
        atomic_store(&g_active, false);
        free(g_audio_samples);
        g_audio_samples = NULL;
    }
}

int whisper_shared_write(const float *samples, size_t count)
{
    if (!atomic_load(&g_active) || !g_audio_samples)
        return -1;
        
    size_t write_pos = atomic_load(&g_write_pos);
    
    for (size_t i = 0; i < count; i++) {
        g_audio_samples[write_pos] = samples[i];
        write_pos = (write_pos + 1) % WHISPER_BUFFER_SIZE;
    }
    
    atomic_store(&g_write_pos, write_pos);
    return 0;
}

int whisper_shared_read(float *samples, size_t count)
{
    if (!atomic_load(&g_active) || !g_audio_samples)
        return -1;
        
    size_t write_pos = atomic_load(&g_write_pos);
    size_t read_pos = atomic_load(&g_read_pos);
    
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
        samples[i] = g_audio_samples[(read_pos + i) % WHISPER_BUFFER_SIZE];
    }
    
    atomic_store(&g_read_pos, (read_pos + count) % WHISPER_BUFFER_SIZE);
    return 0;
}

size_t whisper_shared_available(void)
{
    if (!atomic_load(&g_active) || !g_audio_samples)
        return 0;
        
    size_t write_pos = atomic_load(&g_write_pos);
    size_t read_pos = atomic_load(&g_read_pos);
    
    if (write_pos >= read_pos) {
        return write_pos - read_pos;
    } else {
        return WHISPER_BUFFER_SIZE - read_pos + write_pos;
    }
}