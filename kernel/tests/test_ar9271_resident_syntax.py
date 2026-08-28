#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
scanner=root/'iop/titan_ar9271/ar9271_scanner.c'
inc=root/'iop/titan_ar9271'
with tempfile.TemporaryDirectory(prefix='titan_ar9271_resident_syntax_') as td0:
    td=Path(td0)
    (td/'types.h').write_text(r'''#ifndef TYPES_H
#define TYPES_H
#include <stdint.h>
typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16; typedef int16_t s16; typedef uint32_t u32; typedef int32_t s32; typedef uint64_t u64; typedef int64_t s64;
#endif
''')
    (td/'irx.h').write_text('#define IRX_ID(a,b,c)\n#define MODULE_NO_RESIDENT_END 1\n#define MODULE_RESIDENT_END 0\n')
    (td/'loadcore.h').write_text('')
    (td/'intrman.h').write_text('int QueryIntrContext(void);\n')
    (td/'sifrpc.h').write_text(r'''typedef struct { int x; } SifRpcDataQueue_t; typedef struct { int x; } SifRpcServerData_t;
void SifSetRpcQueue(SifRpcDataQueue_t*,int); void SifRegisterRpc(SifRpcServerData_t*,unsigned int,void*(*)(int,void*,int),void*,void*,void*,SifRpcDataQueue_t*); void SifRpcLoop(SifRpcDataQueue_t*);
''')
    (td/'sysclib.h').write_text('#include <string.h>\n')
    (td/'thbase.h').write_text(r'''#include <types.h>
typedef struct { u32 lo,hi; } iop_sys_clock_t;
typedef struct { int attr; int option; int priority; int stacksize; void (*thread)(void*); } iop_thread_t;
#define TH_C 0
int CreateThread(iop_thread_t*); int StartThread(int,void*); int TerminateThread(int); int DeleteThread(int); int GetThreadId(void); void GetSystemTime(iop_sys_clock_t*); void SysClock2USec(iop_sys_clock_t*,u32*,u32*); void DelayThread(unsigned int); int SetAlarm(iop_sys_clock_t*,unsigned int(*)(void*),void*); int CancelAlarm(unsigned int(*)(void*),void*);
''')
    (td/'thsemap.h').write_text(r'''typedef struct { int attr; int option; int initial; int max; } iop_sema_t;
int CreateSema(iop_sema_t*); int SignalSema(int); int iSignalSema(int); int WaitSema(int); int PollSema(int); int DeleteSema(int);
''')
    (td/'netman.h').write_text(r'''#ifndef NETMAN_H
#define NETMAN_H
#include <types.h>
#define NETMAN_NETIF_NAME_MAX_LEN 4
#define NETMAN_NETIF_FRAME_SIZE 1514
#define NETMAN_NETIF_ETHERNET 1
#define NETMAN_NETIF_ETH_LINK_MODE_10M_FDX 2
#define NETMAN_NETIF_ETH_LINK_STATE_DOWN 0
#define NETMAN_NETIF_ETH_LINK_STATE_UP 1
#define NETMAN_NETIF_IOCTL_ETH_GET_MAC 0x1000
#define NETMAN_NETIF_IOCTL_ETH_GET_LINK_MODE 0x1001
#define NETMAN_NETIF_IOCTL_ETH_GET_STATUS 0x100A
#define NETMAN_NETIF_IOCTL_GET_LINK_STATUS 0x3000
#define NETMAN_NETIF_IOCTL_GET_TX_DROPPED_COUNT 0x3001
#define NETMAN_NETIF_IOCTL_GET_RX_DROPPED_COUNT 0x3002
struct NetManEthRuntimeStats { u32 a[8]; };
struct NetManEthStatus { u8 LinkStatus,LinkMode; struct NetManEthRuntimeStats stats; };
struct NetManNetIF { char name[4]; unsigned short flags; short id; int(*init)(void); void(*deinit)(void); void(*xmit)(void); int(*ioctl)(unsigned int,void*,unsigned int,void*,unsigned int); int EventFlagID; };
void *NetManNetProtStackAllocRxPacket(unsigned int,void**); void NetManNetProtStackFreeRxPacket(void*); void NetManNetProtStackEnQRxPacket(void*); int NetManTxPacketNext(void**); void NetManTxPacketDeQ(void); int NetManRegisterNetIF(struct NetManNetIF*); void NetManUnregisterNetIF(const char*); void NetManToggleNetIFLinkState(int,unsigned char);
#endif
''')
    (td/'usbd.h').write_text(r'''#ifndef STUB_USBD_H
#define STUB_USBD_H
#include <types.h>
#define USB_RC_OK 0
#define USB_DT_DEVICE 1
#define USB_DT_CONFIG 2
#define USB_DT_ENDPOINT 5
#define USB_ENDPOINT_XFERTYPE_MASK 3
#define USB_ENDPOINT_XFER_BULK 2
#define USB_TYPE_VENDOR 0x40
#define USB_DIR_OUT 0
#define USB_RECIP_DEVICE 0
typedef struct { u8 bLength,bDescriptorType; u16 bcdUSB; u8 bDeviceClass,bDeviceSubClass,bDeviceProtocol,bMaxPacketSize0; u16 idVendor,idProduct,bcdDevice; u8 iManufacturer,iProduct,iSerialNumber,bNumConfigurations; } UsbDeviceDescriptor;
typedef struct { u8 bLength,bDescriptorType; u16 wTotalLength; u8 bNumInterfaces,bConfigurationValue,iConfiguration,bmAttributes,bMaxPower; } UsbConfigDescriptor;
typedef struct { u8 bLength,bDescriptorType,bEndpointAddress,bmAttributes,wMaxPacketSizeLB,wMaxPacketSizeHB,bInterval; } UsbEndpointDescriptor;
typedef struct UsbDriver { void *next,*prev; const char *name; int (*probe)(int); int (*connect)(int); int (*disconnect)(int); int a,b,c,d,e; void *p; } UsbDriver;
#endif
''')
    (td/'usbd_macro.h').write_text(r'''#include <usbd.h>
void *UsbGetDeviceStaticDescriptor(int,void*,int); int UsbOpenEndpoint(int,UsbEndpointDescriptor*); int UsbOpenEndpointAligned(int,UsbEndpointDescriptor*); int UsbCloseEndpoint(int); int UsbBulkTransfer(int,void*,int,void(*)(int,int,void*),void*); int UsbControlTransfer(int,int,int,int,int,int,void*,void(*)(int,int,void*),void*); int UsbSetDeviceConfiguration(int,int,void(*)(int,int,void*),void*); int UsbRegisterDriver(UsbDriver*); int UsbUnregisterDriver(UsbDriver*);
''')
    subprocess.run([
        'cc','-std=gnu99','-fsyntax-only','-Wall','-Wextra',
        '-Werror=implicit-function-declaration','-Werror=misleading-indentation',
        f'-I{td}',f'-I{inc}',str(scanner)
    ],check=True)
print('AR9271 resident full translation-unit syntax guard: PASS')
