#if defined(__EE__) || defined(_EE)
/*
 * Single owner for bin2c-generated IOP module storage.
 * These generated headers DEFINE the arrays, so no other translation unit
 * may include them directly.
 */
#include "generated/titan_iomanx_irx.h"
#include "generated/titan_filexio_irx.h"
#include "generated/titan_usbd_irx.h"
#include "generated/titan_smsutils_irx.h"
#include "generated/titan_usb_pademu_irx.h"
#include "generated/titan_bt_pademu_irx.h"
#include "generated/titan_usbkbd_irx.h"
#include "generated/titan_usbmouse_irx.h"
#include "generated/titan_usbhdfsd_irx.h"
#include "generated/titan_bdm_irx.h"
#include "generated/titan_bdmfs_fatfs_irx.h"
#include "generated/titan_usbmass_bd_irx.h"
#include "generated/titan_ps2dev9_irx.h"
#if defined(TITAN_HAVE_PFS_EMBEDDED)
#include "generated/titan_ps2atad_irx.h"
#include "generated/titan_ps2hdd_irx.h"
#include "generated/titan_ps2fs_irx.h"
#endif
#include "generated/titan_netman_irx.h"
#include "generated/titan_smap_irx.h"
#include "generated/titan_ar9271_irx.h"
#endif
