#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
c=(root/"bootloader/stage2/src/main.c").read_text(encoding="utf-8")
m=(root/"bootloader/stage2/Makefile").read_text(encoding="utf-8")
link=(root/"bootloader/stage2/linkfile").read_text(encoding="utf-8")
def req(v,msg):
    if not v: raise SystemExit("rc3-cold-iop-handoff: FAIL: "+msg)
req("TITAN_RC4_COLD_IOP_HANDOFF_R1" in c,"stage2 source marker missing")
req("TITAN_RC4_COLD_IOP_HANDOFF_R1" in m,"stage2 Makefile marker missing")
for token in (
    'SifIopReset("", 0)',"SifIopSync()","sbv_patch_enable_lmb()",
    "titan_stage2_iomanx_irx","titan_stage2_filexio_irx",
    "titan_stage2_usbd_irx","titan_stage2_bdm_irx",
    "titan_stage2_bdmfs_fatfs_irx","titan_stage2_usbmass_bd_irx",
    "TitanStage2_ProbeTarget","cold-iop-ready",
):
    req(token in c,"missing "+token)
start=c.index("static int TitanStage2_RebuildMinimalIop")
order=[
    "titan_stage2_iomanx_irx","titan_stage2_filexio_irx",
    "titan_stage2_usbd_irx","titan_stage2_bdm_irx",
    "titan_stage2_bdmfs_fatfs_irx","titan_stage2_usbmass_bd_irx",
]
pos=[c.index(x,start) for x in order]
req(pos==sorted(pos),"minimal IOP module order changed")
for token in (
    "STAGE2_IOMANX_IRX","STAGE2_FILEXIO_IRX","STAGE2_USBD_IRX",
    "STAGE2_BDM_IRX","STAGE2_BDMFS_FATFS_IRX","STAGE2_USBMASS_BD_IRX",
    "-lfileXio","-lpatches",
):
    req(token in m,"Makefile missing "+token)
req("ORIGIN = 0x00094000" in link and "ORIGIN = 0x00100000" in link,
    "low-memory envelope changed")
print("rc3-cold-iop-handoff: PASS")
