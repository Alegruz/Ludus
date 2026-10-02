/*
 * Private web miniaudio implementation translation unit (compiled once as C for
 * the Emscripten target). Keeps the decoder (WAV/FLAC) and the low-level
 * resampler; disables device I/O and native threading. The browser output path
 * is the Wasm AudioWorklet adapter, not a miniaudio device (design section 2/10).
 */
#define LUDUS_MINIAUDIO_WEB
#include "ludus_miniaudio_config.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
