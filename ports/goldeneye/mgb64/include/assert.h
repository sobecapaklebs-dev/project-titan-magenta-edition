#ifndef MGB64_ASSERT_H
#define MGB64_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG
#undef assert
#define assert(EX) ((void)0)
#else
#ifdef NATIVE_PORT
extern int printf(const char *fmt, ...);
#define assert(EX) \
    ((EX) ? (void)0 : (void)printf("\n--- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, __FILE__, __LINE__))
#define assertmsg(EX, MSG) ((EX) ? (void)0 : (void)printf("%s", (MSG)))
#else
extern void osSyncPrintf(const char *fmt, ...);
#define assert(EX) \
    ((EX) ? (void)0 : (void)osSyncPrintf("\n--- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, __FILE__, __LINE__))
#define assertmsg(EX, MSG) ((EX) ? (void)0 : (void)osSyncPrintf("%s", (MSG)))
#endif
#endif

#ifndef assertmsg
#define assertmsg(EX, MSG) assert(EX)
#endif

#ifdef __cplusplus
}
#endif

#endif
