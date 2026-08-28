#include <ultra64.h>
#include <PR/os.h>
#include "sched.h"
#include "audi.h"
#include "thread_config.h"
#include "bondgame.h"
#include "speed_graph.h"

/**
 * EU .data, offset from start of data_seg : 0x23A0
*/

#ifdef NATIVE_PORT
/* Port implementations in audi_port.c */
#elif defined(PORT_FIXME_STUBS)
/* audi.c uses deep libultra audio internals (ALGlobals, ALDMAproc, OSTime as
 * arithmetic type). Stubbed until audio infrastructure is built. */
void amCreateAudioManager(ALSynConfig *alconf) { (void)alconf; }
void amStartAudioThread(void) {}
#else

/**
 * @file audi.c
 * This file contains audio code. Starts main audio thread, handles some audio DMA.
 */

// 0x5622 = 22050
#define OUTPUT_RATE                    0x5622

#ifdef REFRESH_PAL
/* PAL */
#define MAYBE_FRAME_RATE                   50
#else
/* NTSC */
#define MAYBE_FRAME_RATE                   60
#endif


#define FRAMES_PER_FIELD_AS_POW2            1
#define AUDIO_FRAME_MESSAGE_QUEUE_SIZE      8
#define AUDIO_REPLY_MESSAGE_QUEUE_SIZE      8
#define AUDIO_DMA_IO_QUEUE_SIZE            64
#define AUDIO_DMA_QUEUE_SIZE               66
#define AUDIO_DMA_MAX_BUFFER_LENGTH     0x200

#define NUMBER_OUTPUT_BUFFERS               3
#define NUMBER_ACMD_LISTS                   2
#define MAX_ACMD_SIZE                    3000
#define NUMBER_DMA_BUFFERS                 64
#define EXTRA_SAMPLES                    0x25
#define AUDIO_FRAME_MESSAGE_QUEUE_SIZE      8
#define AUDIO_REPLY_MESSAGE_QUEUE_SIZE      8

#define MAIN_QUIT_MESSAGE                  10
#define AUDIO_MANAGER_COUNT_INTERVAL     0xf0

extern long long int rspbootTextStart[];
extern long long int gsp3DTextStart[];
extern long long int aspMainTextStart[];
extern long long int aspMainDataStart[];
extern u8 sp_audi[];

/**
 * Cached DMA window for sample data.
 * sizeof(struct DMABuffer_s) == 0x14 (20)
 */
typedef struct DMABuffer_s {
    /**
     * 0x0.
     */
    ALLink node;

    /**
     * 0x8.
     */
    int startAddr;

    /**
     * 0xc.
     */
    u32 lastFrame;

    /**
     * 0x10.
     */
    u8* ptr;
} DMABuffer;

/**
 * Audio DMA cache state.
 * sizeof(struct DMAState_s) == 0xc (12).
 */
typedef struct DMAState_s {
    /**
     * Decompiled behavior treats this as a byte flag, while surrounding struct
     * layout requires word alignment before firstUsed. The union makes that
     * layout explicit.
     * 0x0.
     */
    union {
        u8 initialized;
        s32 _unusedAlign;
    } u;

    /**
     * 0x4.
     */
    DMABuffer *firstUsed;

    /**
     * 0x8.
     */
    DMABuffer *firstFree;
} DMAState;

/**
 * Audio-manager message payload.
 */
typedef union AudioMessage_u {
    struct {
        s16 type;
    } gen;
    
    struct {
        s16 type;
        struct AudioInfo_s *info;
    } done;
    
    OSScMsg app;
} AudioMessage;

/**
 * Per-frame audio task payload.
 */
typedef struct AudioInfo_s {
    /**
    * Output data pointer.
    * 0x0.
    */
    s16 *data;

    /**
     * # of samples synthesized in this frame
     * 0x4.
     */
    s16 frameSamples;

    /**
     * scheduler structure
     * 0x8
     */
    OSScTask task;
} AudioInfo;

// unknown purpose
u32 D_800230F0 = 0;

u32 g_AudioFrameCount = 0;

u32 g_NextDMa = 0;

