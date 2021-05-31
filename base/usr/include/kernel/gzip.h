/**
 * @brief Kernel gzip decompressor
 *
 * This is a slimmed down version of libtoaru_inflate; it's an implementation
 * of the tinf algorithm for decompressing gzip/DEFLATE payloads, with a
 * very straightforward API: Point @c gzip_inputPtr at your gzip data,
 * point @c gzip_outputPtr where you want the output to go, and then
 * run @c gzip_decompress().
 */
#pragma once

#include <stdint.h>

extern int gzip_decompress(void);
extern uint8_t * gzip_inputPtr;
extern uint8_t * gzip_outputPtr;
