/**
 * platform_os.h — Stub/replacement declarations for libultra OS functions.
 *
 * Provides all types, structs, and function declarations that game code
 * expects from <PR/os.h> and related N64 SDK headers.
 */
#ifndef _PLATFORM_OS_H_
#define _PLATFORM_OS_H_

#include <PR/ultratypes.h>

/* ===== Constants ===== */

#define OS_MESG_NOBLOCK     0
#define OS_MESG_BLOCK       1
#define OS_MESG_PRI_NORMAL  0
#define OS_MESG_PRI_HIGH    1

/* RSP ucode constants (from PR/ucode.h) */
#define SP_DRAM_STACK_SIZE8 0x400
#define SP_UCODE_SIZE       0x1000
#define SP_UCODE_DATA_SIZE  0x800

/* PI status / control registers */
#define PI_STATUS_DMA_BUSY  0x01
#define PI_SET_RESET        0x01

/* TV types */
#define OS_TV_PAL           0
#define OS_TV_NTSC          1
#define OS_TV_MPAL          2
#define TV_TYPE_PAL         OS_TV_PAL
#define TV_TYPE_NTSC        OS_TV_NTSC
#define TV_TYPE_MPAL        OS_TV_MPAL
#define OS_PRIORITY_VIMGR   254

#define OS_STATE_STOPPED    1
#define OS_STATE_RUNNABLE   2
#define OS_STATE_RUNNING    4
#define OS_STATE_WAITING    8

#define OS_PRIORITY_IDLE    0
#define OS_PRIORITY_APPMAX  127
#define OS_PRIORITY_SIMGR   140
#define OS_PRIORITY_PIMGR   150
#define OS_PRIORITY_RMONSPIN 200
#define OS_PRIORITY_RMON    250

#define OS_VI_NTSC_LPN1     0
#define OS_VI_NTSC_LPF1     1
#define OS_VI_NTSC_LAN1     2
#define OS_VI_NTSC_LAF1     3
#define OS_VI_NTSC_LPN2     4
#define OS_VI_NTSC_LPF2     5
#define OS_VI_NTSC_LAN2     6
#define OS_VI_NTSC_LAF2     7
#define OS_VI_NTSC_HPN1     8
#define OS_VI_NTSC_HPF1     9
#define OS_VI_NTSC_HAN1     10
#define OS_VI_NTSC_HAF1     11
#define OS_VI_NTSC_HPN2     12
#define OS_VI_NTSC_HPF2     13

#define OS_VI_PAL_LPN1      14
#define OS_VI_PAL_LPF1      15
#define OS_VI_PAL_LAN1      16
#define OS_VI_PAL_LAF1      17
#define OS_VI_PAL_LPN2      18
#define OS_VI_PAL_LPF2      19
#define OS_VI_PAL_LAN2      20
#define OS_VI_PAL_LAF2      21
#define OS_VI_PAL_HPN1      22
#define OS_VI_PAL_HPF1      23
#define OS_VI_PAL_HAN1      24
#define OS_VI_PAL_HAF1      25
#define OS_VI_PAL_HPN2      26
#define OS_VI_PAL_HPF2      27

#define OS_VI_MPAL_LPN1     28
#define OS_VI_MPAL_LPF1     29
#define OS_VI_MPAL_LAN1     30
#define OS_VI_MPAL_LAF1     31
#define OS_VI_MPAL_LPN2     32
#define OS_VI_MPAL_LPF2     33
#define OS_VI_MPAL_LAN2     34
#define OS_VI_MPAL_LAF2     35
#define OS_VI_MPAL_HPN1     36
#define OS_VI_MPAL_HPF1     37
#define OS_VI_MPAL_HAN1     38
#define OS_VI_MPAL_HAF1     39
#define OS_VI_MPAL_HPN2     40
#define OS_VI_MPAL_HPF2     41

