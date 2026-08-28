/*
 * Clean-room matching-target audio compatibility surface.
 *
 * The native port and the in-progress N64 matching target use the same
 * project-owned audio compatibility implementation. Keeping this wrapper in the
 * libultra tree lets the matching makefile build one clean implementation unit
 * instead of compiling the historical SDK/libultra audio source files.
 */

#include "../../platform/audio_compat.c"
