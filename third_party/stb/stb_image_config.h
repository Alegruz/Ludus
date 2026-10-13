/* Feature selection shared by the stb_image implementation and its callers. */
#ifndef LUDUS_STB_IMAGE_CONFIG_H
#define LUDUS_STB_IMAGE_CONFIG_H
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 16384
#endif