/* VI special features */
#define OS_VI_GAMMA_ON          0x0001
#define OS_VI_GAMMA_OFF         0x0002
#define OS_VI_GAMMA_DITHER_ON   0x0004
#define OS_VI_GAMMA_DITHER_OFF  0x0008
#define OS_VI_DIVOT_ON          0x0010
#define OS_VI_DIVOT_OFF         0x0020
#define OS_VI_DITHER_FILTER_ON  0x0040
#define OS_VI_DITHER_FILTER_OFF 0x0080

/* VI mode attribute bits */
#define OS_VI_BIT_NONINTERLACE      0x0001
#define OS_VI_BIT_INTERLACE         0x0002
#define OS_VI_BIT_NORMALINTERLACE   0x0004
#define OS_VI_BIT_DEFLICKINTERLACE  0x0008
#define OS_VI_BIT_ANTIALIAS         0x0010
#define OS_VI_BIT_POINTSAMPLE       0x0020
#define OS_VI_BIT_16PIXEL           0x0040
#define OS_VI_BIT_32PIXEL           0x0080

#define OS_EVENT_SW1        0
#define OS_EVENT_SW2        1
#define OS_EVENT_CART       2
#define OS_EVENT_COUNTER    3
#define OS_EVENT_SP         4
#define OS_EVENT_SI         5
#define OS_EVENT_AI         6
#define OS_EVENT_VI         7
#define OS_EVENT_PI         8
#define OS_EVENT_DP         9
#define OS_EVENT_CPU_BREAK  10
#define OS_EVENT_SP_BREAK   11
#define OS_EVENT_FAULT      12
#define OS_EVENT_THREADSTATUS 13
#define OS_EVENT_PRENMI     14

#define OS_FLAG_CPU_BREAK   1
#define OS_FLAG_FAULT       2

#define OS_IM_NONE          0x00000001
#define OS_IM_SW1           0x00000501
#define OS_IM_SW2           0x00000601
#define OS_IM_CART          0x00000C01
#define OS_IM_PRENMI        0x00004C01
#define OS_IM_RDBWRITE      0x00008C01
#define OS_IM_RDBREAD       0x00010C01
#define OS_IM_COUNTER       0x00020C01
#define OS_IM_CPU           0x0003FF01
#define OS_IM_SP            0x00040001
#define OS_IM_SI            0x00080001
#define OS_IM_AI            0x00100001
#define OS_IM_VI            0x00200001
#define OS_IM_PI            0x00400001
#define OS_IM_DP            0x00800001
#define OS_IM_ALL           0x00FF0001
#define RCP_IMASK           0x003F0000
#define RCP_IMASKSHIFT      16

/* Scheduler flags */
#define OS_SC_NEEDS_RDP         0x0001
#define OS_SC_NEEDS_RSP         0x0002
#define OS_SC_DRAM_DLIST        0x0004
#define OS_SC_PARALLEL_TASK     0x0010
#define OS_SC_LAST_TASK         0x0020
#define OS_SC_SWAPBUFFER        0x0040

/* PI/DMA */
#define OS_READ     0
#define OS_WRITE    1

#define PI_STATUS_REG       0x04600010
#define PI_CLR_INTR         0x02
#define PI_DOM1_ADDR2       0x10000000

#define DEVICE_TYPE_CART    0
#define DEVICE_TYPE_64DD    1

/* Controller */
#define CONT_ABSOLUTE       0x0001
#define CONT_RELATIVE       0x0002
#define CONT_JOYPORT        0x0004
#define CONT_EEPROM         0x8000
#define CONT_CARD_ON        0x0001
#define CONT_CARD_PULL      0x0004
#define CONT_ADDR_CRC_ER    0x0004
#define CONT_TYPE_NORMAL    0x0005
#define CONT_TYPE_MOUSE     0x0002
#define CONT_TYPE_VOICE     0x0000

