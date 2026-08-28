#ifndef MGB64_BSTRING_H
#define MGB64_BSTRING_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NATIVE_PORT
extern void bcopy(const void *src, void *dst, int len);
extern int bcmp(const void *lhs, const void *rhs, int len);
extern void bzero(void *dst, int len);
#endif

#ifdef __cplusplus
}
#endif

#endif
