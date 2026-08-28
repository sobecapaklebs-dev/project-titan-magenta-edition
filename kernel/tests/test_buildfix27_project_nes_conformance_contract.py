#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def text(rel): return (root/rel).read_text(encoding='utf-8',errors='replace')
def req(ok,msg):
    if not ok: raise SystemExit('buildfix27-project-nes-conformance-contract: FAIL: '+msg)
core=text('apps/project-nes/src/project_nes_core.h')
app=text('apps/project-nes/src/project_nes.c')
test=text('apps/project-nes/tests/host_conformance_test.c')
make=text('apps/project-nes/Makefile')
report=text('docs/history/reports/PROJECT_NES_TITAN_PORT_REPORT.md')
req('ProjectNesCoreRomInfo' in core and 'ProjectNesCore_ProbeRom' in core and 'probe_rom' in core,
    'provider-neutral ROM metadata probe boundary missing')
req('PROJECT_NES_CORE_ROM_INES1' in core and 'PROJECT_NES_CORE_ROM_NES2' in core,
    'ROM format identity is not explicit')
req('PROJECT_NES_CORE_REGION_NTSC' in core and 'PROJECT_NES_CORE_REGION_PAL' in core,
    'region hint identity missing')
req('PROJECT_NES_CORE_LOAD_UNSUPPORTED_MAPPER' in core and
    'PROJECT_NES_CORE_LOAD_UNSUPPORTED_FORMAT' in core,
    'unsupported mapper/format results are not explicit')
req('TitanAppApiV1' not in core and 'titan_app_sdk.h' not in core,
    'conformance boundary regressed into Titan API ownership')
req('info.mapper != 0u' in app and 'PROJECT_NES_CORE_LOAD_UNSUPPORTED_MAPPER' in app,
    'Legacy NROM fallback no longer rejects unsupported mappers truthfully')
req('info.format != PROJECT_NES_CORE_ROM_INES1' in app,
    'Legacy NROM fallback does not reject unsupported NES2 execution')
req('pattern[0]!=533u' in test and 'pattern[2]!=534u' in test and 'total!=3200u' in test,
    '32kHz/60Hz cadence fixture missing')
req('info.mapper!=0x123u' in test and 'PROJECT_NES_CORE_REGION_PAL' in test,
    'ROM-free mapper/region fixture missing')
req('PROJECT_NES_CORE_LOAD_UNSUPPORTED_MAPPER' in test and
    'PROJECT_NES_CORE_LOAD_UNSUPPORTED_FORMAT' in test,
    'fixture does not distinguish metadata detection from execution support')
req('host_conformance_test' in make,'conformance fixture not in host suite')
req('FCEUmm remains selected' in report or 'FCEUmm' in report,
    'provider status documentation missing')
print('buildfix27-project-nes-conformance-contract: OK truthful mapper/region/SRAM/audio fixtures')
