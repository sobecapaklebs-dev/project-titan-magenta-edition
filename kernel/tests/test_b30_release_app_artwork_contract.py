#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
if root.name == 'kernel':
    root = root.parent


def text(rel: str) -> str:
    p = root / rel
    if not p.is_file():
        raise AssertionError(f'missing {rel}')
    return p.read_text(encoding='utf-8', errors='replace')


def need(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def sha(rel: str) -> str:
    return hashlib.sha256((root / rel).read_bytes()).hexdigest()

video = text('kernel/src/graphics/titan_video.c')
video_h = text('kernel/src/graphics/titan_video.h')
assets = text('kernel/src/services/titan_app_assets.c')
assets_h = text('kernel/src/services/titan_app_assets.h')
apps = text('kernel/src/services/titan_applications.c')
apps_h = text('kernel/src/services/titan_applications.h')
runtime = text('kernel/src/services/titan_app_runtime.c')
loc = text('kernel/src/services/titan_localization.c')
loc_h = text('kernel/src/services/titan_localization.h')
installer = text('kernel/src/services/titan_package_installer.c')
scene = text('kernel/src/ui/titan_shell_scene.c')
pkg = text('kernel/tools/titan_pkg.py')
pn_make = text('apps/project-nes/Makefile')
wle_make = text('apps/wlaunchelf/Makefile')
eye_make = text('ports/goldeneye/Makefile.titan')
eye_builder = text('ports/goldeneye/tools/build_private_package.py')

# 640x480 is the primary app background while 512x256 stays explicit fallback.
for token in (
    '#define TITAN_VIDEO_MEDIA_PRIMARY_WIDTH 640u',
    '#define TITAN_VIDEO_MEDIA_PRIMARY_HEIGHT 480u',
    'legacy_surface', 'primary_surface', 'TitanVideo_Rgb565ToPsm16',
):
    need(token in video + video_h, 'video/background contract missing: ' + token)
need('(!primary_surface && !legacy_surface)' in video,
     'media renderer does not reject arbitrary RGB565 surface geometry')

# Selected app artwork is bounded, worker-owned during I/O, and main-owned only
# after publication. The 640x480 buffer must never be rearmed after launch release.
for token in (
    '#define TITAN_APP_TILE_WIDTH 256u',
    '#define TITAN_APP_TILE_HEIGHT 256u',
    '#define TITAN_APP_BACKGROUND_PRIMARY_WIDTH 640u',
    '#define TITAN_APP_BACKGROUND_PRIMARY_HEIGHT 480u',
    '#define TITAN_APP_BACKGROUND_LEGACY_WIDTH 512u',
    '#define TITAN_APP_BACKGROUND_LEGACY_HEIGHT 256u',
):
    need(token in assets_h, 'app asset geometry missing: ' + token)
for token in (
    '#define TITAN_APP_SELECTED_IO_CHUNK (16u * 1024u)',
    'TitanAppAssets_Rgb565ToPsm16',
    'TitanStorageScheduler_NotifyBackgroundWork',
    'TITAN_APP_SELECTED_LOAD_READ_BACKGROUND',
    'selection_pending_ready',
    'selection_suspended',
    'g_app_assets.selection_suspended ? 0 : 1',
    'TitanAppAssets_ReleaseSelectedBackground',
):
    need(token in assets, 'selected-art ownership contract missing: ' + token)
need('return (unsigned short)(red | (green << 5u) | (blue << 10u) | 0x8000u);' in assets,
     'RGB565 -> GS PSM16 channel conversion is absent from app atlas path')

# Discovery/manifest includes independent icon/tile/background paths and Titan
# Game does not fall through to the ELF status string.
need('char tile_path[TITAN_APPLICATION_PATH_CAPACITY];' in apps_h,
     'TitanApplication lacks tile path')
need('char background_path[TITAN_APPLICATION_PATH_CAPACITY];' in apps_h,
     'TitanApplication lacks background path')
need('"tile.rgb565"' in apps and 'background_path' in apps,
     'installed app discovery does not materialize artwork paths')
need('TITAN_STR_TITAN_GAME_START' in loc_h and 'Titan Native Game / Press X to start' in loc,
     'Titan Game HOME identity still aliases the ELF Application label')

# Welcome Cross is geometry, not the old X text in a 64x44 DS2 pill.
welcome_start = scene.index('static void TitanShellScene_DrawStartupWelcome')
welcome_end = scene.index('\nstatic void TitanShellScene_DrawProfileTile', welcome_start)
welcome = scene[welcome_start:welcome_end]
need('int dualshock_cross' in welcome and 'TitanVideo_DrawCircleAlpha' in welcome,
     'DS2 welcome does not use circular face-button geometry')
need(welcome.count('TitanVideo_DrawLineAlpha') >= 2,
     'DS2 welcome Cross is not drawn geometrically')
need('DS2 welcome affordance: real Cross symbol' in welcome,
     'welcome Cross closure marker missing')

# All non-system app launch call-sites converge on one launch choreography.
need('static void TitanShellScene_BeginApplicationLaunch' in scene,
     'application launch state-machine entry missing')
need(scene.count('TitanShellScene_BeginApplicationLaunch(') >= 5,
     'not all HOME/Library/Applications app call-sites use unified launch')
need('g_scene.game_launch_application = 1;' in scene and
     'TitanAppAssets_ReleaseSelectedBackground();' in scene,
     'app launch does not reuse game launch choreography + release live-area background')
need('native_badge = g_scene.game_launch_application != 0;' in scene and
     'engine_prefix = native_game ? "TITAN NATIVE" : "TITAN";' in scene and
     'engine_word = native_game ? "GAME" : "NATIVE";' in scene and
     '"Powered by:"' in scene,
     'native classification badge is not rendered by shared engine badge component')
need('TitanAppAssets_DrawSelectedBackground(' in scene and
     scene.count('TitanShellScene_DrawApplicationLiveBackground(') >= 3 and
     '255u' in scene[scene.index('TitanShellScene_GetHomeEntry('):],
     'selected app background is not drawn through the live-background helper at full alpha')
need('if (!selected_app_background_drawn)' in scene,
     'waves/ribbons are not suppressed after selected app background succeeds')

# The hardware-frozen runtime reloads the app background at the runtime
# ownership boundary. Both primary 640x480 and legacy 512x256 sizes remain
# bounded explicitly, and the launch phase publishes background_ready before
# application init.
need('g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME' in runtime and
     'TitanAppRuntime_LoadBackground(g_app_runtime.background_path)' in runtime,
     'runtime background policy missing')
load_bg = runtime[runtime.index('case TITAN_APP_LAUNCH_LOAD_BACKGROUND:'):runtime.index('case TITAN_APP_LAUNCH_START_INIT:')]
need('g_app_runtime.background_ready =' in load_bg and
     'TitanAppRuntime_LoadBackground(g_app_runtime.background_path)' in load_bg and
     'TITAN_APP_BACKGROUND_PRIMARY_BYTES' in runtime and
     'TITAN_APP_BACKGROUND_LEGACY_BYTES' in runtime,
     'hardware-frozen runtime background ownership/geometry contract missing')

# Compatibility localization closes high-traffic legacy literals in EN/PT/ES.
for token in ('TitanLocalization_Compat', '"Start", "Iniciar", "Iniciar"',
              '"Uninstall", "Desinstalar", "Desinstalar"',
              '"Applications", "Aplicativos", "Aplicaciones"',
              '"Starting application", "Iniciando aplicativo", "Iniciando aplicación"'):
    need(token in loc + loc_h, 'localization closure missing: ' + token)

# Package/install gates recognize tile 256 and app background 640x480, while
# preserving the theme 512x256 contract.
for token in ('TILE_NAME = "tile.rgb565"', 'TILE_BYTES = 256 * 256 * 2',
              'BACKGROUND_PRIMARY_BYTES = 640 * 480 * 2',
              'BACKGROUND_LEGACY_BYTES = 512 * 256 * 2',
              'if app_type == APP_THEME'):
    need(token in pkg, 'TPKG artwork schema missing: ' + token)
for token in ('TITAN_PACKAGE_TILE_NAME "tile.rgb565"',
              'TITAN_PACKAGE_TILE_BYTES (256u * 256u * 2u)',
              'TITAN_PACKAGE_BACKGROUND_PRIMARY_BYTES (640u * 480u * 2u)',
              'tile_entry != 0 ? "tile=" : ""',
              'background_entry != 0 ? "background=" : ""'):
    need(token in installer, 'installer artwork gate/manifest missing: ' + token)

# Hardware-frozen package recipes consume the canonical resources/ artwork.
# App-local packager copies are allowed only while byte-identical to the kernel
# canonical schema; this preserves the hardware-tested recipes without schema drift.
for name, make in (('Project NES', pn_make), ('wLaunchELF', wle_make)):
    for asset in ('icon.rgb565', 'tile.rgb565', 'background.rgb565'):
        need('resources/' + asset in make, name + ' package omits canonical ' + asset)
need(sha('apps/project-nes/tools/titan_pkg.py') == sha('kernel/tools/titan_pkg.py'),
     'Project NES local packager diverges from canonical kernel schema')
need(sha('apps/wlaunchelf/tools/titan_pkg.py') == sha('kernel/tools/titan_pkg.py'),
     'wLaunchELF local packager diverges from canonical kernel schema')
need('--tile resources/tile.rgb565' in eye_make and
     '--background resources/background.rgb565' in eye_make,
     'TitanEYE private package recipe omits canonical tile/background')
for token in ('parser.add_argument("--tile"', 'parser.add_argument("--background"',
              'source / "tile.rgb565"', 'source / "background.rgb565"'):
    need(token in eye_builder, 'TitanEYE private builder missing: ' + token)

expected_assets = {
    'apps/project-nes/resources/icon.rgb565': '1a249d336de1da1f51626787f7116ce3910adbd53a63fac891282bc57bb29804',
    'apps/project-nes/resources/tile.rgb565': '957e12293a241a3e68cd6daaef3dd9fc9849168be1bc175658391b61db4199df',
    'apps/project-nes/resources/background.rgb565': '7dc536c94940d4864143ea298fb5a0b1ae03c04b7a05892d35fd75f351ab9271',
    'apps/wlaunchelf/resources/icon.rgb565': 'c4f09bddf551558e72eb6798e1233ba35e831d102d76343f542273814480ef00',
    'apps/wlaunchelf/resources/tile.rgb565': '747bef094bbc767f583d639283e5c8b68c3e7b9ecada04d81c78a789cfe54a00',
    'apps/wlaunchelf/resources/background.rgb565': '8360b87c61fce5a575febc7379c35f1b6834d9f566bcd4d5f2fb8622d5e3b64a',
    'ports/goldeneye/resources/icon.rgb565': '49b1fdde13ba58a62d7967f421de9e5606489c4e8d7d6941fb147dbecd749300',
    'ports/goldeneye/resources/tile.rgb565': 'e543a8d180caed72d15522a88514c88f291a364d867ff9a014fbca390a70b355',
    'ports/goldeneye/resources/background.rgb565': 'e134951e0a730d24b4536f893d989fac966bf05e5f36dc967d07adc3623d2c02',
}
for rel, expected in expected_assets.items():
    p = root / rel
    need(p.is_file(), 'hardware-frozen artwork missing: ' + rel)
    need(sha(rel) == expected, 'hardware-frozen artwork SHA mismatch: ' + rel)

print('b30-release-app-artwork-closure: PASS hardware-frozen 640x480 + tile256 + RGB565/PSM16 + ownership + launch/badge + Cross + localization + package recipes')