#define CONT_A      0x8000
#define CONT_B      0x4000
#define CONT_G      0x2000
#define CONT_START  0x1000
#define CONT_UP     0x0800
#define CONT_DOWN   0x0400
#define CONT_LEFT   0x0200
#define CONT_RIGHT  0x0100
#define CONT_L      0x0020
#define CONT_R      0x0010
#define CONT_E      0x0008
#define CONT_D      0x0004
#define CONT_C      0x0002
#define CONT_F      0x0001

/* Button aliases (from os.h) */
#define A_BUTTON    CONT_A
#define B_BUTTON    CONT_B
#define L_TRIG      CONT_L
#define R_TRIG      CONT_R
#define Z_TRIG      CONT_G
#define START_BUTTON CONT_START
#define U_JPAD      CONT_UP
#define L_JPAD      CONT_LEFT
#define R_JPAD      CONT_RIGHT
#define D_JPAD      CONT_DOWN
#define U_CBUTTONS  CONT_E
#define L_CBUTTONS  CONT_C
#define R_CBUTTONS  CONT_F
#define D_CBUTTONS  CONT_D

/* Rumble pak */
#define CONT_ERR_NO_CONTROLLER  PFS_ERR_NOPACK
#define CONT_ERR_CONTRFAIL      PFS_ERR_CONTRFAIL
#define PFS_ERR_NOPACK          1
#define PFS_ERR_NEW_PACK        2
#define PFS_ERR_CONTRFAIL       4
#define PFS_ERR_DEVICE          11
#define PFS_ERR_INCONSISTENT    8
#define PFS_ERR_ID_FATAL        -1
#define CONT_NO_RESPONSE_ERROR  8
#define CONT_OVERRUN_ERROR      4

#define CONT_CARD_PULL          0x0004
#define MAXCONTROLLERS          4

/* EEPROM */
#define EEPROM_MAXBLOCKS        64
#define EEPROM_BLOCK_SIZE       8
#define EEPROM_TYPE_4K          0x01
#define EEPROM_TYPE_16K         0x02

/* ===== SP task constants (from sptask.h) ===== */
#define M_GFXTASK       1
#define M_AUDTASK       2
#define M_VIDTASK       3

#define OS_YIELD_DATA_SIZE      0xc00
#define OS_YIELD_AUDIO_SIZE     0x400
#define OS_YIELD_GFX_DATA_SIZE  0xBA0

#define OS_TASK_YIELDED     0x0001
#define OS_TASK_DP_WAIT     0x0002
#define OS_TASK_LOADABLE    0x0004
#define OS_TASK_SP_ONLY     0x0008

/* SP status bits */
#define SPSTATUS_CLEAR_HALT         0x00000001
#define SPSTATUS_SET_HALT           0x00000002
#define SPSTATUS_CLEAR_BROKE        0x00000004
#define SPSTATUS_CLEAR_INTR         0x00000008
#define SPSTATUS_SET_INTR           0x00000010
#define SPSTATUS_HALT               0x0001
#define SPSTATUS_BROKE              0x0002
#define SPSTATUS_DMA_BUSY           0x0004
#define SPSTATUS_DMA_FULL           0x0008
#define SPSTATUS_IO_FULL            0x0010
#define SPSTATUS_SINGLE_STEP        0x0020
#define SPSTATUS_INTERRUPT_ON_BREAK 0x0040
#define SPSTATUS_SIGNAL0_SET        0x0080
#define SPSTATUS_SIGNAL1_SET        0x0100

