/* Module-local allocation and deliberately closed desktop/POSIX compatibility
 * for TitanEYE.  The only real heap is TitanGameApiV1::alloc/release and every
 * file operation is routed separately through TGR storage. */
#include "titan_ge_runtime.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/reent.h>

/* Never expose host console/file objects to a game module. Legacy trace call
 * sites receive closed local sentinels and all writes below are no-ops. */
FILE *titan_ge_stdin = 0;
FILE *titan_ge_stdout = 0;
FILE *titan_ge_stderr = 0;

/* Some newlib headers expand errno/process state through _impure_ptr even in a
 * freestanding translation unit.  Keep that state module-local instead of
 * importing the shell/kernel copy.  TitanEYE does not expose newlib FILE
 * objects; stdin/stdout/stderr are redirected to the sentinels above. */
static struct _reent s_titan_ge_reent;
struct _reent *_impure_ptr = &s_titan_ge_reent;

#define TITAN_GE_ALLOC_MAGIC 0x54474541u

#define TITAN_GE_MALLOC_FAIL_TRACE_SLOTS 8u
static uint32_t s_titan_ge_malloc_fail_sizes[TITAN_GE_MALLOC_FAIL_TRACE_SLOTS];
static uint32_t s_titan_ge_malloc_fail_count;

static void TitanGeLibc_TraceMallocFailure(uint32_t request_bytes)
{
    uint32_t index;

    for (index = 0u; index < s_titan_ge_malloc_fail_count; index++)
        if (s_titan_ge_malloc_fail_sizes[index] == request_bytes)
            return;

    if (s_titan_ge_malloc_fail_count >= TITAN_GE_MALLOC_FAIL_TRACE_SLOTS)
        return;

    s_titan_ge_malloc_fail_sizes[s_titan_ge_malloc_fail_count++] = request_bytes;
    TitanGeRuntime_TraceHeap("E_MALLOC", request_bytes);
}

typedef struct TitanGeAllocationHeader
{
    uint32_t magic;
    uint32_t bytes;
    uint32_t reserved0;
    uint32_t reserved1;
} TitanGeAllocationHeader;

void *malloc(size_t bytes)
{
    const TitanGameApiV1 *api = TitanGeRuntime_GetApi();
    TitanGeAllocationHeader *header;
    if (api == 0 || bytes == 0u || bytes > 0xffffffffu - sizeof(*header))
        return 0;
    header = (TitanGeAllocationHeader *)api->alloc(
        (uint32_t)bytes + (uint32_t)sizeof(*header), 64u
    );
    if (header == 0)
    {
        TitanGeLibc_TraceMallocFailure(
            (uint32_t)bytes + (uint32_t)sizeof(*header)
        );
        return 0;
    }
    header->magic = TITAN_GE_ALLOC_MAGIC;
    header->bytes = (uint32_t)bytes;
    return header + 1;
}

void free(void *pointer)
{
    const TitanGameApiV1 *api;
    TitanGeAllocationHeader *header;
    if (pointer == 0) return;
    api = TitanGeRuntime_GetApi();
    header = ((TitanGeAllocationHeader *)pointer) - 1;
    if (api == 0 || header->magic != TITAN_GE_ALLOC_MAGIC) return;
    header->magic = 0u;
    api->release(header);
}

void *calloc(size_t count, size_t bytes)
{
    size_t total;
    void *pointer;
    if (count != 0u && bytes > (size_t)-1 / count) return 0;
    total = count * bytes;
    pointer = malloc(total);
    if (pointer != 0) memset(pointer, 0, total);
    return pointer;
}

void *realloc(void *pointer, size_t bytes)
{
    TitanGeAllocationHeader *header;
    void *replacement;
    size_t copy_bytes;
    if (pointer == 0) return malloc(bytes);
    if (bytes == 0u) { free(pointer); return 0; }
    header = ((TitanGeAllocationHeader *)pointer) - 1;
    if (header->magic != TITAN_GE_ALLOC_MAGIC) return 0;
    replacement = malloc(bytes);
    if (replacement == 0) return 0;
    copy_bytes = header->bytes < bytes ? header->bytes : bytes;
    memcpy(replacement, pointer, copy_bytes);
    free(pointer);
    return replacement;
}

