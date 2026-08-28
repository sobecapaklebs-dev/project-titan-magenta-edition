/*
 * Project Titan BuildFix28 - Neutrino resident Smart IGR / Overlay hook.
 * PadOpen pattern matching is derived from the AFL-3.0 OPL/Neutrino lineage
 * already vendored by Project Titan. The Titan overlay renderer is shared with
 * the UL backend; HOME/Theme Engine is never loaded in-game.
 */
#include <kernel.h>
#include <delaythread.h>
#include <tamtypes.h>
#include "asm.h"
#include "../include/util.h"
#include "padhook.h"
#include "padpatterns.h"
#include "eecore_config.h"
#include "titan_engine_overlay.h"
#include <titan_pademu_telemetry.h>
#include <sifcmd.h>

#define TITAN_OVERLAY_PROFILE_CONTEXT2_SAFE (1u << 0)
#define TITAN_IGR_STACK_SIZE (4 * 1024)

static int (*scePadPortOpen)(int port, int slot, void *addr);
static int (*scePad2CreateSocket)(pad2socketparam_t *SocketParam, void *addr);
static paddata_t Pad_Data;
static int IGR_Thread_ID = -1;
static int IGR_Intc_ID = -1;
static u8 IGR_Stack[TITAN_IGR_STACK_SIZE] __attribute__((aligned(16), section(".titan_igr_stack")));
static volatile int TitanOverlay_Request;
static volatile int TitanSystemButton_Request;
static volatile int TitanOverlay_Active;
static volatile int TitanOverlay_TriggeredBySystem;
static int TitanSystemHandler_Installed;
static u32 TitanOverlay_SuspendedThreads[8];
extern void *_gp;

static void TitanSystemButton_Handler(void *packet, void *opt)
{
    TitanPademuSystemButtonEvent *e=(TitanPademuSystemButtonEvent *)packet;
    (void)opt;
    if(!e || !e->pressed) return;
    if(TitanOverlay_Active) { TitanSystemButton_Request=1; return; }
    TitanOverlay_TriggeredBySystem=1;
    TitanOverlay_Request=1;
    if(IGR_Thread_ID>=0) { iChangeThreadPriority(IGR_Thread_ID,0); iWakeupThread(IGR_Thread_ID); }
}

static u32 TitanOverlay_ReadButtons(void)
{
    u8 low, high;
    if (Pad_Data.pad_buf == NULL)
        return 0;
    low = ((u8 *)UNCACHED_SEG(Pad_Data.pad_buf))[Pad_Data.pos_combo2];
    high = ((u8 *)UNCACHED_SEG(Pad_Data.pad_buf))[Pad_Data.pos_combo1];
    return ((u32)(~low) & 0xffu) | (((u32)(~high) & 0xffu) << 8);
}

static void TitanOverlay_SuspendGameThreads(void)
{
    int i, self = GetThreadId();
    for (i = 0; i < 8; ++i) TitanOverlay_SuspendedThreads[i] = 0;
    for (i = 1; i < 256; ++i) {
        if (i == self) continue;
        if (SuspendThread(i) >= 0)
            TitanOverlay_SuspendedThreads[(unsigned)i >> 5] |= 1u << ((unsigned)i & 31u);
    }
}

static void TitanOverlay_ResumeGameThreads(void)
{
    int i;
    for (i = 1; i < 256; ++i) {
        if (TitanOverlay_SuspendedThreads[(unsigned)i >> 5] & (1u << ((unsigned)i & 31u)))
            ResumeThread(i);
    }
    for (i = 0; i < 8; ++i) TitanOverlay_SuspendedThreads[i] = 0;
}