/* DP status bits */
#define DPC_CLR_XBUS_DMEM_DMA  0x0001
#define DPC_SET_XBUS_DMEM_DMA  0x0002
#define DPC_CLR_FREEZE         0x0004
#define DPC_SET_FREEZE         0x0008
#define DPC_CLR_FLUSH          0x0010
#define DPC_SET_FLUSH          0x0020
#define DPC_CLR_TMEM_CTR       0x0040
#define DPC_CLR_PIPE_CTR       0x0080
#define DPC_CLR_CMD_CTR        0x0100
#define DPC_CLR_CLOCK_CTR      0x0200
#define DPC_STATUS_XBUS_DMEM_DMA   0x0001
#define DPC_STATUS_FREEZE          0x0002
#define DPC_STATUS_FLUSH           0x0004
#define DPC_STATUS_START_GCLK      0x0008
#define DPC_STATUS_TMEM_BUSY       0x0010
#define DPC_STATUS_PIPE_BUSY       0x0020
#define DPC_STATUS_CMD_BUSY        0x0040
#define DPC_STATUS_CBUF_READY      0x0080
#define DPC_STATUS_DMA_BUSY        0x0100
#define DPC_STATUS_END_VALID       0x0200
#define DPC_STATUS_START_VALID     0x0400

/* ===== Struct definitions ===== */

typedef s32 OSPri;
typedef s32 OSId;
typedef void *OSMesg;
typedef u32 OSEvent;
typedef u32 OSIntMask;
typedef u32 OSPageMask;
typedef u32 OSHWIntr;

/* Thread context (simplified for PC) */
typedef struct {
    u64 gp_regs[32];
    u64 fp_regs[32];
    u64 sr, pc, cause;
    u32 fpcsr;
    u32 rcp;
} __OSThreadContext;

/* Thread */
typedef struct OSThread_s {
    struct OSThread_s *next;
    OSPri priority;
    OSId id;
    s16 state;
    s16 flags;
    __OSThreadContext context;
    void *fp;
    void *queue;
} OSThread;

/* Message queue */
typedef struct {
    OSThread *mtqueue;
    OSThread *fullqueue;
    s32 validCount;
    s32 first;
    s32 msgCount;
    OSMesg *msg;
} OSMesgQueue;

/* Timer */
typedef struct OSTimer_s {
    struct OSTimer_s *next;
    struct OSTimer_s *prev;
    u64 interval;
    u64 value;
    OSMesgQueue *mq;
    OSMesg msg;
} OSTimer;

/* Video mode */
typedef struct {
    u8 type;
    struct {
        u32 ctrl;
        u32 width;
        u32 burst;
        u32 vSync;
        u32 hSync;
        u32 leap;
        u32 hStart;
        u32 xScale;
        u32 vCurrent;
    } comRegs;
    struct {
        u32 origin;
        u32 yScale;
        u32 vStart;
        u32 vBurst;
        u32 vIntr;
    } fldRegs[2];
} OSViMode;

/* Controller */
typedef struct {
    u16 type;
    u8 status;
    u8 errnum; /* named `errno` in the real libultra ABI; renamed here because
                * errno is a libc macro (<errno.h>) and collides with it on
                * some toolchains (e.g. MinGW) when both are visible in a TU */
} OSContStatus;

typedef struct {
    u16 button;
    s8 stick_x;
    s8 stick_y;
    u8 errnum; /* see OSContStatus.errnum rename note above */
} OSContPad;

/* Filesystem / Controller Pak */
typedef struct {
    s32 status;
    OSMesgQueue *queue;
    s32 channel;
    u8 id[32];
    u8 label[32];
    s32 version;
    s32 dir_size;
    s32 inode_table;
    s32 minode_table;
    s32 dir_table;
    s32 inode_start_page;
    u8 banks;
    u8 activebank;
} OSPfs;

typedef struct {
    u32 file_size;
    u32 game_code;
    u16 company_code;
    char ext[4];
    char game_name[16];
} OSPfsState;

/* PI (Peripheral Interface) */
typedef struct {
    u32 errStatus;
    void *dramAddr;
    u32 devAddr;
    u32 size;
    u32 type;
    u8 pri;
} OSIoMesg;

typedef struct {
    u32 type;
    u32 address;
} OSPiHandle;