u32 g_CurrentAcmdList = 0;

/* Audio delay-table helper: convert milliseconds to an aligned sample count. */
#define ms *(((s32)((f32)44.1)) & ~0x7)

#define CUSTOM_FX_SECTION_COUNT   6
#define CUSTOM_FX_SECTION_SIZE    8
s32 CUSTOM_FX_PARAMS_N[CUSTOM_FX_SECTION_COUNT * CUSTOM_FX_SECTION_SIZE + 2] = {

    /* sections	   length */
             6,     160 ms,

    /*                                         chorus  chorus   filter
    input    output  fbcoef  ffcoef    gain     rate   depth    coef  */
        0,     4 ms,   9830,  -9830,      0,        0,     0,       0,
     4 ms,     8 ms,   9830,  -9830, 0x2B84,        0,     0,  0x2500,
    20 ms,    64 ms,  16384, -16384, 0x11EB,        0,     0,  0x3000,
    80 ms,   140 ms,  16384, -16384, 0x11EB,        0,     0,  0x3500,
    84 ms,   120 ms,   8192,  -8192,      0,        0,     0,  0x4000,
        0,   148 ms,  13000, -13000,      0,   0x017C,   0xA,  0x4500
};

s32 g_FirstTime = 1;

/*bss needs fixing */
s32 dword_CODE_bss_8005E4B0[2];

/**
 * Address 8005E4B8.
 * (type is u64)
 * Used in amMain.
 * This looks like it stores the largest sDeltaTime between
 * counts of AUDIO_MANAGER_COUNT_INTERVAL.
 */
OSTime g_LargestDeltaTime;

/**
 * Address 8005E4C0.
 * (type is u64)
 * Used in amMain.
 * Stores the elpased time of main loop (difference between sEndTime and sStartTime).
 */
OSTime g_DeltaTime;


/**
 * Address 8005E4C8.
 * Every AUDIO_MANAGER_COUNT_INTERVAL number of events, the average for sDeltaTimeSum
 * is computed and stored here.
 */
u64 g_DeltaAverage;


/**
 * Address 8005E4D0.
 * Tracks the sum total elapsed time. Reset every AUDIO_MANAGER_COUNT_INTERVAL.
 */
u64 g_DeltaTimeSum;

/**
 * Address 8005E4D8.
 * (type is u64)
 * Used in amMain.
 * Stores the time at the start of the loop.
 */
OSTime g_StartTime;

/**
 * Address 8005E4E0.
 * (type is u64)
 * Used in amMain.
 * Stores the time after primary processing is done.
 */
OSTime g_EndTime;

/**
 * Unknown / unused
 */
char dword_CODE_bss_8005E4E8[0x30];

/**
 * Address 8005e518.
 * sizeof(struct AudioManager_s) == 0x288 (648)
 */
struct AudioManager_s {

    /**
     * 0.
     */
    Acmd *cmdList[NUMBER_ACMD_LISTS];
    
    /**
     * 0x8.
     */
    AudioInfo *audioInfo[NUMBER_OUTPUT_BUFFERS];
    
    /**
    * 0x14.
    */
    u32 numberOutputBuffers;
    
    /**
     * 0x18.
     */
    OSThread audioThread;
    
    /**
     * 0x1c8.
     */
    OSMesgQueue frameMessageQueue;
    
    /**
     * 0x1e0.
     */
    OSMesg frameMessageBuffer[AUDIO_FRAME_MESSAGE_QUEUE_SIZE];
    
    /**
     * 0x200.
     */
    OSMesgQueue replyMessageQueue;

    /**
     * 0x218.
     */
    OSMesg replyMessageBuffer[AUDIO_REPLY_MESSAGE_QUEUE_SIZE];

    /**
     * 0x238
     */
    ALGlobals g;

} g_AudioManager;

/**
 * Address 0x8005e7a0.
 */
OSScClient g_AudioClient[2];

/**
 * Address 0x8005e7b0.
 */
DMAState g_DmaState;

DMABuffer g_DmaBuffers[NUMBER_DMA_BUFFERS];

u32 g_MinFrameSize;
u32 g_FrameSize;
u32 g_MaxFrameSize;
s32 g_CommandLength;

