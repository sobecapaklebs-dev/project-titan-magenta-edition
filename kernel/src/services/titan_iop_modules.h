#ifndef PROJECT_TITAN_IOP_MODULES_H
#define PROJECT_TITAN_IOP_MODULES_H

/*
 * Single-owner declarations for the IOP modules embedded with bin2c.
 * The generated headers contain storage definitions, not just declarations,
 * so they must be included by exactly one translation unit.
 */
#if defined(__EE__) || defined(_EE)
extern unsigned char titan_iomanx_irx[];
extern unsigned int size_titan_iomanx_irx;
extern unsigned char titan_filexio_irx[];
extern unsigned int size_titan_filexio_irx;
extern unsigned char titan_usbd_irx[];
extern unsigned char titan_smsutils_irx[];
extern unsigned int size_titan_smsutils_irx;
extern unsigned char titan_usb_pademu_irx[];
extern unsigned int size_titan_usb_pademu_irx;
extern unsigned char titan_bt_pademu_irx[];
extern unsigned int size_titan_bt_pademu_irx;
extern unsigned char titan_usbkbd_irx[];
extern unsigned int size_titan_usbkbd_irx;
extern unsigned char titan_usbmouse_irx[];
extern unsigned int size_titan_usbmouse_irx;
extern unsigned int size_titan_usbd_irx;
extern unsigned char titan_usbhdfsd_irx[];
extern unsigned int size_titan_usbhdfsd_irx; /* installer legacy path */
extern unsigned char titan_bdm_irx[];
extern unsigned int size_titan_bdm_irx;
extern unsigned char titan_bdmfs_fatfs_irx[];
extern unsigned int size_titan_bdmfs_fatfs_irx;
extern unsigned char titan_usbmass_bd_irx[];
extern unsigned int size_titan_usbmass_bd_irx;
extern unsigned char titan_ps2dev9_irx[];
extern unsigned int size_titan_ps2dev9_irx;
#if defined(TITAN_HAVE_PFS_EMBEDDED)
extern unsigned char titan_ps2atad_irx[];
extern unsigned int size_titan_ps2atad_irx;
extern unsigned char titan_ps2hdd_irx[];
extern unsigned int size_titan_ps2hdd_irx;
extern unsigned char titan_ps2fs_irx[];
extern unsigned int size_titan_ps2fs_irx;
#endif
extern unsigned char titan_netman_irx[];
extern unsigned int size_titan_netman_irx;
extern unsigned char titan_smap_irx[];
extern unsigned int size_titan_smap_irx;
extern unsigned char titan_ar9271_irx[];
extern unsigned int size_titan_ar9271_irx;
#endif

#endif
