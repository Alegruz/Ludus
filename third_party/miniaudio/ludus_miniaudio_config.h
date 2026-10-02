/*
 * Ludus miniaudio build configuration, per .kiro/specs/audio/design.md
 * section 2 and the audio decision log A0 dependency lock.
 *
 * This header is included BEFORE miniaudio.h in the private C implementation
 * translation units. It selects the audited 0.11.23 option macros: keep the WAV
 * and FLAC decoders and the low-level resampler; disable MP3/Vorbis, encoding,
 * and the high-level engine / resource manager / node graph / waveform+noise
 * generation. Ludus owns voice management, mixing and streaming. The web unit
 * additionally disables device I/O and native threading.
 *
 * Every macro here is verified present in the pinned miniaudio.h (hash in
 * SOURCE-LOCK.json). This is build configuration only; it includes no Ludus
 * header and declares nothing.
 */
#ifndef LUDUS_MINIAUDIO_CONFIG_H
#define LUDUS_MINIAUDIO_CONFIG_H

/* Common: codecs and high-level features we never use. */
#define MA_NO_MP3
#define MA_NO_VORBIS
#define MA_NO_ENCODING
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION

#if defined(LUDUS_MINIAUDIO_WEB)
/* Web decode/resample unit: no device backends, no native threading. The
 * browser AudioWorklet adapter drives the renderer; the worker decodes. */
#define MA_NO_DEVICE_IO
#define MA_NO_THREADING
#endif

#endif /* LUDUS_MINIAUDIO_CONFIG_H */