char *strdup(const char *source)
{
    size_t bytes;
    char *copy;
    if (source == 0) return 0;
    bytes = strlen(source) + 1u;
    copy = (char *)malloc(bytes);
    if (copy != 0) memcpy(copy, source, bytes);
    return copy;
}

int strcasecmp(const char *left, const char *right)
{
    unsigned char a, b;
    do {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
    } while (a != 0u && a == b);
    return (int)a - (int)b;
}

int strncasecmp(const char *left, const char *right, size_t count)
{
    unsigned char a = 0u, b = 0u;
    while (count-- != 0u) {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a == 0u || a != b) break;
    }
    return (int)a - (int)b;
}

/* Console diagnostics must never smuggle a raw FILE/device capability into a
 * game module. Release builds keep these calls quiet; fatal boot errors surface
 * through the shell notification API in titan_ge_module.c. */
int printf(const char *format, ...) { (void)format; return 0; }
int fprintf(FILE *stream, const char *format, ...)
{ (void)stream; (void)format; return 0; }
int puts(const char *text) { (void)text; return 0; }
int fputs(const char *text, FILE *stream)
{ (void)text; (void)stream; return 0; }
int putchar(int character) { return character; }
int fflush(FILE *stream) { (void)stream; return 0; }
int fputc(int character, FILE *stream)
{ (void)stream; return character; }
int vfprintf(FILE *stream, const char *format, va_list args)
{ (void)stream; (void)format; (void)args; return 0; }
int setvbuf(FILE *stream, char *buffer, int mode, size_t bytes)
{ (void)stream; (void)buffer; (void)mode; (void)bytes; return 0; }

FILE *fopen(const char *path, const char *mode)
{ (void)path; (void)mode; return 0; }
int fclose(FILE *stream) { (void)stream; return -1; }
size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{ (void)buffer; (void)size; (void)count; (void)stream; return 0u; }
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{ (void)buffer; (void)size; (void)count; (void)stream; return 0u; }
int fseek(FILE *stream, long offset, int origin)
{ (void)stream; (void)offset; (void)origin; return -1; }
long ftell(FILE *stream) { (void)stream; return -1; }
void rewind(FILE *stream) { (void)stream; }
int ferror(FILE *stream) { (void)stream; return 1; }
int remove(const char *path) { (void)path; return -1; }
int rename(const char *source, const char *destination)
{ (void)source; (void)destination; return -1; }
int access(const char *path, int mode) { (void)path; (void)mode; return -1; }
int mkdir(const char *path, unsigned int mode)
{ (void)path; (void)mode; return -1; }

int atexit(void (*callback)(void)) { (void)callback; return 0; }
double atof(const char *text) { return strtod(text, 0); }

void bcopy(const void *source, void *destination, size_t bytes)
{ memmove(destination, source, bytes); }
void bzero(void *destination, size_t bytes)
{ memset(destination, 0, bytes); }

char *getenv(const char *name) { (void)name; return 0; }
int setenv(const char *name, const char *value, int overwrite)
{ (void)name; (void)value; (void)overwrite; return -1; }
int unsetenv(const char *name) { (void)name; return -1; }

/* Deterministic fallback for incidental port-only callers. GoldenEye gameplay
 * uses its own cartridge-faithful PRNG in random.c. */
static unsigned int s_titan_ge_rand = 1u;
void srand(unsigned int seed) { s_titan_ge_rand = seed != 0u ? seed : 1u; }
int rand(void)
{
    s_titan_ge_rand = s_titan_ge_rand * 1103515245u + 12345u;
    return (int)((s_titan_ge_rand >> 16u) & 0x7fffu);
}