OSIoMesg g_DmaIOMessageBuffer[AUDIO_DMA_IO_QUEUE_SIZE];

OSMesgQueue g_DmaMessageQueue;

OSMesg g_DmaMessageBuffer[AUDIO_DMA_QUEUE_SIZE];


// Forward declarations
s32 amDmaCallback(s32 addr, s32 len, void* state);
void amClearDmaBuffers(void);
void amHandleFrameMessage(AudioInfo *info, AudioInfo *lastInfo);
void amHandleDoneMessage(AudioInfo *info);
void amMain(void* arg);
ALDMAproc amDmaNew(DMAState** state);


/**
 * Address 29D0 70001BD0
 * @param alconf hw setup/config.
 */
void amCreateAudioManager(ALSynConfig* alconf)
{
    u32 j;
    f32 fsize;

    alconf->dmaproc = &amDmaNew;
    alconf->outputRate = osAiSetFrequency(OUTPUT_RATE);
    
    fsize = (f32) ((alconf->outputRate << FRAMES_PER_FIELD_AS_POW2) / (f32)MAYBE_FRAME_RATE);
    
    g_FrameSize = (u32) fsize;

    if (g_FrameSize < fsize)
    {
        g_FrameSize++;
    }
    
    // This rounds up to the next multiple of 16.
    if (g_FrameSize & 0xf)
    {
        g_FrameSize = (g_FrameSize & ~0xf) + 0x10;
    }
    
    g_MinFrameSize = (u32)(g_FrameSize - 0x10);
    g_MaxFrameSize = (u32)(g_FrameSize + EXTRA_SAMPLES + 0x10);
    
    if (alconf->fxType == AL_FX_CUSTOM)
    {
        s32 sp48[CUSTOM_FX_SECTION_COUNT * CUSTOM_FX_SECTION_SIZE + 2] = CUSTOM_FX_PARAMS_N;
        alconf->params = sp48;
        alInit(&g_AudioManager.g, alconf);
    }
    else
    {
        alInit(&g_AudioManager.g, alconf);
    }
    
    for (j=0; j < NUMBER_OUTPUT_BUFFERS; j++)
    {
        g_AudioManager.audioInfo[j] = (AudioInfo *)alHeapDBAlloc(0, 0, alconf->heap, 1, sizeof(AudioInfo));
        g_AudioManager.audioInfo[j]->data = (s16*)alHeapDBAlloc(0, 0, alconf->heap, 1, g_MaxFrameSize * 4);
    }
    
    osCreateMesgQueue(&g_AudioManager.replyMessageQueue, (OSMesg*)&g_AudioManager.replyMessageBuffer, AUDIO_REPLY_MESSAGE_QUEUE_SIZE);
    osCreateMesgQueue(&g_AudioManager.frameMessageQueue, (OSMesg*)&g_AudioManager.frameMessageBuffer, AUDIO_FRAME_MESSAGE_QUEUE_SIZE);
    osCreateMesgQueue(&g_DmaMessageQueue, (OSMesg*)&g_DmaMessageBuffer, AUDIO_DMA_IO_QUEUE_SIZE);
    
    g_DmaBuffers[0].node.prev = NULL;
    g_DmaBuffers[0].node.next = NULL;

    for (j=0; (s32)j < NUMBER_DMA_BUFFERS - 1; j++)
    {
        alLink((ALLink*)&g_DmaBuffers[j+1], (ALLink*)&g_DmaBuffers[j]);
        g_DmaBuffers[j].ptr = (void*)alHeapDBAlloc(0, 0, alconf->heap, 1, AUDIO_DMA_MAX_BUFFER_LENGTH);
    }
    // last buffer already linked, but still needs buffer
    g_DmaBuffers[j].ptr = (void*)alHeapDBAlloc(0, 0, alconf->heap, 1, AUDIO_DMA_MAX_BUFFER_LENGTH);
    
    for (j=0; j < NUMBER_ACMD_LISTS; j++)
    {
        g_AudioManager.cmdList[j] = (Acmd *)alHeapDBAlloc(0, 0, alconf->heap, 1, MAX_ACMD_SIZE * sizeof(Acmd));
    }

    osCreateThread(&g_AudioManager.audioThread, AUDI_THREAD_ID, &amMain, 0, (void*)setSPToEnd((u8*)(&sp_audi), sizeof(sp_audi)), AUDI_THREAD_PRIORITY);
}

