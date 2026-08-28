#include "titan_hid_input.h"
#include "titan_iop_modules.h"
#include "titan_unified_input.h"
#include "titan_time.h"
#include <string.h>
#if defined(__EE__) || defined(_EE)
#ifndef NEWLIB_PORT_AWARE
#define NEWLIB_PORT_AWARE 1
#endif
#include <fileXio_rpc.h>
#include <io_common.h>
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <ps2kbd.h>
#include <ps2mouse.h>
#endif

#define KBD_CAPS (TITAN_INPUT_CAP_KEYBOARD|TITAN_INPUT_CAP_BUTTONS|TITAN_INPUT_CAP_WIRED)
#define MOUSE_CAPS (TITAN_INPUT_CAP_POINTER|TITAN_INPUT_CAP_BUTTONS|TITAN_INPUT_CAP_WIRED)

static TitanHidInputStatus g_hid;
static uint8_t g_modifiers;

#if defined(__EE__) || defined(_EE)
static int g_kbd_fd=-1;
#define TITAN_KBD_RING_CAPACITY 32u
#define TITAN_KBD_WORKER_POLL_MS 8u
static kbd_rawkey g_kbd_ring[TITAN_KBD_RING_CAPACITY];
static volatile unsigned int g_kbd_ring_read;
static volatile unsigned int g_kbd_ring_write;
static uint64_t g_kbd_next_poll_ms;
static void kbd_barrier(void){__asm__ volatile("" ::: "memory");}
static int kbd_ring_push(const kbd_rawkey *k){unsigned int w,n;if(!k)return 0;w=g_kbd_ring_write;n=(w+1u)&(TITAN_KBD_RING_CAPACITY-1u);if(n==g_kbd_ring_read){g_hid.errors++;return 0;}g_kbd_ring[w]=*k;kbd_barrier();g_kbd_ring_write=n;return 1;}
static int kbd_ring_pop(kbd_rawkey *k){unsigned int r;if(!k)return 0;r=g_kbd_ring_read;if(r==g_kbd_ring_write)return 0;*k=g_kbd_ring[r];kbd_barrier();g_kbd_ring_read=(r+1u)&(TITAN_KBD_RING_CAPACITY-1u);return 1;}

/* PS2SDK's ps2mouse RPC exposes PS2MOUSE_ENUM, which returns the driver's
 * physical dev_count.  Treat a bound RPC server as "backend available", not
 * "mouse connected".  This prevents a loaded ps2mouse.irx from creating a
 * phantom Unified Input device. */
typedef enum TitanMouseRpcOp { TITAN_MOUSE_RPC_NONE=0, TITAN_MOUSE_RPC_ENUM, TITAN_MOUSE_RPC_READ } TitanMouseRpcOp;
static SifRpcClientData_t g_mouse_rpc __attribute__((aligned(64)));
static unsigned char g_mouse_rpc_buf[128] __attribute__((aligned(64)));
static volatile int g_mouse_rpc_pending;
static volatile int g_mouse_rpc_done;
static volatile int g_mouse_rpc_op;
static unsigned int g_mouse_presence_frame;
static unsigned int g_mouse_physical_count;
static mouse_data g_mouse_last;

static void mouse_rpc_done(void *arg){(void)arg;g_mouse_rpc_done=1;}
static int exec_mod(const unsigned char*d,unsigned int n){int r=-1,id=SifExecModuleBuffer((void*)d,(int)n,0,0,&r);return(id>=0&&r>=0)?id:-1;}
static uint8_t hid_ascii(uint8_t k,uint8_t mods){int sh=(mods&(PS2KBD_LEFT_SHIFT|PS2KBD_RIGHT_SHIFT))!=0;if(k>=0x04&&k<=0x1d)return(uint8_t)((sh?'A':'a')+(k-0x04));if(k>=0x1e&&k<=0x26)return(uint8_t)((sh?'!':'1')+(k-0x1e));if(k==0x27)return sh?')':'0';if(k==0x2c)return' ';if(k==0x28)return'\n';return 0;}
static uint32_t key_button(uint8_t k){switch(k){case 0x4f:return TITAN_NBTN_RIGHT;case 0x50:return TITAN_NBTN_LEFT;case 0x51:return TITAN_NBTN_DOWN;case 0x52:return TITAN_NBTN_UP;case 0x28:return TITAN_NBTN_CROSS;case 0x29:return TITAN_NBTN_CIRCLE;default:return 0;}}
static int mouse_bind_bounded(void){unsigned i;memset(&g_mouse_rpc,0,sizeof(g_mouse_rpc));for(i=0;i<16u;i++){if(sceSifBindRpc(&g_mouse_rpc,PS2MOUSE_BIND_RPC_ID,0)<0)return 0;if(g_mouse_rpc.server)return 1;nopdelay();}return 0;}

