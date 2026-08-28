#ifdef NATIVE_PORT
/* On PC, forward to the real system string.h */
#include_next <string.h>
#else
#ifndef _STRING_H_
#define _STRING_H_
#include <PR/ultratypes.h>

extern void *memcpy(void *, const void *, size_t);
extern unsigned char *strchr(const unsigned char *, int);
extern size_t strlen(const unsigned char *);

#endif
#endif /* !NATIVE_PORT */
