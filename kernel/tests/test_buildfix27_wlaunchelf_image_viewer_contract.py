#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
sdk=(root/'kernel/include/titan_app_sdk.h').read_text(errors='replace')
runtime=(root/'kernel/src/services/titan_app_runtime.c').read_text(errors='replace')
image=(root/'kernel/src/services/titan_asset_image.c').read_text(errors='replace')
app=(root/'apps/wlaunchelf/src/wlaunchelf_titan.c').read_text(errors='replace')
runner=(root/'kernel/tests/run_host_tests.sh').read_text(errors='replace')
def req(ok,msg):
    if not ok: raise SystemExit('buildfix27-wlaunchelf-image-viewer: FAIL: '+msg)
req('media_present_image_file' in sdk, 'append-only app image API missing')
req('TitanAppApi_MediaPresentImageFile' in runtime and 'TITAN_APP_IMAGE_SOURCE_MAX (512u * 1024u)' in runtime, 'bounded kernel image bridge missing')
req('TitanAssetImage_DecodeMediaRgb565Fit' in runtime and 'TitanAssetImage_DecodeMediaRgb565Fit' in image, 'shared FIT decoder not used')
req('TITAN_ASSET_KIND_APP_MEDIA' in image and 'fit_inside' in image and 'offset_x' in image and 'offset_y' in image, 'letterbox FIT semantics missing')
req('wl_suffix_image' in app and 'media_present_image_file(target)' in app and 'wl_draw_image_viewer' in app, 'wLaunch image viewer missing')
req('media_draw(' in app, 'image is not drawn through Titan media surface')
req('jpeglib' not in app and 'png.h' not in app and 'gsKit' not in app and 'fileXio' not in app, 'app owns decoder/platform backend')
req('test_buildfix27_app_image_fit.py' in runner, 'FIT executable fixture missing from runner')
print('buildfix27-wlaunchelf-image-viewer: OK Titan-owned JPEG/PNG FIT media viewer')
