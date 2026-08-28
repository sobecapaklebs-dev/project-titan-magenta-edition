#ifndef _VIINT_H_
#define _VIINT_H_
#    include <os_internal.h>

/*
 * Clean-room internal VI declarations and helper macros retained for the
 * matching-target libultra VI sources.
 */

#ifdef _LANGUAGE_C_PLUS_PLUS
extern "C"
{
#endif

#if 1

#    define VI_FIELD1 0
#    define VI_FIELD2 1

#    define VI_STATE_NORMAL     0x00
#    define VI_STATE_MODE       0x01
#    define VI_STATE_X_SCALE    0x02
#    define VI_STATE_Y_SCALE    0x04
#    define VI_STATE_CONTROL    0x08
#    define VI_STATE_FRAME      0x10
#    define VI_STATE_BLACK      0x20
#    define VI_STATE_REPEATLINE 0x40
#    define VI_STATE_FADE       0x80

    typedef struct
    {
        f32 factor;
        u16 offset;
        u32 scale;
    } __OSViScale;

    typedef struct
    {
        u16 state;
        u16 retraceCount;
        void *framep;
        OSViMode *modep;
        u32 control;
        OSMesgQueue *msgq;
        OSMesg msg;
        __OSViScale x;
        __OSViScale y;
    } __OSViContext;

    extern OSDevMgr __osViDevMgr;
    extern __OSViContext *__osViCurr;
    extern __OSViContext *__osViNext;

    extern void __osViCreateAccessQueue(void);
    extern OSMesgQueue *__osViGetAccessQueue(void);
    extern void __osViGetAccess(void);
    extern void __osViRelAccess(void);

    extern void __osViInit(void);
    extern __OSViContext *__osViGetCurrentContext(void);
    extern __OSViContext *__osViGetNextContext(void);
    extern void __osViSwapContext(void);

#endif

#ifdef _LANGUAGE_C_PLUS_PLUS
}
#endif

#define OS_TV_TYPE_PAL 0
#define OS_TV_TYPE_NTSC 1
#define OS_TV_TYPE_MPAL 2

#define VI_CTRL_ANTIALIAS_MODE_3 0x00300
#define VI_CTRL_ANTIALIAS_MODE_2 0x00200
#define VI_CTRL_ANTIALIAS_MODE_1 0x00100

#define VI_SCALE_MASK 0xfff
#define VI_2_10_FPART_MASK 0x3ff
#define VI_SUBPIXEL_SH 0x10

#define BURST(hsync_width, color_width, vsync_width, color_start) \
    (hsync_width | (color_width << 8) | (vsync_width << 16) | (color_start << 20))
#define WIDTH(v) v
#define VSYNC(v) v
#define HSYNC(duration, leap) (duration | (leap << 16))
#define LEAP(upper, lower) ((upper << 16) | lower)
#define START(start, end) ((start << 16) | end)

#define FTOFIX(val, i, f) ((u32)(val * (f32)(1 << f)) & ((1 << (i + f)) - 1))

#define F210(val) FTOFIX(val, 2, 10)
#define SCALE(scaleup, off) (F210((1.0f / (f32)scaleup)) | (F210((f32)off) << 16))

#define VCURRENT(v) v
#define ORIGIN(v) v
#define VINTR(v) v
#define HSTART START

#endif