/* P10.1_ACCESSORY_TRUTH_SCAN_RETRY: keep the PS2SDK command literal at the
 * call site as well as SIF_RPC_M_NOWAIT.  Besides making the nonblocking
 * contract auditable, ENUM and READ can never accidentally share an opcode. */
static int mouse_rpc_prepare(int op)
{
    if(!g_mouse_rpc.server||g_mouse_rpc_pending)return 0;
    memset(g_mouse_rpc_buf,0,sizeof(g_mouse_rpc_buf));
    g_mouse_rpc_done=0;
    g_mouse_rpc_op=op;
    g_mouse_rpc_pending=1;
    return 1;
}
static void mouse_enum_poll(void)
{
    if(!mouse_rpc_prepare(TITAN_MOUSE_RPC_ENUM))return;
    if(sceSifCallRpc(&g_mouse_rpc,PS2MOUSE_ENUM,SIF_RPC_M_NOWAIT,
                     g_mouse_rpc_buf,sizeof(g_mouse_rpc_buf),
                     g_mouse_rpc_buf,sizeof(g_mouse_rpc_buf),mouse_rpc_done,0)<0){
        g_mouse_rpc_pending=0;g_mouse_rpc_op=TITAN_MOUSE_RPC_NONE;g_hid.errors++;
    }
}
static void mouse_poll(void)
{
    if(!mouse_rpc_prepare(TITAN_MOUSE_RPC_READ))return;
    if(sceSifCallRpc(&g_mouse_rpc,PS2MOUSE_READ,SIF_RPC_M_NOWAIT,
                     g_mouse_rpc_buf,sizeof(g_mouse_rpc_buf),
                     g_mouse_rpc_buf,sizeof(g_mouse_rpc_buf),mouse_rpc_done,0)<0){
        g_mouse_rpc_pending=0;g_mouse_rpc_op=TITAN_MOUSE_RPC_NONE;g_hid.errors++;
    }
}

/* Returns 1 when a mouse data sample was consumed. Presence transitions are
 * applied on the main/input thread so Unified Input is never mutated from an
 * RPC callback. */
static int mouse_consume(mouse_data *m)
{
    unsigned char *u;
    int op;
    if(!g_mouse_rpc_done)return 0;
    g_mouse_rpc_done=0;g_mouse_rpc_pending=0;op=g_mouse_rpc_op;g_mouse_rpc_op=TITAN_MOUSE_RPC_NONE;
    u=(unsigned char*)UNCACHED_SEG(g_mouse_rpc_buf);
    if(op==TITAN_MOUSE_RPC_ENUM){
        unsigned int count=((const unsigned int*)u)[0];
        g_mouse_physical_count=count;
        if(count>0u && g_hid.mouse_device_id==0u){
            g_hid.mouse_device_id=TitanUnifiedInput_Connect(TITAN_INPUT_DEVICE_MOUSE,TITAN_INPUT_TRANSPORT_USB,0,0,MOUSE_CAPS,0x4d4f5501u);
        }else if(count==0u && g_hid.mouse_device_id!=0u){
            TitanUnifiedInput_Disconnect(g_hid.mouse_device_id);g_hid.mouse_device_id=0u;
        }
        return 0;
    }
    if(op==TITAN_MOUSE_RPC_READ){
        memcpy(&g_mouse_last,u,sizeof(g_mouse_last));if(m)*m=g_mouse_last;return 1;
    }
    return 0;
}
#endif

void TitanHidInput_Initialize(void)
{
    memset(&g_hid,0,sizeof(g_hid));g_hid.keyboard_module_id=g_hid.mouse_module_id=-1;g_modifiers=0;
#if defined(__EE__) || defined(_EE)
    g_mouse_rpc_pending=0;g_mouse_rpc_done=0;g_mouse_rpc_op=TITAN_MOUSE_RPC_NONE;g_mouse_presence_frame=0u;g_mouse_physical_count=0u;memset(&g_mouse_last,0,sizeof(g_mouse_last));
    g_kbd_ring_read=0u;g_kbd_ring_write=0u;g_kbd_next_poll_ms=0u;memset(g_kbd_ring,0,sizeof(g_kbd_ring));

    /* A successfully loaded/open ps2kbd driver proves only that the backend is
     * available.  PS2SDK's keyboard API has no public physical-device enum.
     * Therefore register a keyboard with Unified Input only after an actual raw
     * keyboard report arrives.  This gives zero false positives in Accessories. */
    g_hid.keyboard_module_id=exec_mod(titan_usbkbd_irx,size_titan_usbkbd_irx);
    if(g_hid.keyboard_module_id>=0){
        (void)fileXioInit();g_kbd_fd=fileXioOpen(PS2KBD_DEVFILE,O_RDONLY,0);
        if(g_kbd_fd>=0){uint32_t raw=PS2KBD_READMODE_RAW,nb=PS2KBD_NONBLOCKING;fileXioIoctl(g_kbd_fd,PS2KBD_IOCTL_SETREADMODE,&raw);fileXioIoctl(g_kbd_fd,PS2KBD_IOCTL_SETBLOCKMODE,&nb);g_hid.keyboard_ready=1;}
    }

    g_hid.mouse_module_id=exec_mod(titan_usbmouse_irx,size_titan_usbmouse_irx);
    if(g_hid.mouse_module_id>=0&&mouse_bind_bounded()){
        g_hid.mouse_ready=1;
        /* Asynchronous physical enumeration; no device is advertised until
         * PS2MOUSE_ENUM reports dev_count > 0. */
        mouse_enum_poll();
    }
#endif
    g_hid.initialized=1;
}

