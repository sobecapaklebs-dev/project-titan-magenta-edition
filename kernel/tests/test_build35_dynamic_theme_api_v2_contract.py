#!/usr/bin/env python3
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = root.parent

def req(cond, msg):
    if not cond:
        raise SystemExit(msg)

def text(rel):
    return (root / rel).read_text(encoding='utf-8')

theme_h = text('src/services/titan_theme.h')
theme_c = text('src/services/titan_theme.c')
background = text('src/ui/titan_shell_background.c')
scene = text('src/ui/titan_shell_scene.c')
audio_h = text('src/services/titan_audio.h')
audio_c = text('src/services/titan_audio.c')
icons = text('src/ui/titan_icons.c')
update_h = text('src/services/titan_system_update.h')
make = (root / 'Makefile').read_text(encoding='utf-8')
pkg = text('tools/titan_pkg.py')
wle_pkg = (project / 'apps/wlaunchelf/tools/titan_pkg.py').read_text(encoding='utf-8')

req('#define TITAN_THEME_SHELL_API_VERSION 4u' in theme_h, 'Theme Shell API v2/v3 compatibility must remain available under v4')
req('#define TITAN_THEME_BACKGROUND_SCENE_V2 2u' in theme_h, 'scene-v2 engine id missing')
req('#define TITAN_THEME_SCENE_TRANSITION_MAX 6u' in theme_h, 'scene transition budget changed')
for token in ('TitanThemeSceneStyle', 'TitanThemeAudioStyle', 'TitanThemeIconStyle', 'TitanTheme_BuildAssetPath'):
    req(token in theme_h, f'missing Theme API v2 contract: {token}')
req('background_engine == TITAN_THEME_BACKGROUND_SCENE_V2' in theme_c and
    'TitanTheme_ValidateSceneFrame' in theme_c, 'scene-v2 runtime validation missing')
req('strstr(value, "..")' in theme_c and "strchr(value, ':')" in theme_c,
    'theme asset path traversal/device protection missing')

req('TitanVideo_DrawMediaImage16' in background, 'scene-v2 does not use RGB565 media renderer')
req('g_scene_transition[TITAN_THEME_SCENE_TRANSITION_MAX]' in background,
    'scene transition frame cache missing')
req('HOME and the top menu never render a ribbon' in background and
    'system_amount > 0.001f' in background, 'functional-only ribbon gate missing')
req('TitanShellBackground_RefreshThemeAssets' in background, 'scene asset preload/reload missing')

for page in ('TITAN_SHELL_PAGE_SETTINGS', 'TITAN_SHELL_PAGE_USER_PROFILE',
             'TITAN_SHELL_PAGE_SAVED_DATA', 'TITAN_SHELL_PAGE_LIBRARY'):
    req(page in scene[scene.index('theme_system_view_transition'):],
        f'functional scene-v2 reduction missing for {page}')
req('TitanShellScene_IsFolderScreenMode' in scene[scene.index('theme_system_view_transition'):],
    'folder options are not included in functional scene-v2 reduction')
req('g_scene.top_menu_transition' in scene and 'theme_vertical_transition' in scene,
    'scene transition is not synchronized to shell top-menu transition')
req('TITAN_AUDIO_SFX_THEME_TOP_ENTER' in scene and
    'TitanAudio_ReloadThemeAssets' in scene, 'theme top-enter audio/reload wiring missing')

req('TITAN_AUDIO_SFX_THEME_TOP_ENTER' in audio_h and 'TitanAudio_ReloadThemeAssets' in audio_h,
    'theme audio API missing')
for key in ('audio->music', 'audio->home_nav', 'audio->top_nav', 'audio->interface_nav',
            'audio->top_enter', 'audio->shutdown'):
    req(key in audio_c, f'theme audio override missing: {key}')
req('TITAN_THEME_ICON_DECORATOR_RING' in icons and 'TITAN_THEME_ICON_DECORATOR_CORNERS' in icons,
    'theme icon decorators missing')
req('theme_icons->tint_enabled' in icons, 'theme icon tint missing')

for source in (pkg, wle_pkg):
    req('validate_theme_manifest' in source and 'scene-v2 themes require shell_api=2 or shell_api=3' in source and
        'validate_theme_manifest(package_files, title_id)' in source,
        'TPKG builder/verifier lacks scene-v2 validation')
    req('scene_transition_count must be 0..6' in source, 'TPKG transition cap validation missing')
    req('theme packages cannot contain executable or microprogram files' in source,
        'TPKG declarative-only theme policy missing')

req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD 38u' in update_h, 'current system generation missing')
req('--build 36' in make and '--version 0.3.90-build36' in make and
    'build36-complex-theme-api-v3-compat7.pup' in make, 'Build 36 PUP metadata missing')