/**
 * 2B58 70001F58
 * insert sound manager thread
 *	redirect to 7000D580: A0=8005E530
 */
void amStartAudioThread(void)
{
    osStartThread(&g_AudioManager.audioThread);
}

/**
 * 2B7C 70001F7C
 * Processes scheduler retrace messages and submits audio work.
 *
 * @param arg unused.
 */
void amMain(void* arg)
{
	s32 count = 0;
	s32 done = 0;
	s16 *msg = NULL;
	AudioInfo *info = NULL;

	osScAddClient(&os_scheduler, &g_AudioClient[0], &g_AudioManager.frameMessageQueue, 1);

	while (!done) {
		osRecvMesg(&g_AudioManager.frameMessageQueue, (OSMesg *) &msg, OS_MESG_BLOCK);

		switch (*msg) {
		case OS_SC_RETRACE_MSG:
			g_StartTime = osGetTime();
		    speedgraphMarkerHandler(0x30000);
			amHandleFrameMessage(g_AudioManager.audioInfo[g_AudioFrameCount % 3], info);
			count++;
            speedgraphMarkerHandler(0x60000);

			g_EndTime = osGetTime();
			g_DeltaTime = g_EndTime - g_StartTime;

			if (count % AUDIO_MANAGER_COUNT_INTERVAL == 0) {
				g_DeltaAverage = g_DeltaTimeSum / AUDIO_MANAGER_COUNT_INTERVAL;
				
                // comma is required to continue into next statement, or will fail to match.
                // Or can have two statements on the same line.
                g_DeltaTimeSum = 0,
                g_LargestDeltaTime = 0;
			} else {
				g_DeltaTimeSum = (g_DeltaTimeSum + g_EndTime) - g_StartTime;
			}

			if (g_LargestDeltaTime < g_EndTime - g_StartTime) {
				g_LargestDeltaTime = g_EndTime - g_StartTime;
			}

			osRecvMesg(&g_AudioManager.replyMessageQueue, (OSMesg *) &info, OS_MESG_BLOCK);

			amHandleDoneMessage(info);
			break;
		case 5:
			done = 1;
			break;
		case MAIN_QUIT_MESSAGE:
			done = 1;
			break;
		}
	}

	alClose(&g_AudioManager.g);
}

/**
 * 2E44	70002244
 * Builds and submits the next audio task for the scheduler.
 * 
 * @param info audio info.
 * @param lastInfo last info.
 */