void TitanHidInput_Update(void)
{
#if defined(__EE__) || defined(_EE)
    if(g_hid.keyboard_ready){
        kbd_rawkey k;unsigned n=0;
        while(n++<8u&&kbd_ring_pop(&k)){
            uint32_t mask=0;TitanNormalizedInputState st;uint8_t bit;
            if(g_hid.keyboard_device_id==0u)
                g_hid.keyboard_device_id=TitanUnifiedInput_Connect(TITAN_INPUT_DEVICE_KEYBOARD,TITAN_INPUT_TRANSPORT_USB,0,0,KBD_CAPS,0x4b424401u);
            if(!g_hid.keyboard_device_id)continue;
            if(k.key>=0xe0&&k.key<=0xe7){bit=(uint8_t)(1u<<(k.key-0xe0));if(k.state==PS2KBD_RAWKEY_DOWN)g_modifiers|=bit;else g_modifiers&=(uint8_t)~bit;}
            memset(&st,0,sizeof(st));st.left_x=st.left_y=st.right_x=st.right_y=128;mask=key_button(k.key);if(k.state==PS2KBD_RAWKEY_DOWN)st.buttons=mask;
            (void)TitanUnifiedInput_SubmitState(g_hid.keyboard_device_id,&st,0,0,0);
            (void)TitanUnifiedInput_SubmitKeyboardEvent(g_hid.keyboard_device_id,k.state==PS2KBD_RAWKEY_DOWN,k.key,g_modifiers,hid_ascii(k.key,g_modifiers));
        }
    }
    if(g_hid.mouse_ready){
        mouse_data m;int got=mouse_consume(&m);
        g_mouse_presence_frame++;
        if(got && g_hid.mouse_device_id){
            TitanNormalizedInputState st;memset(&st,0,sizeof(st));st.left_x=st.left_y=st.right_x=st.right_y=128;st.pointer_dx=(int16_t)m.x;st.pointer_dy=(int16_t)m.y;st.wheel=(int8_t)m.wheel;if(m.buttons&PS2MOUSE_BTN1)st.buttons|=TITAN_NBTN_CROSS;if(m.buttons&PS2MOUSE_BTN2)st.buttons|=TITAN_NBTN_CIRCLE;(void)TitanUnifiedInput_SubmitState(g_hid.mouse_device_id,&st,0,0,0);
        }
        if(!g_mouse_rpc_pending){
            /* Re-enumerate once per second so unplug is reflected without
             * guessing from inactivity. Read reports on the other frames. */
            if((g_mouse_presence_frame%60u)==1u)mouse_enum_poll();
            else if(g_mouse_physical_count>0u)mouse_poll();
        }
    }
#endif
}

int TitanHidInput_StorageWorkerPump(void)
{
#if defined(__EE__) || defined(_EE)
    uint64_t now;kbd_rawkey k;int got;
    if(!g_hid.keyboard_ready||g_kbd_fd<0)return 0;
    now=TitanTime_NowMilliseconds();if(now<g_kbd_next_poll_ms)return 0;g_kbd_next_poll_ms=now+TITAN_KBD_WORKER_POLL_MS;
    got=fileXioRead(g_kbd_fd,&k,2);
    if(got==2){(void)kbd_ring_push(&k);return 1;}
    if(got<0)g_hid.errors++;
#else
    (void)g_hid;
#endif
    return 0;
}

void TitanHidInput_Shutdown(void)
{
#if defined(__EE__) || defined(_EE)
    if(g_kbd_fd>=0){fileXioClose(g_kbd_fd);g_kbd_fd=-1;}g_mouse_rpc_pending=0;g_mouse_rpc_done=0;g_mouse_rpc_op=TITAN_MOUSE_RPC_NONE;memset(&g_mouse_rpc,0,sizeof(g_mouse_rpc));
#endif
    if(g_hid.keyboard_device_id)TitanUnifiedInput_Disconnect(g_hid.keyboard_device_id);
    if(g_hid.mouse_device_id)TitanUnifiedInput_Disconnect(g_hid.mouse_device_id);
    memset(&g_hid,0,sizeof(g_hid));
}

const TitanHidInputStatus *TitanHidInput_GetStatus(void){return &g_hid;}
