#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]

def text(rel): return (root/rel).read_text(encoding="utf-8", errors="replace")
def req(ok,msg):
    if not ok: raise SystemExit("wlaunchelf-native-port: FAIL: "+msg)

sdk=text("kernel/include/titan_app_sdk.h")
runtime=text("kernel/src/services/titan_app_runtime.c")
storage_h=text("kernel/src/services/titan_storage_io.h")
storage_c=text("kernel/src/services/titan_storage_io.c")
app=text("apps/wlaunchelf/src/wlaunchelf_titan.c")
make=text("apps/wlaunchelf/Makefile")
build=text("scripts/build_all.sh")
app_sdk=text("apps/wlaunchelf/include/titan_app_sdk.h")

req(app_sdk == sdk, "app SDK copy diverged from kernel public SDK")
req("TitanAppStorageEntry" in sdk and "storage_list" in sdk and "launch_elf" in sdk,
    "append-only Titan App storage/ELF ABI missing")
req("TitanStorageIo_ListDirectory" in storage_h and "TitanStorageIo_ListDirectory" in storage_c,
    "kernel storage directory enumeration missing")
req("TitanStorageIo_RemoveAny" in storage_h and "TitanStorageIo_RemoveAny" in storage_c,
    "kernel file/empty-directory removal bridge missing")
req("TitanAppRuntime_StoragePathSafe" in runtime and "TitanAppApi_StorageList" in runtime and
    "TitanAppApi_LaunchElf" in runtime, "runtime storage policy or ELF handoff bridge missing")
req('strncmp(path, "cdfs:/", 6u)' in runtime and 'strncmp(path, "hdd0:", 5u)' in runtime,
    "read-only device policy lost")
req("fileXio" not in app and "gsKit" not in app and "ExecPS2" not in app,
    "native wLaunchELF app bypasses Titan ownership")
req("WLE_COPY_CHUNK (16u * 1024u)" in app and "wl_copy_step" in app,
    "cooperative copy engine missing")
req("offsetof(TitanAppApiV1, launch_elf)" in app and "api->struct_size < required_size" in app,
    "wLaunchELF does not gate the required append-only ABI tail")
init=app[app.index("int titan_app_init("):app.index("void titan_app_update(")]
req("api->alloc(" not in init and
    "api->storage_list(" not in init and "api->storage_stat(" not in init and
    "api->storage_read(" not in init and "api->storage_write(" not in init and
    "api->launch_elf(" not in init,
    "wLaunchELF performs resource/storage work during protected init")
req("check-ee-compiler" in make and "-march=r5900 -mabi=n32 -mhard-float -msingle-float" in make,
    "wLaunchELF hardware build can regress to an unsafe compiler target")
req('make -C "$work/apps/wlaunchelf" all verify' in build and "WLAUNCHELF.pkg" in build,
    "release build does not compile/package wLaunchELF")
print("wlaunchelf-native-port-contract: OK")