static TitanEngineOverlayAction TitanOverlay_Run(void)
{
    TitanEngineOverlay overlay;
    TitanEngineOverlayAction action = TITAN_ENGINE_OVERLAY_NONE;
    u32 caps = TITAN_ENGINE_OVERLAY_CAP_RETURN | TITAN_ENGINE_OVERLAY_CAP_RESUME;
    u32 buttons, last_selection;
    unsigned int release_wait;

    if (eec.TitanOverlayLevel < 2 ||
        (eec.TitanOverlayProfileFlags & TITAN_OVERLAY_PROFILE_CONTEXT2_SAFE) == 0)
        return TITAN_ENGINE_OVERLAY_UNSAFE;

    caps |= TITAN_ENGINE_OVERLAY_CAP_CONTEXT2_SAFE;
    TitanEngineOverlay_Init(&overlay, caps, eec.GameID);
    if (!TitanEngineOverlay_Open(&overlay))
        return TITAN_ENGINE_OVERLAY_UNSAFE;

    TitanOverlay_Active=1;
    TitanSystemButton_Request=0;
    TitanOverlay_SuspendGameThreads();
    if (!TitanEngineOverlay_Render(&overlay)) {
        TitanOverlay_ResumeGameThreads();
        return TITAN_ENGINE_OVERLAY_UNSAFE;
    }

    /* Never leak the trigger chord into overlay navigation. A System Button
       trigger is delivered out-of-band and therefore has no DS2 chord to drain. */
    for (release_wait = 0; !TitanOverlay_TriggeredBySystem && release_wait < 240u; ++release_wait) {
        buttons = TitanOverlay_ReadButtons();
        if ((buttons & 0x0f09u) != 0x0f09u)
            break;
        DelayThread(8333);
    }
    if (!TitanOverlay_TriggeredBySystem && release_wait == 240u) {
        TitanOverlay_Active=0;
        TitanOverlay_ResumeGameThreads();
        return TITAN_ENGINE_OVERLAY_UNSAFE;
    }
    TitanOverlay_TriggeredBySystem=0;

    overlay.previous_buttons = TitanOverlay_ReadButtons();
    last_selection = overlay.selection;
    while (action == TITAN_ENGINE_OVERLAY_NONE) {
        if (TitanSystemButton_Request) { TitanSystemButton_Request=0; action=TITAN_ENGINE_OVERLAY_RESUME; break; }
        buttons = TitanOverlay_ReadButtons();
        action = TitanEngineOverlay_Update(&overlay, buttons);
        if (overlay.selection != last_selection) {
            last_selection = overlay.selection;
            if (!TitanEngineOverlay_Render(&overlay)) {
                action = TITAN_ENGINE_OVERLAY_UNSAFE;
                break;
            }
        }
        if (action == TITAN_ENGINE_OVERLAY_NONE)
            DelayThread(8333);
    }

    TitanOverlay_Active=0;
    TitanOverlay_ResumeGameThreads();
    return action;
}

static void TitanOverlay_ReturnToTitan(void)
{
    /* LoadExecPS2 is already routed through the resident Neutrino EE core.
       That path wipes title memory, reboots the IOP into a clean environment,
       reloads the destination ELF and CleanExecPS2s it. Reuse it rather than
       inventing a second destructive return pipeline. */
    if (eec.TitanExitPath[0] != '\0')
        LoadExecPS2(eec.TitanExitPath, 0, NULL);
}

static void IGR_Thread(void *arg)
{
    (void)arg;
    for (;;) {
        TitanEngineOverlayAction action;
        SleepThread();
        if (!TitanOverlay_Request)
            continue;
        TitanOverlay_Request = 0;
        if (eec.TitanOverlayLevel == 1) {
            Pad_Data.combo_type = 0;
            TitanOverlay_ReturnToTitan();
            continue;
        }
        action = TitanOverlay_Run();
        Pad_Data.combo_type = 0;
        if (action == TITAN_ENGINE_OVERLAY_RETURN)
            TitanOverlay_ReturnToTitan();
        /* RESUME or UNSAFE leaves the title running. Return-only/UNKNOWN is
           handled directly in interrupt context by waking this thread with a
           synthetic RETURN action below. */
    }
}