/* SP Task */
typedef struct {
    u32 type;
    u32 flags;
    u64 *ucode_boot;
    u32 ucode_boot_size;
    u64 *ucode;
    u32 ucode_size;
    u64 *ucode_data;
    u32 ucode_data_size;
    u64 *dram_stack;
    u32 dram_stack_size;
    u64 *output_buff;
    u64 *output_buff_size;
    u64 *data_ptr;
    u32 data_size;
    u64 *yield_data_ptr;
    u32 yield_data_size;
} OSTask_t;

typedef union {
    OSTask_t t;
    long long int force_structure_alignment;
} OSTask;

typedef u32 OSYieldResult;

/* Scheduler task */
typedef struct OSScMsg_s {
    s16 type;
    char misc[30];
} OSScMsg;

typedef struct OSScTask_s {
    struct OSScTask_s *next;
    u32 state;
    u32 flags;
    void *framebuffer;
    OSTask list;
    OSMesgQueue *msgQ;
    OSMesg msg;
} OSScTask;

/* Scheduler client */
typedef struct OSScClient_s {
    struct OSScClient_s *next;
    OSMesgQueue *msgQ;
} OSScClient;

/* ===== GU types (from gu.h) ===== */

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define M_DTOR (3.14159265358979323846/180.0)

#define FTOFIX32(x)   (long)((x) * (float)0x00010000)
#define FIX32TOF(x)   ((float)(x) / (float)0x00010000)
#define FTOFRAC8(x)   ((int) MIN(((x) * (128.0)), 127.0) & 0xff)

#define FILTER_WRAP  0
#define FILTER_CLAMP 1

typedef struct {
    unsigned char *base;
    int fmt, siz;
    int xsize, ysize;
    int lsize;
    int addr;
    int w, h;
    int s, t;
} Image;

typedef struct {
    float col[3];
    float pos[3];
    float a1, a2;
} PositionalLight;

/* ===== OS function stubs ===== */

/* Virtual-to-physical: identity on PC, but also store in pointer table
 * for the GBI translator to resolve 64-bit pointers from truncated 32-bit values */
#include "gfx_ptr.h"
static inline uintptr_t osVirtualToPhysical_pc(void *addr) {
    uintptr_t phys = (uintptr_t)addr;

    if (phys > 0x00FFFFFFu) {
        gfx_ptr_store(addr);
    }
    return phys;
}
#define osVirtualToPhysical(addr)   osVirtualToPhysical_pc((void*)(addr))
#define osPhysicalToVirtual(addr)   ((void *)(uintptr_t)(addr))

/* Printf — declared as function, defined in rmon.c */
void osSyncPrintf(const char *fmt, ...);

/* Thread management — no-ops */
void osCreateThread(OSThread *thread, OSId id, void (*entry)(void *),
                    void *arg, void *sp, OSPri pri);
void osStartThread(OSThread *thread);
void osStopThread(OSThread *thread);
void osDestroyThread(OSThread *thread);
void osSetThreadPri(OSThread *thread, OSPri pri);
OSPri osGetThreadPri(OSThread *thread);
OSId osGetThreadId(OSThread *thread);

/* Message queue */
void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count);
s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flags);
s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flags);
s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flags);

/* Event */
void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg);

/* Interrupt */
OSIntMask osGetIntMask(void);
OSIntMask osSetIntMask(OSIntMask mask);

/* Timer */
u32 osGetCount(void);
void osSetTime(u64 time);
u64 osGetTime(void);
void osSetTimer(OSTimer *timer, u64 countdown, u64 interval,
                OSMesgQueue *mq, OSMesg msg);
void osStopTimer(OSTimer *timer);

/* VI (Video Interface) */
void osViSetMode(OSViMode *mode);
void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount);
void osViSwapBuffer(void *framebuffer);
void osViBlack(u32 active);
void osViSetSpecialFeatures(u32 features);
void *osViGetNextFramebuffer(void);
void *osViGetCurrentFramebuffer(void);
u32 osViGetStatus(void);
u32 osViGetCurrentLine(void);
void osViSetXScale(f32 scale);
void osViSetYScale(f32 scale);
void osViRepeatLine(u32 active);
void osViExtendVStart(u32 extend);
void osViRepeatLine(u32 active);
void osViSetXScale(f32 value);
void osViSetYScale(f32 value);
void osViSetXScale(f32 value);
void osViSetYScale(f32 value);

