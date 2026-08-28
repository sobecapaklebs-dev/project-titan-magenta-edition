from pathlib import Path
import sys
root=Path(sys.argv[1]); k=root/'kernel'
video=(k/'src/graphics/titan_video.c').read_text()
guard=(k/'src/graphics/titan_vram_boot_guard.h').read_text()
scene=(k/'src/ui/titan_shell_background.c').read_text()
check=(root/'TITAN_BUILDFIX25_HARDWARE_TEST_CHECKLIST.md').read_text()
profile=(root/'BUILDFIX25_FIRST_HARDWARE_TEST_PROFILE.md').read_text()
readiness=(root/'PIKAVR_720P_TARGET_READINESS.md').read_text()
# The BF25 profile is retained as historical evidence of the original gate; BF30
# hardware validation has since promoted the optimized composition path.
assert 'g_video.hd_path_preference = TITAN_VIDEO_720P_PATH_OPTIMIZED' in video
assert 'TitanVideo_Set720pValidationPath' in video
assert 'TITAN_VRAM_WARNING_HEADROOM_BYTES (512u * 1024u)' in guard
assert 'TITAN_VRAM_CRITICAL_HEADROOM_BYTES (256u * 1024u)' in guard
assert 'TITAN_VRAM_RECOVERY_HEADROOM_BYTES (640u * 1024u)' in guard
assert 'TITAN_VRAM_PRESSURE_CRITICAL' in scene
assert 'PHASE A' in check and 'PHASE D' in check
assert 'R5900 CROSS-BUILD NOT EXECUTED' in profile
assert 'Do not promote Optimized' in profile
assert 'Release/default path: OPTIMIZED 960x540' in readiness
print('titan-first-hardware-profile-contract: PASS historical BF25 gate retained; BF30 optimized promotion active')
