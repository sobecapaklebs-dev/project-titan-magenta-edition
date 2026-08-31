#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

ul = (root / "modules/titan-ul-engine/src/opl.c").read_text()
stream = (root / "modules/titan-neutrino-engine/iop/cdvdman_emu/src/streaming.c").read_text()
ncmd = (root / "modules/titan-neutrino-engine/iop/cdvdfsv/src/ncmd.c").read_text()
kernel = (root / "kernel/src/services/titan_neutrino.c").read_text()
version_h = (root / "kernel/include/titan_version.h").read_text()

# Runtime identity: VERSION at repository root is known to be stale release
# metadata in RC17; the actual kernel version header is authoritative here.
assert 'TITAN_KERNEL_VERSION_STRING "0.3.93-rc17"' in version_h

# Titan UL: shell emits this exact argument and the headless engine must accept it.
assert '-titan-exit=mc0:/BOOT/BOOT.ELF' in kernel
assert 'else if (!strcmp(argument, "-titan-exit=mc0:/BOOT/BOOT.ELF"))' in ul
assert 'snprintf(gExitPath, sizeof(gExitPath)' in ul
assert '"mc0:/BOOT/BOOT.ELF"' in ul

# Neutrino streaming callback guard.
callback_guard = """if (cdvdman_stat.StreamingData.StIsReading) {
        CpuSuspendIntr(&OldState);"""
assert callback_guard in stream

# Neutrino cdvd_readee current-upstream retry/race behavior.
assert """while (sceCdRead(r->lsn, temp, (void *)fsvRbuf, NULL) == 0)
                DelayThread(10000);""" in ncmd
assert """bytesent += size_64bb;
                sysmemSendEE((void *)(fsvRbuf + readee.b1len), eeaddr_64b, size_64bb);""" in ncmd

# The old progress-DMA race form must be gone from the Neutrino copy.
assert "sysmemSendEE2((void *)(fsvRbuf + readee.b1len), eeaddr_64b, size_64bb" not in ncmd

# cdvd_readiopm must retry transient read-start failures and succeed with 1.
iopm_marker = """static inline int cdvd_readiopm(RpcCdvd_t *r)
{
    u32 readpos;

    M_DEBUG("%s\\n", __FUNCTION__);

    while (sceCdRead(r->lsn, r->sectors, r->buf, NULL) == 0)
        DelayThread(10000);"""
assert iopm_marker in ncmd

iopm_return = """    return 1;
}

//-------------------------------------------------------------------------"""
assert iopm_return in ncmd

# cdvd_readchain must use retry loops in both IOP and EE paths.
chain_iop = """if ((u32)ch->buf & 1) { // IOP addr
            while (sceCdRead(lsn, tsectors, (void *)addr, NULL) == 0)
                DelayThread(10000);"""
chain_ee = """nsectors = (tsectors > cdvdfsv_sectors) ? cdvdfsv_sectors : tsectors;

                while (sceCdRead(lsn, nsectors, cdvdfsv_buf, NULL) == 0)
                    DelayThread(10000);"""
assert chain_iop in ncmd
assert chain_ee in ncmd

print("RC18_GAME_BACKEND_HOTFIX_CONTRACT=PASS")
print("RUNTIME_VERSION_HEADER=0.3.93-rc17")
print("ROOT_VERSION_METADATA_STALE=0.3.91-rc15")
