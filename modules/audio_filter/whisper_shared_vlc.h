/*****************************************************************************
 * whisper_shared_vlc.h : Shared audio buffer using VLC variables
 *****************************************************************************/

#ifndef WHISPER_SHARED_VLC_H
#define WHISPER_SHARED_VLC_H

#include <stddef.h>
#include <stdatomic.h>
#include <vlc_common.h>
#include <vlc_threads.h>

#define WHISPER_SAMPLE_RATE 16000
#define WHISPER_BUFFER_SIZE (WHISPER_SAMPLE_RATE * 10) // 10 seconds

// Shared buffer structure
typedef struct {
    float *samples;
    atomic_size_t write_pos;
    atomic_size_t read_pos;
    atomic_bool active;
    atomic_int ref_count;
} whisper_shared_buffer_t;

#endif /* WHISPER_SHARED_VLC_H */