# Hardware bootstrap contract: preserve the exact seven-entry envelope used by
# BuildFix34 (payload offset 64 + 7*144 = 1072). The first payload remains the
# new kernel; the six following entries are the same existing wLaunchELF and
# Scarlet Flow assets. Do not add the three Flow variants here because that
# would change the table geometry back to the failing ten-entry envelope.
pup_start = make.index('.PHONY: pup')
pup_end = make.index('PUP_SELFTEST_OUTPUT', pup_start)
pup_block = make[pup_start:pup_end]
expected_files = (
    '--file kernel.elf=$(EE_BIN)',
    '--file TITAN/packages/WLAUNCHELF.pkg=$(WLAUNCHELF_PKG)',
    '--file TITAN/apps/TITNWLE01/wlaunchelf.tmod=../apps/wlaunchelf/build/source/wlaunchelf.tmod',
    '--file TITAN/apps/TITNWLE01/icon.rgb565=../apps/wlaunchelf/build/source/icon.rgb565',
    '--file TITAN/apps/TITNWLE01/titan_app.ini=../apps/wlaunchelf/build/installed/titan_app.ini',
    '--file TITAN/apps/TITNWLE01/UPSTREAM_NOTICE.txt=../apps/wlaunchelf/build/source/UPSTREAM_NOTICE.txt',
    '--file TITAN/themes/SCARLETFLOW/theme.cfg=../themes/ribbon-scarlet/theme.cfg',
)
req(pup_block.count('--file ') == 7 and all(item in pup_block for item in expected_files),
    'Build 36 bootstrap no longer preserves the proven seven-entry envelope')
for forbidden in ('FLOWWINE/theme.cfg', 'FLOWCYAN/theme.cfg', 'FLOWTWILIGHT/theme.cfg'):
    req(forbidden not in pup_block, f'Build36 compat7 envelope gained an extra Flow payload: {forbidden}')
req('Project-Titan-PUP-SelfTest-build37.pup' in make and '--build 37' in make,
    'Build36 forward-only updater self-test is not newer than the running build')

# Exercise TPUP v1 geometry itself with seven synthetic entries. This is the
# exact table size expected by the BuildFix34 hardware bootstrap path.
with tempfile.TemporaryDirectory(prefix='titan-pup36-compat7-') as td:
    td = Path(td)
    args = [sys.executable, str(root/'tools/titan_pup.py'), 'build',
            '--output', str(td/'compat7.pup'), '--build', '36', '--version', '0.3.90-build36']
    for index in range(7):
        payload = td/f'p{index}.bin'
        payload.write_bytes(bytes(((index * 17 + n) & 0xff) for n in range(257 + index)))
        args += ['--file', f'TITAN/update-tests/compat7-{index}.bin={payload}']
    subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    raw = (td/'compat7.pup').read_bytes()
    fmt, buildn, count, payload_off = struct.unpack_from('<IIII', raw, 4)
    req(fmt == 1 and buildn == 36 and count == 7 and payload_off == 1072,
        'compat7 TPUP geometry is not header=64/table=7x144/payload=1072')
    subprocess.run([sys.executable, str(root/'tools/titan_pup.py'), 'verify', str(td/'compat7.pup')],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

# Exercise the real builder/verifier with a synthetic scene-v2 theme, then prove
# that malformed scene frames and executable payloads are rejected at build time.
with tempfile.TemporaryDirectory(prefix='titan-theme35-') as td:
    td = Path(td)
    src = td / 'theme'
    (src / 'audio').mkdir(parents=True)
    frame_bytes = 512 * 256 * 2
    (src / 'home.rgb565').write_bytes(bytes([0x11]) * frame_bytes)
    (src / 'mid.rgb565').write_bytes(bytes([0x22]) * frame_bytes)
    (src / 'top.rgb565').write_bytes(bytes([0x33]) * frame_bytes)
    (src / 'audio' / 'transform.wav').write_bytes(b'RIFF' + bytes(40))
    (src / 'theme.cfg').write_text(
        'format=1\n'
        'id=SCNTEST35\n'
        'name=Scene Test 35\n'
        'author=Project Titan\n'
        'version=1.0.0\n'
        'shell_api=2\n'
        'background_engine=scene-v2\n'
        'scene_home=home.rgb565\n'
        'scene_top=top.rgb565\n'
        'scene_transition_count=1\n'
        'scene_transition_0=mid.rgb565\n'
        'scene_system_flow=1\n'
        'audio_top_enter=audio/transform.wav\n'
        'icon_tint_enabled=1\n'
        'icon_decorator=corners\n', encoding='utf-8')
    out = td / 'SCNTEST35.tthm'
    build_cmd = [sys.executable, str(root/'tools/titan_pkg.py'), 'build',
                 '--source', str(src), '--output', str(out), '--title-id', 'SCNTEST35',
                 '--title', 'Scene Test 35', '--version', '1.0.0', '--type', 'theme']
    subprocess.run(build_cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    subprocess.run([sys.executable, str(root/'tools/titan_pkg.py'), 'verify', str(out)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    req(out.read_bytes()[:4] == b'TPKG', 'synthetic Build35 .tthm did not produce TPKG')

    (src / 'mid.rgb565').write_bytes(b'bad')
    bad_cmd = build_cmd.copy()
    bad_cmd[bad_cmd.index(str(out))] = str(td/'bad-size.tthm')
    bad = subprocess.run(bad_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    req(bad.returncode != 0, 'malformed scene-v2 RGB565 frame was accepted')

    (src / 'mid.rgb565').write_bytes(bytes([0x22]) * frame_bytes)
    (src / 'payload.elf').write_bytes(b'not-an-elf')
    exec_cmd = build_cmd.copy()
    exec_cmd[exec_cmd.index(str(out))] = str(td/'exec.tthm')
    bad_exec = subprocess.run(exec_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    req(bad_exec.returncode != 0, 'executable payload was accepted in a .tthm')

print('build35-scene-v2-backcompat-contract: OK under Theme API v3')
