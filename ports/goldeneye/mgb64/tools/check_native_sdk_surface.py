#!/usr/bin/env python3
"""Guard the native CMake target against avoidable SDK/libultra source use."""

import argparse
from pathlib import Path
import re
import sys


DISALLOWED_NATIVE_CMAKE_SOURCES = (
    "src/libultra/audio/auxbus.c",
    "src/libultra/audio/bnkf.c",
    "src/libultra/audio/cents2ratio.c",
    "src/libultra/audio/clean_compat.c",
    "src/libultra/audio/copy.c",
    "src/libultra/audio/cseq.c",
    "src/libultra/audio/csplayer.c",
    "src/libultra/audio/cspgetstate.c",
    "src/libultra/audio/cspplay.c",
    "src/libultra/audio/cspsetseq.c",
    "src/libultra/audio/cspsetvol.c",
    "src/libultra/audio/cspstop.c",
    "src/libultra/audio/event.c",
    "src/libultra/audio/filter.c",
    "src/libultra/audio/heapalloc.c",
    "src/libultra/audio/heapinit.c",
    "src/libultra/audio/load.c",
    "src/libultra/audio/mainbus.c",
    "src/libultra/audio/resample.c",
    "src/libultra/audio/save.c",
    "src/libultra/audio/seq.c",
    "src/libultra/audio/seqplayer.c",
    "src/libultra/audio/seqpsetbank.c",
    "src/libultra/audio/synaddplayer.c",
    "src/libultra/audio/synallocfx.c",
    "src/libultra/audio/synallocvoice.c",
    "src/libultra/audio/syndelete.c",
    "src/libultra/audio/synfreevoice.c",
    "src/libultra/audio/synsetfxmix.c",
    "src/libultra/audio/synsetpan.c",
    "src/libultra/audio/synsetpitch.c",
    "src/libultra/audio/synsetpriority.c",
    "src/libultra/audio/synsetvol.c",
    "src/libultra/audio/synstartvoice.c",
    "src/libultra/audio/synstartvoiceparam.c",
    "src/libultra/audio/synstopvoice.c",
    "src/libultra/audio/synthesizer.c",
    "src/libultra/audio/sl.c",
    "src/libultra/gu/sins.c",
    "src/libultra/gu/coss.c",
    "src/libultrare/audio/drvrNew.c",
    "src/libultrare/audio/env_wip.c",
    "src/libultrare/audio/reverb.c",
)

EXPECTED_NATIVE_LIBULTRA_AUDIO_SOURCES = ()

DISALLOWED_NATIVE_INCLUDE_DIRS = (
    "src/libultra",
    "src/libultra/gu",
    "src/libultrare/audio",
)

REMOVED_PUBLIC_LIBULTRA_FILES = (
    "src/libultra/audio/cents2ratio.c",
    "src/libultra/audio/copy.c",
    "src/libultra/audio/cseq.h",
    "src/libultra/audio/cseqp.h",
    "src/libultra/audio/cspgetstate.c",
    "src/libultra/audio/cspplay.c",
    "src/libultra/audio/cspsetseq.c",
    "src/libultra/audio/cspsetvol.c",
    "src/libultra/audio/cspstop.c",
    "src/libultra/audio/filter.c",
    "src/libultra/audio/heapalloc.c",
    "src/libultra/audio/heapinit.c",
    "src/libultra/audio/initfx.h",
    "src/libultra/audio/seq.h",
    "src/libultra/audio/seqpsetbank.c",
    "src/libultra/audio/synaddplayer.c",
    "src/libultra/audio/synallocfx.c",
    "src/libultra/audio/syndelete.c",
    "src/libultra/audio/synfreevoice.c",
    "src/libultra/audio/synsetfxmix.c",
    "src/libultra/audio/synsetpan.c",
    "src/libultra/audio/synsetpitch.c",
    "src/libultra/audio/synsetpriority.c",
    "src/libultra/audio/synsetvol.c",
    "src/libultra/audio/synstartvoice.c",
    "src/libultra/audio/synstartvoiceparam.c",
    "src/libultra/audio/synstopvoice.c",
    "src/libultra/io/pirawwrite.s",
    "src/libultrare/audio/env.s",
)

CLEAN_TOP_LEVEL_COMPAT_HEADERS = (
    "include/assert.h",
    "include/bstring.h",
    "include/limits.h",
    "include/math.h",
    "include/sgidefs.h",
    "include/stddef.h",
    "include/stdlib.h",
    "include/svr4_math.h",
)

CLEAN_PR_COMPAT_HEADERS = (
    "include/PR/R4300.h",
    "include/PR/abi.h",
    "include/PR/gbi.h",
    "include/PR/gu.h",
    "include/PR/libaudio.h",
    "include/PR/mbi.h",
    "include/PR/os.h",
    "include/PR/os_internal.h",
    "include/PR/rcp.h",
    "include/PR/region.h",
    "include/PR/ultratypes.h",
)

