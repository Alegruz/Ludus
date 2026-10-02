/*
 * Private native miniaudio implementation translation unit (compiled once as C).
 * Keeps the decoder (WAV/FLAC), the low-level resampler and native device I/O.
 * Linux backend selection (PulseAudio/ALSA) stays private to the device owner;
 * no PipeWire claim until a pinned backend is demonstrated (design section 2).
 */
#include "ludus_miniaudio_config.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
