#ifndef TITAN_GE_STDIO_COMPAT_H
#define TITAN_GE_STDIO_COMPAT_H

/*
 * PS2SDK/newlib exposes stdin/stdout/stderr (and ferror on some revisions)
 * as macros backed by the newlib reentrancy object. TitanEYE is a freestanding
 * Titan module and must not import that process-global libc state.
 *
 * Force this header before every translation unit: it loads the real FILE
 * declarations once, then redirects the three stream macros to module-local
 * sentinels implemented by titan_ge_libc.c. ferror is intentionally left as a
 * normal function so the closed TitanEYE implementation can satisfy callers.
 */
#include <stdio.h>

#ifdef stdin
#undef stdin
#endif
#ifdef stdout
#undef stdout
#endif
#ifdef stderr
#undef stderr
#endif
#ifdef ferror
#undef ferror
#endif

extern FILE *titan_ge_stdin;
extern FILE *titan_ge_stdout;
extern FILE *titan_ge_stderr;

#define stdin  titan_ge_stdin
#define stdout titan_ge_stdout
#define stderr titan_ge_stderr

#endif /* TITAN_GE_STDIO_COMPAT_H */
