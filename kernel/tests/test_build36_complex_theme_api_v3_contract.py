#!/usr/bin/env python3
from pathlib import Path
import subprocess
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
pkg = text('tools/titan_pkg.py')
asset_tool = text('tools/titan_theme_asset.py')
wle_pkg = (project / 'apps/wlaunchelf/tools/titan_pkg.py').read_text(encoding='utf-8')
update_h = text('src/services/titan_system_update.h')
make = (root / 'Makefile').read_text(encoding='utf-8')

req('#define TITAN_THEME_SHELL_API_VERSION 4u' in theme_h, 'Theme Shell API v3 compatibility must remain available under v4')
req('#define TITAN_THEME_SCENE_IDLE_MAX 8u' in theme_h, 'per-state idle frame limit missing')
req('#define TITAN_THEME_SCENE_IDLE_TOTAL_MAX 8u' in theme_h, 'shared idle RAM budget missing')
req('#define TITAN_THEME_SCENE_IDLE_FRAME_MS_MIN 40u' in theme_h and
    '#define TITAN_THEME_SCENE_IDLE_FRAME_MS_MAX 2000u' in theme_h,
    'idle frame timing bounds missing')
req('TITAN_THEME_SCENE_IDLE_LOOP' in theme_h and 'TITAN_THEME_SCENE_IDLE_PINGPONG' in theme_h,
    'idle loop modes missing')
for token in ('home_idle_frames', 'top_idle_frames', 'home_idle_frame_count',
              'top_idle_frame_count', 'home_idle_frame_ms', 'top_idle_frame_ms',
              'home_idle_mode', 'top_idle_mode'):
    req(token in theme_h, f'missing scene-v2 API v3 field: {token}')

for key in ('scene_home_idle_count', 'scene_top_idle_count', 'scene_home_idle_ms',
            'scene_top_idle_ms', 'scene_home_idle_mode', 'scene_top_idle_mode'):
    req(key in theme_c, f'runtime parser missing {key}')
req('Loops idle scene-v2 requerem shell_api=3' in theme_c,
    'runtime does not gate idle loops on API v3')
req('idle_total > TITAN_THEME_SCENE_IDLE_TOTAL_MAX' in theme_c,
    'runtime does not enforce shared idle-frame memory budget')
req('TitanTheme_ValidateSceneFrame(root_path, theme->scene.home_idle_frames[frame_index])' in theme_c and
    'TitanTheme_ValidateSceneFrame(root_path, theme->scene.top_idle_frames[frame_index])' in theme_c,
    'runtime does not validate idle frame files')

req('#include "titan_time.h"' in background, 'idle loops are not monotonic-time driven')
req('TitanShellBackground_SelectIdleFrame' in background, 'idle frame selector missing')
req('TITAN_THEME_SCENE_STATE_HOME' in background and 'TITAN_THEME_SCENE_STATE_TOP' in background,
    'stable HOME/TOP idle states missing')
req('g_scene_idle_started_ms' in background and 'TitanTime_NowMilliseconds()' in background,
    'idle loops do not reset/use monotonic time')
req('g_scene_home_idle[TITAN_THEME_SCENE_IDLE_MAX]' in background and
    'g_scene_top_idle[TITAN_THEME_SCENE_IDLE_MAX]' in background,
    'idle frame EE-RAM caches missing')
req('mode == TITAN_THEME_SCENE_IDLE_PINGPONG' in background,
    'ping-pong idle playback missing')
req('amount <= 0.001f' in background and 'amount >= 0.999f' in background,
    'idle loops are not restricted to stable HOME/TOP states')
req('g_scene_idle_state = TITAN_THEME_SCENE_STATE_TRANSITION' in background,
    'transition path does not pause/reset idle loops')

for source in (pkg, wle_pkg):
    req('scene-v2 themes require shell_api=2 or shell_api=3' in source,
        'builder/verifier lost scene-v2 API v2 compatibility')
    req('theme manifest id must match package title-id' in source and
        'validate_theme_manifest(package_files, title_id)' in source,
        'builder/verifier does not reject package/theme ID mismatch')
    req('scene-v2 idle loops require shell_api=3' in source,
        'builder/verifier does not gate idle loops on API v3')
    req('scene idle frame budget exceeded' in source,
        'builder/verifier does not enforce idle frame budget')
    req('scene_{state}_idle_mode must be loop or pingpong' in source,
        'builder/verifier does not validate idle playback mode')
    req('scene_{state}_idle_ms' in source and '40 <= frame_ms <= 2000' in source,
        'builder/verifier does not validate idle timing')

