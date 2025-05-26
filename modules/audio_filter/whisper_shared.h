/*****************************************************************************
 * whisper_shared.h : Shared definitions for Whisper audio capture
 *****************************************************************************/

#ifndef WHISPER_SHARED_H
#define WHISPER_SHARED_H

#include <stddef.h>

#define WHISPER_SAMPLE_RATE 16000
#define WHISPER_BUFFER_SIZE (WHISPER_SAMPLE_RATE * 10) // 10 seconds

// Initialize shared audio buffer
int whisper_shared_init(void);

// Cleanup shared audio buffer
void whisper_shared_cleanup(void);

// Write samples to shared buffer
int whisper_shared_write(const float *samples, size_t count);

// Read samples from shared buffer
int whisper_shared_read(float *samples, size_t count);

// Get available samples
size_t whisper_shared_available(void);

#endif // WHISPER_SHARED_H