void amHandleFrameMessage(AudioInfo *info, AudioInfo *lastInfo)
{
    s16* outBuffer;
    Acmd *cmdlp;
    s32 temp_v1;

    /* call once a frame, before doing alAudioFrame */
    amClearDmaBuffers();
    
    outBuffer = (s16*)osVirtualToPhysical(info->data);
    
    if (lastInfo)
    {
        osAiSetNextBuffer(lastInfo->data, lastInfo->frameSamples * 4);
    }
    
    /* calculate how many samples needed for this frame to keep the DAC full */
    /* this will vary slightly frame to frame, must recalculate every frame */
    /* divide by four, to convert bytes */
    /* to stereo 16 bit samples */
    info->frameSamples = (u16)(((g_FrameSize - (osAiGetLength() >> 2)) + 16 + EXTRA_SAMPLES) & ~0xf);  
    temp_v1 = g_MinFrameSize;
    
    if ((s32)info->frameSamples < (s32)(s16)temp_v1)
    {
        info->frameSamples = (s16)temp_v1;
    }
    
    cmdlp = (Acmd*)alAudioFrame(g_AudioManager.cmdList[g_CurrentAcmdList], &g_CommandLength, outBuffer, info->frameSamples);
    
    /* paranoia */
    info->task.next = 0;
    info->task.flags = 0;

    /* reply to when finished */
    info->task.msgQ = (void *) (&(g_AudioManager.replyMessageQueue.mtqueue));

    /* reply with this message */
    info->task.msg = info;
    info->task.flags = OS_SC_NEEDS_RSP;
    info->task.list.t.data_ptr = (u64*)(g_AudioManager.cmdList[g_CurrentAcmdList]);
    info->task.list.t.data_size = (((s32)cmdlp - (s32)g_AudioManager.cmdList[g_CurrentAcmdList]) >> 3) * sizeof(Acmd);
    info->task.list.t.type = M_AUDTASK;
    info->task.list.t.ucode_boot = (u64*)rspbootTextStart;
    info->task.list.t.ucode_boot_size = ((s32)gsp3DTextStart - (s32)rspbootTextStart);
    info->task.list.t.flags = 0; // 1c
    info->task.list.t.ucode = (u64*)aspMainTextStart;
    info->task.list.t.ucode_data = (u64*)aspMainDataStart;
    info->task.list.t.ucode_data_size = SP_UCODE_DATA_SIZE;
    info->task.list.t.yield_data_ptr = NULL; // 50
    info->task.list.t.yield_data_size = 0; // 54
    
    osSendMesg(osScGetCmdQ(&os_scheduler), (OSMesg)&info->task, OS_MESG_NOBLOCK);

    /* swap which acmd list you use each frame */
    g_CurrentAcmdList ^= 1;
}


/**
 * 2FE4	700023E4
 * Updates audio underrun diagnostics after a completed frame.
 * 
 * @param info Unused.
 */
void amHandleDoneMessage(AudioInfo *info)
{
    s32 samplesLeft;
    /* Keep first-time state global to preserve the current matching layout. */
    int *b;
    
    samplesLeft = (s32)osAiGetLength() >> 2;

    /*
    * The initial code probably looked like the following (and this
    * is what you get with mips_to_c):
    * 
    *     if (samplesLeft == 0 && !firstTime) 
    */   
    b = &g_FirstTime;
    if (!samplesLeft && !(*b))
    {
        // debug underrun printf
#ifdef ENABLE_LOG
      osSyncPrintf("audio: ai out of samples\n");    
#endif
        g_FirstTime = 0;
    }
}

/**
 * 3024 70002424
 * Returns a cached sample-data window, starting a PI DMA when needed.
 * 
 * @param addr ?.
 * @param len ?.
 * @param state unused.
 * @return result from call to osVirtualToPhysical
 */
s32 amDmaCallback(s32 addr, s32 len, void* state)
{
    void *freeBuffer;
    s32 delta;
    DMABuffer *dmaPtr;
    s32 addrEnd;
    s32 buffEnd;
    DMABuffer *lastDmaPtr;

    lastDmaPtr = NULL;
    dmaPtr = g_DmaState.firstUsed;
    delta = addr & 0x1;
    addrEnd = addr + len;

    /* first check to see if a currently existing buffer contains the
       sample that you need.  */
    while (dmaPtr)
    {
        buffEnd = dmaPtr->startAddr + AUDIO_DMA_MAX_BUFFER_LENGTH;

        /* since buffers are ordered */
        /* abort if past possible */
        if ((u32)dmaPtr->startAddr > (u32)addr)
        {
            break;
        }
        /* yes, found a buffer with samples */
        else if (addrEnd <= buffEnd)
        {
            /* mark it used */
            dmaPtr->lastFrame = (s32) g_AudioFrameCount;
            freeBuffer = (dmaPtr->ptr + addr) - dmaPtr->startAddr;
            return osVirtualToPhysical(freeBuffer);
        }
    
        lastDmaPtr = dmaPtr;
        dmaPtr = (DMABuffer*)dmaPtr->node.next;
    }

    /* get here, and you didn't find a buffer, so dma a new one */
    /* get a buffer from the free list */
    dmaPtr = g_DmaState.firstFree;

    /* 
     * if you get here and dmaPtr is null, send back a bogus
     * pointer, it's better than nothing
     */
    if (!dmaPtr)
    {
        if (!lastDmaPtr)
        {
            lastDmaPtr = g_DmaState.firstUsed;
        }

        return osVirtualToPhysical(lastDmaPtr->ptr) + delta;
    }

    g_DmaState.firstFree = (DMABuffer*)dmaPtr->node.next;
    alUnlink((ALLink*)dmaPtr);

    /* add it to the used list */
    /* if you have other dmabuffers used, add this one */
    /* to the list, after the last one checked above */
    if (lastDmaPtr)
    {
        alLink((ALLink*)dmaPtr, (ALLink*)lastDmaPtr);
    }
    /* if this buffer is before any others */
    // Jam at begining of list
    else if (g_DmaState.firstUsed)
    {
        lastDmaPtr = g_DmaState.firstUsed;
        g_DmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = (ALLink*)lastDmaPtr;
        dmaPtr->node.prev = 0;
        lastDmaPtr->node.prev = (ALLink*)dmaPtr;
    }
    /* no buffers in list, this is the first one */
    else
    {
        g_DmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = 0;
        dmaPtr->node.prev = 0;
    }

    freeBuffer = dmaPtr->ptr;
    addr -= delta;
    dmaPtr->startAddr = addr;
    dmaPtr->lastFrame = g_AudioFrameCount;

    osPiStartDma(&g_DmaIOMessageBuffer[g_NextDMa++], OS_MESG_PRI_HIGH, OS_READ, (u32)addr, freeBuffer, AUDIO_DMA_MAX_BUFFER_LENGTH, &g_DmaMessageQueue);
    return (s32)osVirtualToPhysical(freeBuffer) + delta;
}