req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD 38u' in update_h, 'current updater generation missing')
req('--build 36' in make and '--version 0.3.90-build36' in make and
    'build36-complex-theme-api-v3-compat7.pup' in make,
    'Build 36 PUP metadata missing')
req('Project-Titan-PUP-SelfTest-build37.pup' in make and '--build 37' in make,
    'Build 36 updater self-test is not forward-only')
req('SIZES = {' in asset_tool and '"scene": (512, 256)' in asset_tool and
    '"preview": (256, 128)' in asset_tool and 'struct.pack_into("<H"' in asset_tool,
    'community RGB565 scene/preview authoring helper missing or wrong-endian')

# Build and verify a real API v3 scene theme, then prove the critical invalid
# forms are rejected by both build and verify paths.
with tempfile.TemporaryDirectory(prefix='titan-theme-api3-') as td:
    td = Path(td)
    source = td / 'source'
    (source / 'scenes').mkdir(parents=True)
    frame = bytes(512 * 256 * 2)
    for name in ('home.rgb565', 'top.rgb565', 'home1.rgb565', 'top1.rgb565', 'top2.rgb565'):
        (source / 'scenes' / name).write_bytes(frame)
    (source / 'theme.cfg').write_text(
        'format=1\n'
        'id=API3TEST\n'
        'name=API3 Test\n'
        'author=Project Titan\n'
        'version=1.0.0\n'
        'shell_api=3\n'
        'background_engine=scene-v2\n'
        'scene_home=scenes/home.rgb565\n'
        'scene_top=scenes/top.rgb565\n'
        'scene_transition_count=0\n'
        'scene_home_idle_count=1\n'
        'scene_home_idle_0=scenes/home1.rgb565\n'
        'scene_home_idle_ms=160\n'
        'scene_home_idle_mode=pingpong\n'
        'scene_top_idle_count=2\n'
        'scene_top_idle_0=scenes/top1.rgb565\n'
        'scene_top_idle_1=scenes/top2.rgb565\n'
        'scene_top_idle_ms=90\n'
        'scene_top_idle_mode=loop\n',
        encoding='utf-8'
    )
    out = td / 'API3TEST.tthm'
    cmd = [sys.executable, str(root/'tools/titan_pkg.py'), 'build',
           '--source', str(source), '--output', str(out), '--title-id', 'API3TEST',
           '--title', 'API3 Test', '--version', '1.0.0', '--type', 'theme']
    built = subprocess.run(cmd, capture_output=True, text=True)
    req(built.returncode == 0, built.stdout + built.stderr)
    verified = subprocess.run([sys.executable, str(root/'tools/titan_pkg.py'), 'verify', str(out)],
                              capture_output=True, text=True)
    req(verified.returncode == 0, verified.stdout + verified.stderr)

    # API2 + idle must fail.
    cfg = (source/'theme.cfg').read_text(encoding='utf-8').replace('shell_api=3', 'shell_api=2')
    (source/'theme.cfg').write_text(cfg, encoding='utf-8')
    bad = subprocess.run(cmd, capture_output=True, text=True)
    req(bad.returncode != 0 and 'idle loops require shell_api=3' in (bad.stdout + bad.stderr),
        'API2 theme unexpectedly accepted API3 idle loops')

    # Shared idle budget > 8 must fail.
    cfg = cfg.replace('shell_api=2', 'shell_api=3')
    for i in range(2, 8):
        (source/'scenes'/f'top{i+1}.rgb565').write_bytes(frame)
    cfg = cfg.replace('scene_top_idle_count=2', 'scene_top_idle_count=8')
    cfg += ''.join(f'scene_top_idle_{i}=scenes/top{i+1}.rgb565\n' for i in range(2, 8))
    (source/'theme.cfg').write_text(cfg, encoding='utf-8')
    over = subprocess.run(cmd, capture_output=True, text=True)
    req(over.returncode != 0 and 'idle frame budget exceeded' in (over.stdout + over.stderr),
        'theme exceeding shared idle frame budget was accepted')

print('build36-complex-theme-api-v3-contract: OK')