static int IGR_Intc_Handler(int cause)
{
    u8 state, frame, combo1, combo2;
    (void)cause;

    if (Pad_Data.pad_buf != NULL) {
        state = ((u8 *)UNCACHED_SEG(Pad_Data.pad_buf))[Pad_Data.pos_state];
        frame = ((u8 *)UNCACHED_SEG(Pad_Data.pad_buf))[Pad_Data.pos_frame];
        combo1 = ((u8 *)UNCACHED_SEG(Pad_Data.pad_buf))[Pad_Data.pos_combo1];
        combo2 = ((u8 *)UNCACHED_SEG(Pad_Data.pad_buf))[Pad_Data.pos_combo2];
        if (((Pad_Data.libpad == IGR_LIBPAD) && state == IGR_PAD_STABLE_V1) ||
            ((Pad_Data.libpad == IGR_LIBPAD2) && state == IGR_PAD_STABLE_V2)) {
            if (Pad_Data.vb_count++ >= 10) {
                Pad_Data.prev_frame = frame;
                Pad_Data.vb_count = 0;
            }
            if (combo1 == IGR_COMBO_R1_L1_R2_L2 && combo2 == IGR_COMBO_START_SELECT) {
                Pad_Data.combo_type = combo2;
                TitanOverlay_Request = 1;
                Pad_Data.combo_type = 0;
                iChangeThreadPriority(IGR_Thread_ID, 0);
                iWakeupThread(IGR_Thread_ID);
            }
        }
    }
    ExitHandler();
    return 0;
}

static void Set_libpad_Params(void *addr)
{
    DI();
    Pad_Data.pad_buf = addr;
    if (Pad_Data.libpad == IGR_LIBPAD) {
        if (Pad_Data.libversion >= 0x0160) {
            Pad_Data.pos_combo1=3; Pad_Data.pos_combo2=2; Pad_Data.pos_state=112; Pad_Data.pos_frame=88;
        } else {
            Pad_Data.pos_combo1=11; Pad_Data.pos_combo2=10; Pad_Data.pos_state=4; Pad_Data.pos_frame=0;
        }
    } else if (Pad_Data.libpad == IGR_LIBPAD2) {
        Pad_Data.pos_combo1=29; Pad_Data.pos_combo2=28; Pad_Data.pos_state=4; Pad_Data.pos_frame=124;
    }
    EI();
}

void Install_IGR(void)
{
    ee_thread_t t;
    Pad_Data.vb_count=0; Pad_Data.combo_type=0; Pad_Data.prev_frame=0; TitanOverlay_Request=0;
    if (IGR_Thread_ID < 0) {
        t.gp_reg=&_gp; t.func=IGR_Thread; t.stack=IGR_Stack; t.stack_size=TITAN_IGR_STACK_SIZE; t.initial_priority=127;
        IGR_Thread_ID=CreateThread(&t);
        if (IGR_Thread_ID >= 0) StartThread(IGR_Thread_ID,NULL);
    }
    if (!TitanSystemHandler_Installed && IGR_Thread_ID >= 0) {
        SifAddCmdHandler(TITAN_PADEMU_SYSTEM_CMD_ID, TitanSystemButton_Handler, NULL);
        TitanSystemHandler_Installed=1;
    }
        if (IGR_Intc_ID < 0 && IGR_Thread_ID >= 0) {
        IGR_Intc_ID=AddIntcHandler(kINTC_VBLANK_END,IGR_Intc_Handler,0);
        if (IGR_Intc_ID >= 0) EnableIntc(kINTC_VBLANK_END);
    }
}

void Reset_Padhook(void)
{
    Pad_Data.pad_buf=NULL;
    Pad_Data.combo_type=0;
    TitanOverlay_Request=0;
    TitanSystemButton_Request=0; TitanOverlay_Active=0; TitanOverlay_TriggeredBySystem=0;
}
void Remove_Padhook(void) { Reset_Padhook(); }
void IGR_Exit(s32 exit_code) { (void)exit_code; TitanOverlay_ReturnToTitan(); }