/**
 * 31D8 700025D8
 * Initializes audio DMA cache state on first use.
 * 
 * @param state will point to g_DmaState after call.
 * @return Address of dma callback function.
 */
ALDMAproc amDmaNew(DMAState** state)
{
    if (g_DmaState.u.initialized == 0)
    {
        g_DmaState.firstUsed = NULL;
        g_DmaState.firstFree = g_DmaBuffers;
        g_DmaState.u.initialized = (u8)1U;
    }

    *state = &g_DmaState;
    return &amDmaCallback;
}

/**
 * 3210 70002610
 * Reclaims stale sample DMA buffers.
 */
void amClearDmaBuffers(void)
{
    u32 i;
    OSMesg osmesg;
    DMABuffer *dmaPtr, *nextPtr;
    
    osmesg = 0;

   /*
    * Don't block here. If dma's aren't complete, you've had an audio
    * overrun. (Bad news, but go for it anyway, and try and recover.
    */
   for (i=0; i < g_NextDMa; i++)
   {
       if (osRecvMesg(&g_DmaMessageQueue, (OSMesg *)&osmesg, OS_MESG_NOBLOCK) == -1)
       {
#ifdef ENABLE_LOG
	        osSyncPrintf("Dma not done\n");
#endif
       }

#ifdef DEBUG
    /* Reserved for frame-DMA debug logging. */
#endif
   }

    dmaPtr = g_DmaState.firstUsed;
    while (dmaPtr)
    {
        nextPtr = (DMABuffer*)dmaPtr->node.next;

        /* remove old dma's from list */
        /* Can change FRAME_LAG value.  Should be at least one.  */
        /* Larger values mean more buffers needed, but fewer DMA's */
        if (dmaPtr->lastFrame + 1 < g_AudioFrameCount)
        {
            if (g_DmaState.firstUsed == dmaPtr)
            {
                g_DmaState.firstUsed = (DMABuffer*)dmaPtr->node.next;
            }

            alUnlink((ALLink*)dmaPtr);

            if (g_DmaState.firstFree)
            {
                alLink((ALLink*)dmaPtr, (ALLink*)g_DmaState.firstFree);
            }
            else
            {
                g_DmaState.firstFree = dmaPtr;
                dmaPtr->node.next = 0;
                dmaPtr->node.prev = 0;
            }
        }
        dmaPtr = nextPtr;
    }

    g_NextDMa = 0U;
    g_AudioFrameCount = (s32)(g_AudioFrameCount + 1);
}

#endif /* PORT_FIXME_STUBS */