extern OSViMode osViModeTable[];
extern OSViMode osViModeNtscLpn1;
extern OSViMode osViModeNtscLpn2;
extern OSViMode osViModePalLpn1;
extern OSViMode osViModePalLpn2;
extern OSViMode osViModeMpalLpn1;

/* PI (Peripheral Interface — ROM/cart DMA) */
OSPiHandle *osCartRomInit(void);
void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdBufSize);
s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 direction, u32 devAddr,
                 void *dramAddr, u32 size, OSMesgQueue *mq);
s32 osPiRawReadIo(u32 devAddr, u32 *data);
s32 osPiRawWriteIo(u32 devAddr, u32 data);
u32 osPiGetStatus(void);

/* EPI (External PI) */
s32 osEPiStartDma(OSPiHandle *handle, OSIoMesg *mb, s32 direction);
s32 osEPiReadIo(OSPiHandle *handle, u32 devAddr, u32 *data);

/* AI (Audio Interface) */
u32 osAiGetStatus(void);
s32 osAiSetFrequency(u32 freq);
s32 osAiSetNextBuffer(void *buf, u32 size);
u32 osAiGetLength(void);

/* DP (Display Processor) */
u32 osDpGetStatus(void);
void osDpSetStatus(u32 status);
void osDpGetCounters(u32 *counters);
s32 osDpSetNextBuffer(void *buf, u64 size);

/* SP (Signal Processor) */
u32 __osSpGetStatus(void);
void __osSpSetStatus(u32 status);
void osSpTaskLoad(OSTask *task);
void osSpTaskStartGo(OSTask *task);
void osSpTaskYield(void);
OSYieldResult osSpTaskYielded(OSTask *task);
#define osSpTaskStart(p) do { osSpTaskLoad(p); osSpTaskStartGo(p); } while(0)

/* Controller */
s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *data);
s32 osContReset(OSMesgQueue *mq, OSContStatus *data);
s32 osContStartQuery(OSMesgQueue *mq);
s32 osContStartReadData(OSMesgQueue *mq);
s32 osContGetQuery(OSContStatus *data);
s32 osContGetReadData(OSContPad *data);
void osContSetCh(u8 ch);

/* Native multi-controller accessors (defined in platform_sdl.c).
 * Slot index k is the player number (0..MAXCONTROLLERS-1); accessors
 * bounds-check k and return neutral values for an absent pad. Slot 0 is the
 * keyboard/mouse player and the first opened pad. */
int platformGetPadCount(void);
unsigned int platformGetPadButtons(int k);
void platformGetPadLeftStick(int k, int *lx_out, int *ly_out);
void platformGetPadRightStick(int k, int *rx_out, int *ry_out);
void platformGetPadTriggers(int k, int *leftTrig, int *rightTrig);

/* Shared radial (circular) deadzone + rescale-from-edge for an analog stick.
 * Operates in place on a normalized stick vector (*nx,*ny in [-1,1]); direction
 * is preserved. deadzone is the inner dead fraction of full deflection. When
 * radial_enabled is 0 this is a no-op (the caller keeps its legacy square map).
 * Below the deadzone -> (0,0); above -> magnitude rescaled so output is
 * continuous from 0 at the edge. Used by both the aim stick (game/lvl.c) and
 * the movement stick (platform/stubs.c) so they share one behavior. */
void platformApplyRadialDeadzone(float *nx, float *ny, float deadzone, int radial_enabled);

/* Rumble Pak */
s32 osMotorInit(OSMesgQueue *mq, OSPfs *pfs, s32 channel);
s32 osMotorStart(OSPfs *pfs);
s32 osMotorStop(OSPfs *pfs);