CLEAN_LIBULTRA_COMPAT_HEADERS = (
    "src/libultra/audio/synthInternals.h",
    "src/libultra/gu/guint.h",
    "src/libultra/io/viint.h",
)

CLEAN_LIBULTRA_COMPAT_SOURCES = (
    "src/libultra/audio/clean_compat.c",
    "src/libultra/gu/align.c",
    "src/libultra/gu/cosf.c",
    "src/libultra/gu/coss.c",
    "src/libultra/gu/lookat.c",
    "src/libultra/gu/lookatref.c",
    "src/libultra/gu/mtxutil.c",
    "src/libultra/gu/normalize.c",
    "src/libultra/gu/ortho.c",
    "src/libultra/gu/perspective.c",
    "src/libultra/gu/rotate.c",
    "src/libultra/gu/scale.c",
    "src/libultra/gu/sinf.c",
    "src/libultra/gu/sins.c",
    "src/libultra/gu/translate.c",
    "src/libultra/io/viblack.c",
)

PROPRIETARY_MARKERS = (
    "UNPUBLISHED " "PROPRIETARY",
    "unpublished " "proprietary",
    "proprietary and " "confidential",
    "Silicon " "Graphics",
    "MIPS " "Computer Systems",
    "RESTRICTED " "RIGHTS",
    "Restricted " "Rights",
    "without the prior written " "permission",
    "without the prior written " "consent",
)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=".")
    args = parser.parse_args(argv)

    root = Path(args.repo_root)
    cmake = root / "CMakeLists.txt"
    text = cmake.read_text()

    failures = []
    for source in DISALLOWED_NATIVE_CMAKE_SOURCES:
        if source in text:
            failures.append(f"native CMake target still references replaced SDK source: {source}")

    if "file(GLOB LIBULTRA_AUDIO_SOURCES" in text:
        failures.append("native CMake target uses a wildcard for libultra audio sources")

    for source in EXPECTED_NATIVE_LIBULTRA_AUDIO_SOURCES:
        if source not in text:
            failures.append(f"expected explicit native libultra audio source is missing: {source}")

    for include_dir in DISALLOWED_NATIVE_INCLUDE_DIRS:
        if re.search(rf"^\s*{re.escape(include_dir)}\s*(?:#.*)?$", text, re.MULTILINE):
            failures.append(f"native CMake target exposes unnecessary SDK include directory: {include_dir}")

    for source in REMOVED_PUBLIC_LIBULTRA_FILES:
        if (root / source).exists():
            failures.append(f"removed unbuilt SDK/libultra file reappeared: {source}")

    replacement = root / "src/platform/gu_trig.c"
    if not replacement.exists():
        failures.append("missing clean-room native GU trig replacement: src/platform/gu_trig.c")
    else:
        repl_text = replacement.read_text(errors="replace")
        if any(marker in repl_text for marker in PROPRIETARY_MARKERS):
            failures.append("native GU trig replacement contains proprietary notice text")

    audio_compat = root / "src/platform/audio_compat.c"
    if not audio_compat.exists():
        failures.append("missing clean-room native audio utility replacement: src/platform/audio_compat.c")
    else:
        compat_text = audio_compat.read_text(errors="replace")
        for symbol in (
            "alCopy",
            "alCents2Ratio",
            "alHeapInit",
            "alHeapDBAlloc",
            "alSynDelete",
            "alSynSetPriority",
            "alCSPNew",
            "alCSPGetState",
            "__CSPPostNextSeqEvent",
            "__initChanState",
            "__initFromBank",
            "__lookupSound",
            "__lookupSoundQuick",
            "__lookupVoice",
            "__mapVoice",
            "__seqpReleaseVoice",
            "__seqpStopOsc",
            "__setInstChanState",
            "__unmapVoice",
            "__voiceNeedsNoteKill",
            "__vsDelta",
            "__vsPan",
            "__vsVol",
            "alFilterNew",
            "alSeqFileNew",
            "alBnkfNew",
            "portSetBankCtlSize",
            "alAdpcmPull",
            "alRaw16Pull",
            "alLoadParam",
            "alCSeqNew",
            "alCSeqNextEvent",
            "__alCSeqNextDelta",
            "alCSeqTicksToSec",
            "alCSeqSecToTicks",
            "alCSeqGetTicks",
            "alCSeqNewMarker",
            "alCSeqSetLoc",
            "alCSeqGetLoc",
            "alSeqNew",
            "alSeqNextEvent",
            "__alSeqNextDelta",
            "alSeqTicksToSec",
            "alSeqSecToTicks",
            "alSeqNewMarker",
            "alSeqGetTicks",
            "alSeqSetLoc",
            "alSeqGetLoc",
            "alSynNew",
            "alAudioFrame",
            "__allocParam",
            "__freeParam",
            "_collectPVoices",
            "_freePVoice",
            "_timeToSamples",
            "alResamplePull",
            "alResampleParam",
            "alSynAddPlayer",
            "alSynSetVol",
            "alSynSetPan",
            "alSynSetPitch",
            "alSynSetFXMix",
            "alSynStartVoice",
            "alSynStartVoiceParams",
            "alSynStopVoice",
            "alSynFreeVoice",
            "alSynAllocFX",
            "alSeqpSetBank",
            "alCSPPlay",
            "alCSPSetSeq",
            "alCSPSetVol",
            "alCSPStop",
            "alInit",
            "alClose",
            "alLink",
            "alUnlink",
            "alAuxBusPull",
            "alAuxBusParam",
            "alMainBusPull",
            "alMainBusParam",
            "alSavePull",
            "alSaveParam",
            "alSynAllocVoice",
            "alEvtqNew",
            "alEvtqNextEvent",
            "alEvtqPostEvent",
            "alEvtqFlush",
            "alEvtqFlushType",
            "alFxNew",
            "alEnvmixerNew",
            "alLoadNew",
            "alResampleNew",
            "alAuxBusNew",
            "alMainBusNew",
            "alSaveNew",
            "alEnvmixerPull",
            "alEnvmixerParam",
            "alFxPull",
            "alFxParam",
            "alFxParamHdl",
        ):
            if symbol not in compat_text:
                failures.append(f"native audio utility replacement is missing {symbol}")
        if any(marker in compat_text for marker in PROPRIETARY_MARKERS):
            failures.append("native audio utility replacement contains proprietary notice text")

    for header in CLEAN_TOP_LEVEL_COMPAT_HEADERS:
        header_path = root / header
        if not header_path.exists():
            failures.append(f"missing clean-room top-level compatibility header: {header}")
        else:
            header_text = header_path.read_text(errors="replace")
            if any(marker in header_text for marker in PROPRIETARY_MARKERS):
                failures.append(f"top-level compatibility header contains proprietary notice text: {header}")

    for header in CLEAN_PR_COMPAT_HEADERS:
        header_path = root / header
        if not header_path.exists():
            failures.append(f"missing clean-room PR compatibility header: {header}")
        else:
            header_text = header_path.read_text(errors="replace")
            if any(marker in header_text for marker in PROPRIETARY_MARKERS):
                failures.append(f"clean-room PR compatibility header contains proprietary notice text: {header}")

    for header in CLEAN_LIBULTRA_COMPAT_HEADERS:
        header_path = root / header
        if not header_path.exists():
            failures.append(f"missing clean-room libultra compatibility header: {header}")
        else:
            header_text = header_path.read_text(errors="replace")
            if any(marker in header_text for marker in PROPRIETARY_MARKERS):
                failures.append(f"clean-room libultra compatibility header contains proprietary notice text: {header}")

    for source in CLEAN_LIBULTRA_COMPAT_SOURCES:
        source_path = root / source
        if not source_path.exists():
            failures.append(f"missing clean-room libultra compatibility source: {source}")
        else:
            source_text = source_path.read_text(errors="replace")
            if any(marker in source_text for marker in PROPRIETARY_MARKERS):
                failures.append(f"clean-room libultra compatibility source contains proprietary notice text: {source}")

    os_header = root / "include/PR/os.h"
    if not os_header.exists():
        failures.append("missing NATIVE_PORT OS compatibility shim: include/PR/os.h")
    else:
        os_text = os_header.read_text(errors="replace")
        native_prefix, separator, _ = os_text.partition("#else")
        if not separator:
            failures.append("include/PR/os.h is missing the NATIVE_PORT / SDK split")
        else:
            for marker in PROPRIETARY_MARKERS:
                if marker in native_prefix:
                    failures.append("native PR/os.h shim contains proprietary notice text")
                    break
            if "#include <platform.h>" not in native_prefix:
                failures.append("native PR/os.h shim must forward to platform.h")
            for forbidden in (
                "#define OS_IM_NONE",
                "#define osVirtualToPhysical",
                "#define K0_TO_PHYS",
                "typedef u32 OSIntMask",
            ):
                if forbidden in native_prefix:
                    failures.append(f"native PR/os.h shim overrides platform_os.h surface: {forbidden}")

    r4300 = root / "include/PR/R4300.h"
    if not r4300.exists():
        failures.append("missing R4300 compatibility header: include/PR/R4300.h")
    else:
        r4300_text = r4300.read_text(errors="replace")
        for macro in (
            "K0_TO_K1",
            "K1_TO_K0",
            "K0_TO_PHYS",
            "K1_TO_PHYS",
            "KDM_TO_PHYS",
            "PHYS_TO_K0",
            "PHYS_TO_K1",
        ):
            if f"#ifndef {macro}" not in r4300_text:
                failures.append(f"native R4300.h address macro lacks no-override guard: {macro}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: native SDK surface guard passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