static int Hook_scePadPortOpen(int port,int slot,void *addr)
{
    int ret=scePadPortOpen(port,slot,addr);
    if(port==0 && slot==0){ Install_IGR(); Set_libpad_Params(addr); }
    return ret;
}
static int Hook_scePad2CreateSocket(pad2socketparam_t *sp,void *addr)
{
    int ret=scePad2CreateSocket(sp,addr);
    if(sp==NULL || (sp->port==0 && sp->slot==0)){ Install_IGR(); Set_libpad_Params(addr); }
    return ret;
}

int Install_PadOpen_Hook(u32 mem_start,u32 mem_end,int mode)
{
    u32 *ptr,*ptr2,inst,fncall,mem_size,mem_size2,pattern[1],mask[1];
    int i,found=0,patched=0;
    pattern_t patterns[NB_PADOPEN_PATTERN]={
        {padPortOpenpattern0,padPortOpenpattern0_mask,sizeof(padPortOpenpattern0),1,0x0211},
        {pad2CreateSocketpattern0,pad2CreateSocketpattern0_mask,sizeof(pad2CreateSocketpattern0),2,0x0200},
        {pad2CreateSocketpattern1,pad2CreateSocketpattern1_mask,sizeof(pad2CreateSocketpattern1),2,0x0200},
        {pad2CreateSocketpattern2,pad2CreateSocketpattern2_mask,sizeof(pad2CreateSocketpattern2),2,0x0200},
        {padPortOpenpattern1,padPortOpenpattern1_mask,sizeof(padPortOpenpattern1),1,0x0210},
        {padPortOpenpattern2,padPortOpenpattern2_mask,sizeof(padPortOpenpattern2),1,0x0160},
        {padPortOpenpattern3,padPortOpenpattern3_mask,sizeof(padPortOpenpattern3),1,0x0150}};
    for(i=0;i<NB_PADOPEN_PATTERN;i++){
        ptr=(u32 *)mem_start;
        while(ptr){
            mem_size=mem_end-(u32)ptr;
            ptr=find_pattern_with_mask(ptr,mem_size,patterns[i].pattern,patterns[i].mask,patterns[i].size);
            if(!ptr) break;
            found=1;
            if(patterns[i].type==IGR_LIBPAD) scePadPortOpen=(void *)ptr; else scePad2CreateSocket=(void *)ptr;
            if(mode==PADOPEN_HOOK){
                inst=0x08000000 | (0x03ffffff & ((u32)ptr>>2)); pattern[0]=inst; mask[0]=0xfbffffff;
                ptr2=(u32 *)mem_start;
                while(ptr2){
                    mem_size2=mem_end-(u32)ptr2;
                    ptr2=find_pattern_with_mask(ptr2,mem_size2,pattern,mask,sizeof(pattern));
                    if(!ptr2) break;
                    patched=1; fncall=(u32)ptr2; inst=ptr2[0]&0xfc000000;
                    inst |= 0x03ffffff & ((patterns[i].type==IGR_LIBPAD?(u32)Hook_scePadPortOpen:(u32)Hook_scePad2CreateSocket)>>2);
                    _sw(inst,fncall); Pad_Data.libpad=patterns[i].type; Pad_Data.libversion=patterns[i].version; ptr2++;
                }
                if(!patched){
                    pattern[0]=(u32)ptr; mask[0]=0xffffffff; ptr2=(u32 *)mem_start;
                    while(ptr2){
                        mem_size2=mem_end-(u32)ptr2;
                        ptr2=find_pattern_with_mask(ptr2,mem_size2,pattern,mask,sizeof(pattern));
                        if(!ptr2) break;
                        patched=1; fncall=(u32)ptr2;
                        inst=(patterns[i].type==IGR_LIBPAD)?(u32)Hook_scePadPortOpen:(u32)Hook_scePad2CreateSocket;
                        _sw(inst,fncall); Pad_Data.libpad=patterns[i].type; Pad_Data.libversion=patterns[i].version; ptr2++;
                    }
                }
            } else break;
            ptr += patterns[i].size >> 2;
        }
        if(patched || (mode==PADOPEN_CHECK && found)) break;
    }
    FlushCache(WRITEBACK_DCACHE); FlushCache(INVALIDATE_ICACHE);
    return patched;
}