/* EEPROM */
s32 osEepromProbe(OSMesgQueue *mq);
s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, s32 nbytes);
s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, s32 nbytes);
s32 osEepromRead(OSMesgQueue *mq, u8 address, u8 *buffer);
s32 osEepromWrite(OSMesgQueue *mq, u8 address, u8 *buffer);

/* Controller Pak */
s32 osPfsInitPak(OSMesgQueue *mq, OSPfs *pfs, s32 channel);
s32 osPfsRepairId(OSPfs *pfs);
s32 osPfsInit(OSMesgQueue *mq, OSPfs *pfs, s32 channel);
s32 osPfsIsPlug(OSMesgQueue *mq, u8 *bitpattern);
s32 osPfsAllocateFile(OSPfs *pfs, u16 company_code, u32 game_code,
                      u8 *game_name, u8 *ext_name, s32 file_size_in_bytes, s32 *file_no);
s32 osPfsFindFile(OSPfs *pfs, u16 company_code, u32 game_code,
                  u8 *game_name, u8 *ext_name, s32 *file_no);
s32 osPfsDeleteFile(OSPfs *pfs, u16 company_code, u32 game_code,
                    u8 *game_name, u8 *ext_name);
s32 osPfsReadWriteFile(OSPfs *pfs, s32 file_no, u8 flag, s32 offset,
                       s32 nbytes, u8 *data_buffer);
s32 osPfsFileState(OSPfs *pfs, s32 file_no, OSPfsState *state);
s32 osPfsFreeBlocks(OSPfs *pfs, s32 *bytes_not_used);
s32 osPfsNumFiles(OSPfs *pfs, s32 *max_files, s32 *files_used);

/* Cache operations — no-ops on PC */
#define osInvalDCache(addr, size)       ((void)0)
#define osInvalICache(addr, size)       ((void)0)
#define osWritebackDCache(addr, size)   ((void)0)
#define osWritebackDCacheAll()          ((void)0)

/* TLB — not needed on PC */
#define osMapTLB(index, pm, vaddr, even, odd, mask) ((void)0)
#define osUnmapTLB(index)                           ((void)0)
#define osUnmapTLBAll()                             ((void)0)
#define osSetTLBASID(asid)                          ((void)0)

/* VI Manager */
void osCreateViManager(OSPri pri);

/* Raw PI DMA */
s32 osPiRawStartDma(s32 direction, u32 devAddr, void *dramAddr, u32 size);

/* FPC CSR */
u32 __osGetFpcCsr(void);
void __osSetFpcCsr(u32 value);

/* Misc */
void osInitialize(void);
void __osInitialize_common(void);
extern u64 osClockRate;

/* Profile */
typedef struct {
    u32 *histo_base;
    u32 histo_size;
    u32 *text_start;
    u32 *text_end;
} OSProf;

void osProfileInit(OSProf *prof, u32 count);
void osProfileStart(u32 index);
void osProfileStop(void);
void osProfileFlush(void);

/* GU math functions */
void guMtxF2L(f32 mf[4][4], Mtx *m);
void guMtxL2F(f32 mf[4][4], Mtx *m);
void guMtxIdent(Mtx *m);
void guMtxIdentF(f32 mf[4][4]);
void guOrtho(Mtx *m, f32 l, f32 r, f32 b, f32 t, f32 n, f32 f, f32 scale);
void guOrthoF(f32 mf[4][4], f32 l, f32 r, f32 b, f32 t, f32 n, f32 f, f32 scale);
void guPerspective(Mtx *m, u16 *perspNorm, f32 fovy, f32 aspect, f32 near, f32 far, f32 scale);
void guPerspectiveF(f32 mf[4][4], u16 *perspNorm, f32 fovy, f32 aspect, f32 near, f32 far, f32 scale);
void guLookAt(Mtx *m, f32 xEye, f32 yEye, f32 zEye,
              f32 xAt, f32 yAt, f32 zAt,
              f32 xUp, f32 yUp, f32 zUp);
void guLookAtF(f32 mf[4][4], f32 xEye, f32 yEye, f32 zEye,
               f32 xAt, f32 yAt, f32 zAt,
               f32 xUp, f32 yUp, f32 zUp);
void guLookAtReflect(Mtx *m, LookAt *l, f32 xEye, f32 yEye, f32 zEye,
                     f32 xAt, f32 yAt, f32 zAt,
                     f32 xUp, f32 yUp, f32 zUp);
void guRotate(Mtx *m, f32 angle, f32 x, f32 y, f32 z);
void guRotateF(f32 mf[4][4], f32 angle, f32 x, f32 y, f32 z);
void guScale(Mtx *m, f32 x, f32 y, f32 z);
void guScaleF(f32 mf[4][4], f32 x, f32 y, f32 z);
void guTranslate(Mtx *m, f32 x, f32 y, f32 z);
void guTranslateF(f32 mf[4][4], f32 x, f32 y, f32 z);
void guNormalize(f32 *x, f32 *y, f32 *z);
void guMtxCatF(f32 mf1[4][4], f32 mf2[4][4], f32 result[4][4]);
void guMtxCatL(Mtx *m1, Mtx *m2, Mtx *result);
void guMtxXFMF(f32 mf[4][4], f32 x, f32 y, f32 z, f32 *ox, f32 *oy, f32 *oz);
void guMtxXFML(Mtx *m, f32 x, f32 y, f32 z, f32 *ox, f32 *oy, f32 *oz);
void guAlignF(f32 mf[4][4], f32 a, f32 x, f32 y, f32 z);
s32 guRandom(void);
f32 cosf(f32 angle);
f32 sinf(f32 angle);

/* Hardware globals */
extern u32 osTvType;

/* RMON — defined in rmon.c */
void rmonMain(void);

/* Memory/DMA debugging — stubs */
#define osGetMemSize()  (16 * 1024 * 1024)

/* K0/Physical address macros — identity on PC (preserve full 64-bit pointer) */
#define OS_K0_TO_PHYSICAL(x)    osVirtualToPhysical_pc((void *)(x))
#define OS_PHYSICAL_TO_K0(x)    ((void *)(uintptr_t)(x))
#define PHYS_TO_K0(x)           OS_PHYSICAL_TO_K0(x)
#define K0_TO_PHYS(x)           OS_K0_TO_PHYSICAL(x)

/* Cycle conversion macros */
#define OS_USEC_TO_CYCLES(x)    (((u64)(x) * 46875ULL) / 1000ULL)
#define OS_CYCLES_TO_USEC(x)    (((u64)(x) * 1000ULL) / 46875ULL)
#define OS_NSEC_TO_CYCLES(x)    (((u64)(x) * 46875ULL) / 1000000ULL)
#define OS_CYCLES_TO_NSEC(x)    (((u64)(x) * 1000000ULL) / 46875ULL)

/* osPiReadIo — stub */
s32 osPiReadIo(u32 devAddr, u32 *data);

/* __osGetTLBHi — TLB stub */
u32 __osGetTLBHi(s32 index);

/* assert — N64 version uses osSyncPrintf */
#ifndef assert
#define assert(EX) ((void)0)
#endif

/* ROM segment symbol helpers.
 * On N64, segment symbols are linker-generated; &sym IS the ROM offset.
 * On PC, they're u32 variables storing the ROM offset value.
 * Use *(u32*)& to read the raw u32 regardless of extern declaration type
 * (u32, u32*, u8* — all map to the same u32 storage in segment_stubs.c). */
#define ROM_OFFSET(sym)       ((void *)(uintptr_t)(*(u32 *)&(sym)))
#define ROM_SPAN(start, end)  ((s32)(*(u32 *)&(end) - *(u32 *)&(start)))

#endif /* _PLATFORM_OS_H_ */
