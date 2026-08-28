#!/bin/bash
#
# prepare_ares_movement_oracle_build.sh -- Build local ares movement oracle.
#
# This clones ares into ignored local build space, applies a small N64 movement
# trace and controller-route hook, and builds the desktop frontend. It does not
# vendor ares source and does not create redistributable ROM-derived captures.
#
set -euo pipefail
cd "$(dirname "$0")/.."

ARES_REPO_URL="${ARES_REPO_URL:-https://github.com/ares-emulator/ares.git}"
ARES_REF="${ARES_REF:-91b112279ab2ce89c5fc9bff5dbb81e29af51a68}"
WORK_DIR="${MGB64_ARES_MOVEMENT_ORACLE_DIR:-$PWD/build/ares-movement-oracle}"
JOBS="${JOBS:-}"
FORCE=0
NO_BUILD=0

usage() {
    cat <<'USAGE'
Usage: tools/prepare_ares_movement_oracle_build.sh [options]

Options:
  --work-dir DIR       ignored local workspace (default: build/ares-movement-oracle)
  --ares-ref REF       ares commit/tag/branch (default: pinned known-good commit)
  --jobs N             parallel build jobs (default: host CPU count)
  --no-build           clone/patch only
  --force              delete and recreate the local ares checkout

The checkout and build are local-only. Do not commit ares source, ROM-derived
traces, screenshots, saves, or generated captures.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --work-dir) WORK_DIR="$2"; shift 2 ;;
        --ares-ref) ARES_REF="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --force) FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$JOBS" ]]; then
    if command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || true)"
    fi
    if [[ -z "$JOBS" ]] && command -v getconf >/dev/null 2>&1; then
        JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    fi
    JOBS="${JOBS:-4}"
fi
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --jobs must be a positive integer" >&2
    exit 2
fi

for required in git cmake python3; do
    if ! command -v "$required" >/dev/null 2>&1; then
        echo "FAIL: $required is required" >&2
        exit 2
    fi
done

WORK_DIR="$(python3 - "$WORK_DIR" <<'PY'
import os
import sys
print(os.path.abspath(sys.argv[1]))
PY
)"
SRC_DIR="$WORK_DIR/ares"
BUILD_DIR="$SRC_DIR/build-movement-oracle"

if [[ "$FORCE" -eq 1 && -e "$SRC_DIR" ]]; then
    rm -rf "$SRC_DIR"
fi

mkdir -p "$WORK_DIR"
if [[ ! -d "$SRC_DIR/.git" ]]; then
    git clone --depth 1 "$ARES_REPO_URL" "$SRC_DIR"
fi

SKIP_ARES_SYNC=0
if ! git -C "$SRC_DIR" diff --quiet || ! git -C "$SRC_DIR" diff --cached --quiet; then
    if ! git -C "$SRC_DIR" diff --cached --quiet; then
        echo "FAIL: local ares checkout has staged changes: $SRC_DIR" >&2
        echo "Use --force to recreate it, or clean that checkout manually." >&2
        exit 1
    fi

    while IFS= read -r dirty_path; do
        case "$dirty_path" in
            ares/n64/controller/gamepad/gamepad.cpp|\
            ares/n64/cpu/cpu.cpp|\
            ares/n64/n64.hpp|\
            ares/n64/rdp/render.cpp|\
            ares/n64/vulkan/parallel-rdp/parallel-rdp/rdp_device.cpp|\
            ares/n64/vi/vi.cpp|\
            ares/ares/node/video/screen.cpp|\
            cmake/macos/compilerconfig.cmake|\
            desktop-ui/cmake/os-macos.cmake)
                ;;
            *)
                echo "FAIL: local ares checkout has uncommitted changes: $SRC_DIR" >&2
                echo "  dirty path: $dirty_path" >&2
                echo "Use --force to recreate it, or clean that checkout manually." >&2
                exit 1
                ;;
        esac
    done < <(git -C "$SRC_DIR" diff --name-only)

    SKIP_ARES_SYNC=1
    echo "Refreshing existing instrumented ares checkout: $SRC_DIR"
fi

if [[ "$SKIP_ARES_SYNC" -eq 0 ]]; then
    git -C "$SRC_DIR" fetch origin "$ARES_REF" --depth 1 2>/dev/null || git -C "$SRC_DIR" fetch origin "$ARES_REF"
    git -C "$SRC_DIR" checkout --detach "$ARES_REF"
fi

python3 - "$SRC_DIR" <<'PY'
from pathlib import Path
import sys

src = Path(sys.argv[1])

oracle_cpp = r'''#include <n64/n64.hpp>

#include <cmath>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ares::Nintendo64 {

namespace {

struct OracleInputEvent {
  u64 start = 0;
  u64 end = 0;
  u16 buttons = 0;
  int stickX = 0;
  int stickY = 0;
  u8 phase = 0;
};

struct OracleForcePlayerEvent {
  u64 start = 0;
  u64 end = 0;
  f32 x = 0.0f;
  f32 y = 0.0f;
  f32 z = 0.0f;
  f32 yawDeg = 0.0f;
  f32 pitchDeg = 0.0f;
  f32 eyeOffset = 0.0f;
  s32 targetPad = -1;
};

struct OracleCrosshairEvent {
  u64 start = 0;
  u64 end = 0;
  f32 x = 0.0f;
  f32 y = 0.0f;
};

struct OracleRngSeedEvent {
  u64 frame = 0;
  u64 seed = 0;
  bool applied = false;
};

struct OracleDlValueCount {
  u32 value = 0;
  u32 count = 0;
};

struct OracleSetTexSample {
  u32 address = 0;
  u32 w0 = 0;
  u32 w1 = 0;
  u32 commandIndex = 0;
  u32 texturenum = 0;
  u32 type = 0;
  u32 smode = 0;
  u32 tmode = 0;
  u32 offset = 0;
  u32 shifts = 0;
  u32 shiftt = 0;
  u32 minlod = 0;
  u32 combineHi = 0;
  u32 combineLo = 0;
  u32 otherModeL = 0;
  u32 env = 0;
};

struct OracleDlMaterialState {
  u32 combineHi = 0;
  u32 combineLo = 0;
  u32 otherModeL = 0;
  u32 env = 0;
  u32 currentTextureSerial = 0;
  u32 currentImage = 0;
  u32 currentFmt = 0;
  u32 currentSiz = 0;
  u32 currentTile = 0;
  u32 currentTexWidth = 0;
  u32 currentTexHeight = 0;
};

struct OracleRdpTextureSample {
  u32 address = 0;
  u32 w0 = 0;
  u32 w1 = 0;
  u32 commandIndex = 0;
  u32 op = 0;
  u32 fmt = 0;
  u32 siz = 0;
  u32 width = 0;
  u32 image = 0;
  u32 tile = 0;
  u32 line = 0;
  u32 tmem = 0;
  u32 palette = 0;
  u32 cms = 0;
  u32 cmt = 0;
  u32 masks = 0;
  u32 maskt = 0;
  u32 shifts = 0;
  u32 shiftt = 0;
  u32 uls = 0;
  u32 ult = 0;
  u32 lrs = 0;
  u32 lrt = 0;
  u32 dxt = 0;
  u32 tlutCount = 0;
  u32 imageHashBytes64 = 0;
  u32 imageHashBytes512 = 0;
  u32 imageHashBytes2916 = 0;
  u32 imageHashBytes3024 = 0;
  u32 imageHashBytes3841 = 0;
  u32 imageHashBytes4112 = 0;
  u64 imageHash64 = 0;
  u64 imageHash512 = 0;
  u64 imageHash2916 = 0;
  u64 imageHash3024 = 0;
  u64 imageHash3841 = 0;
  u64 imageHash4112 = 0;
  u32 imageWord0 = 0;
  u32 imageWord1 = 0;
  u32 imageWord2 = 0;
  u32 imageWord3 = 0;
  u32 dumpBytes = 0;
  u32 combineHi = 0;
  u32 combineLo = 0;
  u32 otherModeL = 0;
  u32 env = 0;
};

struct OracleF3dDrawStateCount {
  u32 textureSerial = 0;
  u32 image = 0;
  u32 fmt = 0;
  u32 siz = 0;
  u32 tile = 0;
  u32 width = 0;
  u32 height = 0;
  u32 combineHi = 0;
  u32 combineLo = 0;
  u32 otherModeL = 0;
  u32 env = 0;
  u32 tri1Commands = 0;
  u32 tri2Commands = 0;
  u32 triangleCount = 0;
  u32 firstAddress = 0;
  u32 firstCommandIndex = 0;
  u32 lastAddress = 0;
  u32 lastCommandIndex = 0;
};

struct OracleRoomDlStats {
  u32 ptr = 0;
  s32 csize = 0;
  s32 usize = 0;
  u32 commands = 0;
  u32 vtx = 0;
  u32 tri1 = 0;
  u32 tri2 = 0;
  u32 dl = 0;
  u32 rdpTri = 0;
  u32 setTex = 0;
  u32 setTimg = 0;
  u32 setCombine = 0;
  u32 setEnv = 0;
  u32 setOtherMode = 0;
  u32 endDl = 0;
  u32 truncated = 0;
  u64 hash = 1469598103934665603ull;
  u64 combineHash = 1469598103934665603ull;
  OracleDlValueCount modes[8];
  u32 modeCount = 0;
  OracleDlValueCount envAlpha[8];
  u32 envAlphaCount = 0;
  OracleSetTexSample setTexSamples[16];
  u32 setTexSampleCount = 0;
  u32 targetSetTex = 0;
  OracleRdpTextureSample rdpTexSamples[192];
  u32 rdpTexSampleCount = 0;
  u32 rdpTexSampleTruncated = 0;
  OracleF3dDrawStateCount drawStates[96];
  u32 drawStateCount = 0;
  u32 drawStateTruncated = 0;
};

struct OracleActorSample {
  s32 slot = -1;
  s32 chrnum = -1;
  s32 action = -1;
  s32 alert = -1;
  s32 sleep = 0;
  s32 hidden = 0;
  s32 hiddenBits = 0;
  s32 alive = 0;
  s32 onscreen = 0;
  s32 rendered = 0;
  s32 room = -1;
  f32 distSq = 0.0f;
  f32 x = 0.0f;
  f32 y = 0.0f;
  f32 z = 0.0f;
};

struct OracleState {
  FILE* trace = nullptr;
  bool configured = false;
  bool hasInputScript = false;
  bool complete = false;
  bool gameplayTimelineStarted = false;
  u64 videoFrame = 0;
  u64 inputFrame = 0;
  const char* screenshotPath = nullptr;
  u64 screenshotFrame = 0;
  s32 screenshotGameTimer = -1;
  bool screenshotDumped = false;
  bool screenshotPending = false;
  u64 screenshotPendingFrame = 0;
  s32 screenshotPendingGlobal = 0;
  u64 gameplayOriginInputFrame = 0;
  s32 gameplayOriginGlobal = 0;
  s32 gameplayStartGlobal = 0;
  u32 gameplaySpeedFrames = 0;
  u64 frameLimit = 0;
  u64 lastInputFrame = 0;
  u64 lastInputGameplayFrame = 0;
  u16 lastInputButtons = 0;
  int lastInputStickX = 0;
  int lastInputStickY = 0;
  u32 lastInputMenuEvents = 0;
  u32 lastInputGameplayEvents = 0;
  u32 lastInputSuppressedMenuEvents = 0;
  u32 lastInputSuppressedGameplayEvents = 0;
  bool lastInputMenuClosed = false;
  OracleInputEvent events[1024];
  u32 eventCount = 0;
  OracleForcePlayerEvent forcePlayerEvents[64];
  u32 forcePlayerEventCount = 0;
  OracleCrosshairEvent crosshairEvents[64];
  u32 crosshairEventCount = 0;
  OracleRngSeedEvent rngSeedEvents[64];
  u32 rngSeedEventCount = 0;
  bool rngSeedRepeat = false;
  /* FID-0063 randomGetNext caller-PC trace (MGB64_ARES_RNG_PC_TRACE, default off).
   * One JSONL line per retail randomGetNext() ENTRY: call index, $ra (caller),
   * g_randomSeed BEFORE the step, g_GlobalTimer, and the VI frame.
   * Chain-validatable offline, but NOT via a per-line "one PRNG step apart"
   * check: the RDRAM-sampled seed lags the guest dcache writeback by a few
   * draws and catches up in bursts, so most consecutive lines repeat the same
   * seed (0 steps apart) and single-pair gaps reach into the hundreds. The
   * actual (sufficient) check is a monotone LOCK-chain walk with nonnegative
   * lag (line offset - chain position) everywhere and lag == 0 at every
   * frame-boundary catch-up point; see derivation
   * docs/fidelity/derivations/FID-0063-rng-phase.md sec 9.1.
   * The hook itself lives in cpu.cpp (dispatch PC compare against
   * mgb64OracleRngPcHookAddress); it never CALLS randomGetNext and never
   * writes guest memory. */
  FILE* rngPcTrace = nullptr;
  u64 rngPcCallIndex = 0;
  u32 rngSeedApplyCount = 0;
  int lastRngSeedEvent = -1;
  u64 lastRngSeedGameplayFrame = 0;
  u64 lastRngSeedValue = 0;
  u32 forcePlayerApplyCount = 0;
  int lastForcePlayerEvent = -1;
  u64 lastForcePlayerGameplayFrame = 0;
  int lastForcePlayerPad = -1;
  u32 lastForcePlayerStan = 0;
  s32 lastForcePlayerStanRoom = -1;
  u32 lastForcePlayerPadTable = 0;
  u32 lastForcePlayerPadAddress = 0;
  f32 lastForcePlayerFloorY = 0.0f;
  u32 crosshairApplyCount = 0;
  int lastCrosshairEvent = -1;
  u64 lastCrosshairGameplayFrame = 0;
  f32 lastCrosshairX = 0.0f;
  f32 lastCrosshairY = 0.0f;
  const char* symbolLayout = "us";
  u32 currentPlayerAddress = 0x8008a0b0;
  u32 playerPointersAddress = 0x80079ee0;
  u32 currentSetupAddress = 0x80075d00;
  u32 currentMenuAddress = 0;
  u32 currentStageToLoadAddress = 0x80048364;
  u32 clockTimerAddress = 0x80048374;
  u32 globalTimerDeltaAddress = 0x80048378;
  u32 globalTimerAddress = 0x8004837c;
  u32 cameraModeAddress = 0x80036494;
  u32 cameraAfterCinemaAddress = 0x80036498;
  u32 cameraTransitionTimerAddress = 0x800364a4;
  u32 introCameraIndexAddress = 0x800364a8;
  u32 introSwirlAddress = 0x800364ac;
  u32 selectedIntroCameraAddress = 0x800364c0;
  u32 introAnimationIndexAddress = 0x80036514;
  u32 animationTablePtrAddress = 0x80069538;
  u32 bgCurrentRoomAddress = 0x80044838;
  u32 bgRoomsDrawnCountAddress = 0x8004483c;
  u32 bgPortalDepthLimitAddress = 0x8004489c;
  u32 bgVisibleRoomsAddress = 0x8007bfa0;
  u32 bgVisibleRoomsCountAddress = 0x8007c038;
  u32 bgRenderedRoomsAddress = 0x8007ffa0;
  u32 bgPortalDepthBytesAddress = 0x800442fc;
  u32 bgPortalProjectionCacheAddress = 0x80081618;
  u32 bgPortalsPointerAddress = 0x8007ff80;
  u32 bgRoomInfoAddress = 0x80041414;
  u32 chrSlotsAddress = 0x8002cc64;
  u32 numChrSlotsAddress = 0x8002cc68;
  u32 randomSeedAddress = 0x80024460;
  u32 shatteredWindowPiecesBufferLenAddress = 0x8007a160;
  u32 ptrShatteredWindowPiecesAddress = 0x8007a164;
  u32 nextShardNumAddress = 0x80040940;
  u32 bulletImpactBufferAddress = 0x8007a154;
  u32 numImpactEntriesAddress = 0x80040808;
  u32 bgRoomFilePositionListAddress = 0x8007ff8c;
  u32 roomDataFloat2Address = 0x800413f8;
  u32 bgRoomInfoStride = 0x50;
  u32 bgRoomInfoPortalCountOffset = 0x03;
  s32 bgPortalTraceIndex = -1;
  s32 traceChrnum = -1;
  s32 targetStage = -1;
  bool closeMenuOnPlayer = true;

  enum : u32 {
    PlayerViewMode = 0x0000,
    PlayerIntroPos = 0x0004,
    PlayerIntroTarget = 0x0010,
    PlayerIntroUp = 0x001c,
    PlayerIntroFloor = 0x0028,
    PlayerCurrentModelPos = 0x0038,
    PlayerField6C = 0x006c,
    PlayerField70 = 0x0070,
    PlayerBondHealth = 0x00dc,
    PlayerBondArmour = 0x00e0,
    PlayerDamageShowTime = 0x00f4,
    PlayerHealthShowTime = 0x00f8,
    PlayerStanHeight = 0x0074,
    PlayerVvTheta = 0x0148,
    PlayerProp = 0x00a8,
    PlayerSpeedTheta = 0x014c,
    PlayerVvVerta = 0x0158,
    PlayerVvVerta360 = 0x015c,
    PlayerSpeedVerta = 0x0160,
    PlayerVvCosVerta = 0x0164,
    PlayerVvSinVerta = 0x0168,
    PlayerSpeedSideways = 0x016c,
    PlayerSpeedStrafe = 0x0170,
    PlayerSpeedForwards = 0x0174,
    PlayerSpeedBoost = 0x0178,
    PlayerSpeedMaxTime60 = 0x017c,
    PlayerBondPrevPos = 0x0408,
    PlayerCollisionPos = 0x048c,
    PlayerHeadPos = 0x04fc,
    PlayerHeadLook = 0x0508,
    PlayerHeadUp = 0x0514,
    PlayerViewX = 0x07f0,
    PlayerViewY = 0x07f2,
    PlayerViewLeft = 0x07f4,
    PlayerViewTop = 0x07f6,
    PlayerHandInvisible = 0x07f8,
    PlayerHandItem = 0x0800,
    PlayerHands = 0x0870,
    PlayerCurRoomIndex = 0x108c,
    PlayerCScreenWidth = 0x1090,
    PlayerCScreenHeight = 0x1094,
    PlayerCScreenLeft = 0x1098,
    PlayerCScreenTop = 0x109c,
    PlayerCPerspFovy = 0x10a4,
    PlayerCPerspAspect = 0x10a8,
    PlayerHandPending = 0x2a44,
    PlayerSpeedGo = 0x2a4c,
    PlayerHandLockModel = 0x2a50,
    PlayerGunposAmplitude = 0x0fc0,
    PlayerCrosshairAngle = 0x0fe8,
    PlayerFieldFFC = 0x0ffc,
    PlayerGunAzimuthAngle = 0x1004,
    PlayerGunAzimuthTurning = 0x1008,
    PlayerGunSync = 0x106c,
    PlayerSyncChange = 0x1070,
    PlayerSyncCount = 0x1074,
    PlayerSyncOffset = 0x1078,
    PlayerField107C = 0x107c,
    PlayerField1080 = 0x1080,
    PlayerField10E0 = 0x10e0,

    HandStride = 0x03a8,
    HandWeaponNum = 0x0000,
    HandWeaponNumWatchmenu = 0x0004,
    HandWeaponFiringStatus = 0x000c,
    HandWeaponHoldTime = 0x0010,
    HandWhenDetonatingMinesIs0 = 0x0024,
    HandWeaponCurrentAnimation = 0x0028,
    HandWeaponAmmoInMagazine = 0x002c,
    HandWeaponNextWeapon = 0x003c,
    HandWorldMatrix = 0x0268,
    HandRootMatrix = 0x02a8,
    HandMuzzle = 0x02e8,
    HandDepth = 0x02f4,
    HandEmbeddedModel = 0x02f8,
    HandField930 = 0x00c0,
    HandField934 = 0x00c4,
    HandField938 = 0x00c8,
    HandField954 = 0x00e4,
    HandField958 = 0x00e8,
    HandField95C = 0x00ec,
    HandBlendpos = 0x0108,
    HandCurblendpos = 0x0198,
    HandDampt = 0x019c,
    HandBlendscale = 0x01a0,
    HandBlendscale1 = 0x01a4,
    HandSideflag = 0x01a8,
    HandWeaponThetaDisplacement = 0x01ac,
    HandWeaponVertaDisplacement = 0x01b0,
    HandFieldA28 = 0x01b8,
    HandFieldA2C = 0x01bc,
    HandFieldA30 = 0x01c0,
    HandFieldA38 = 0x01c8,
    HandFieldA3C = 0x01cc,
    HandFieldA40 = 0x01d0,
    HandFieldA84 = 0x0214,
    HandFieldA88 = 0x0218,

    ModelObj = 0x0008,
    ModelScale = 0x0014,
    ModelFileNumMatrices = 0x000e,

    PlayerWatchPauseTime = 0x01c0,
    PlayerWatchAnimationState = 0x01c8,
    PlayerOutsideWatchMenu = 0x01cc,
    PlayerOpenCloseSoloWatchMenu = 0x01d0,
    PlayerPausingFlag = 0x0200,
    PlayerField3B8 = 0x03b8,
    PlayerColourScreenRed = 0x03d0,
    PlayerColourScreenGreen = 0x03d4,
    PlayerColourScreenBlue = 0x03d8,
    PlayerColourScreenFrac = 0x03dc,
    PlayerColourFadeTimeMax60 = 0x03e4,
    PlayerField488 = 0x0488,
    PlayerHealthDamageType = 0x29b8,
    PlayerDamageType = 0x29d4,
    PlayerActualHealth = 0x2a3c,
    PlayerActualArmour = 0x2a40,

    PropPos = 0x0008,
    PropStan = 0x0014,
    PropFlags = 0x0001,
    PropChr = 0x0004,
    PropType = 0x0000,
    PropRooms = 0x002c,

    BgRoomDataStride = 0x0018,
    BgRoomDataPos = 0x000c,

    BulletImpactStride = 0x0050,
    BulletImpactRoom = 0x0000,
    BulletImpactType = 0x0002,
    BulletImpactVertices = 0x0008,
    BulletImpactProp = 0x0048,
    BulletImpactModelPos = 0x004c,
    BulletImpactClear = 0x004d,
    BulletImpactMax = 100,

    VtxStride = 0x0010,
    VtxOb = 0x0000,
    VtxTc = 0x0008,
    VtxCn = 0x000c,

    ChrChrnum = 0x0000,
    ChrAccuracyrating = 0x0002,
    ChrFirecount = 0x0004,
    ChrActiontype = 0x0007,
    ChrSleep = 0x0008,
    ChrHidden = 0x0012,
    ChrChrflags = 0x0014,
    ChrProp = 0x0018,
    ChrModel = 0x001c,
    ChrField20 = 0x0020,
    ChrActPatrolPath = 0x002c,
    ChrActPatrolNextstep = 0x0030,
    ChrActPatrolForward = 0x0034,
    ChrActPatrolWayMode = 0x0038,
    ChrActPatrolWayAge = 0x0060,
    ChrActPatrolWaySegDone = 0x0070,
    ChrActPatrolWaySegTotal = 0x0074,
    ChrActPatrolLastVisible60 = 0x0078,
    ChrActPatrolSpeed = 0x007c,
    ChrGround = 0x00ac,
    ChrPrevpos = 0x00bc,
    ChrDamage = 0x00fc,
    ChrMaxDamage = 0x0100,
    ChrAilist = 0x0104,
    ChrAioffset = 0x0108,
    ChrAireturnlist = 0x010a,
    ChrAlertness = 0x010d,
    ChrPadpreset1 = 0x0112,
    ChrChrpreset1 = 0x0114,
    ChrShotbondsum = 0x013c,
    ChrLastseetarget60 = 0x00d4,   /* combat oracle target_visible (FID-0032) */
    ChrStride = 0x01dc,

    /* StandTile (combat oracle floor{}) */
    StandTileId = 0x0000,   /* u32:24 */
    StandTileRoom = 0x0003, /* u8 */
    StandTileMid = 0x0004,  /* s16 header-mid halfword */

    ModelRenderPos = 0x000c,
    ModelAnim = 0x0020,
    ModelGunhand = 0x0024,
    ModelAnimLooping = 0x0026,
    ModelFrame = 0x0028,
    ModelEndFrame = 0x003c,
    ModelSpeed = 0x0040,

    ModelAnimationFrameCount = 0x0004,

    CollisionCurrentTile = 0x0000,
    CollisionPosition = 0x0004,
    CollisionTheta = 0x0010,
    CollisionFloor = 0x001c,
    CollisionRadius = 0x0028,
    CollisionCameraPos = 0x002c,
    CollisionAppliedView = 0x0038,
    CollisionAppliedUp = 0x0044,
    CollisionPortalTile = 0x0050,

    CurrentSetupPropDefs = 0x000c,
    CurrentSetupPads = 0x0018,
    CurrentSetupBoundPads = 0x001c,
    PadRecordSize = 0x002c,
    BoundPadRecordSize = 0x0044,
    PadRecordStan = 0x0028,

    ObjectState = 0x0002,
    ObjectType = 0x0003,
    ObjectObj = 0x0004,
    ObjectPad = 0x0006,
    ObjectFlags = 0x0008,
    ObjectFlags2 = 0x000c,
    ObjectProp = 0x0010,
    ObjectModel = 0x0014,
    ObjectRuntimePos = 0x0058,
    ObjectRuntimeFlags = 0x0064,
    ObjectMaxDamage = 0x0070,
    ObjectDamage = 0x0074,

    RoomInfoRendered = 0x0000,
    RoomInfoNeighbor = 0x0001,
    RoomInfoModelLoaded = 0x0002,
    RoomInfoPortalCount = 0x0003,
    RoomInfoPointPtr = 0x0004,
    RoomInfoPrimaryPtr = 0x0008,
    RoomInfoSecondaryPtr = 0x000c,
    RoomInfoCSizePoint = 0x0010,
    RoomInfoCSizePrimary = 0x0014,
    RoomInfoCSizeSecondary = 0x0018,
    RoomInfoUSizePoint = 0x001c,
    RoomInfoUSizePrimary = 0x0020,
    RoomInfoUSizeSecondary = 0x0024,
    RoomInfoLoadedMask = 0x0034,
  };

  enum : s32 {
    MenuUnknown = -9999,
    MenuRunStage = 11,
  };

  enum : u8 {
    PhaseGameplay = 0,
    PhaseMenu = 1,
    PhaseGlobal = 2,
  };

  auto configure() -> void {
    if(configured) return;
    configured = true;
    configureSymbols();

    const char* limitEnv = getenv("MGB64_ARES_FRAME_LIMIT");
    if(!limitEnv || !*limitEnv) limitEnv = getenv("MGB64_ARES_EXIT_AFTER_FRAMES");
    if(limitEnv && *limitEnv) {
      char* end = nullptr;
      auto parsed = strtoull(limitEnv, &end, 10);
      if(end && *end == 0) frameLimit = parsed;
    }

    screenshotPath = getenv("MGB64_ARES_SCREENSHOT_PATH");
    if(!screenshotPath || !*screenshotPath) screenshotPath = getenv("MGB64_ARES_SCREENSHOT");
    if(!screenshotPath || !*screenshotPath) screenshotPath = nullptr;
    if(screenshotPath) {
      screenshotFrame = frameLimit ? frameLimit : 1;
      if(auto screenshotFrameEnv = getenv("MGB64_ARES_SCREENSHOT_FRAME")) {
        if(*screenshotFrameEnv) {
          char* end = nullptr;
          auto parsed = strtoull(screenshotFrameEnv, &end, 10);
          if(end && *end == 0) screenshotFrame = parsed;
        }
      }
      if(auto screenshotGameTimerEnv = getenv("MGB64_ARES_SCREENSHOT_GAME_TIMER")) {
        if(*screenshotGameTimerEnv) {
          char* end = nullptr;
          auto parsed = strtol(screenshotGameTimerEnv, &end, 10);
          if(end && *end == 0 && parsed >= 0 && parsed <= 2147483647L) {
            screenshotGameTimer = (s32)parsed;
          }
        }
      }
    }

    if(auto script = getenv("MGB64_ARES_INPUT_SCRIPT")) {
      if(*script) loadInputScript(script);
    }
    loadForcePlayerEvents();
    loadCrosshairEvents();
    loadRngSeedEvents();

    /* FID-0063: randomGetNext caller-PC trace. Enabled ONLY when
     * MGB64_ARES_RNG_PC_TRACE names a writable path; the hook address defaults
     * to the retail US randomGetNext entry (VRAM 0x7000A450, src/random.c
     * GLOBAL_ASM "glabel randomGetNext") and can be overridden with
     * MGB64_ARES_RNG_PC_HOOK for other layouts. While disabled the global
     * stays 0 and the cpu.cpp dispatch compare never fires the call. */
    if(auto rngPcPath = getenv("MGB64_ARES_RNG_PC_TRACE")) {
      if(*rngPcPath) {
        u32 hookAddress = 0x7000A450;
        if(auto hookEnv = getenv("MGB64_ARES_RNG_PC_HOOK")) {
          if(*hookEnv) {
            char* end = nullptr;
            u64 parsed = strtoull(hookEnv, &end, 0);
            if(end && *end == 0 && parsed != 0) hookAddress = (u32)parsed;
          }
        }
        rngPcTrace = fopen(rngPcPath, "wb");
        if(!rngPcTrace) {
          fprintf(stderr, "mgb64 oracle: failed to open RNG PC trace %s\n", rngPcPath);
        } else {
          mgb64OracleRngPcHookAddress = hookAddress;
          fprintf(stderr,
            "mgb64 oracle: RNG PC trace hook=0x%08X -> %s\n",
            hookAddress, rngPcPath);
        }
      }
    }

    auto path = getenv("MGB64_ARES_ORACLE_TRACE");
    if(!path || !*path) path = getenv("MGB64_ARES_MOVEMENT_TRACE");
    if(!path || !*path) {
      complete = true;
      return;
    }

    trace = fopen(path, "wb");
    if(!trace) {
      fprintf(stderr, "mgb64 oracle: failed to open %s\n", path);
      complete = true;
      return;
    }

    setvbuf(trace, nullptr, _IONBF, 0);
    fprintf(stderr, "mgb64 oracle: writing JSONL to %s\n", path);
  }

  static auto writePpmPixel(FILE* out, u32 color) -> void {
    fputc((u8)((color >> 16) & 0xff), out);
    fputc((u8)((color >> 8) & 0xff), out);
    fputc((u8)(color & 0xff), out);
  }

  auto dumpScreen(Node::Video::Screen screen) -> void {
    configure();
    if(!screenshotPath || screenshotDumped || screenshotPending) return;
    if(screenshotGameTimer >= 0) {
      s32 currentStage = readS32(currentStageToLoadAddress);
      s32 global = readS32(globalTimerAddress);
      if(!targetStageMatches(currentStage) || global < screenshotGameTimer) return;
    } else if(screenshotFrame && videoFrame < screenshotFrame) {
      return;
    }

    screenshotPending = true;
    screenshotPendingFrame = videoFrame;
    screenshotPendingGlobal = readS32(globalTimerAddress);
  }

  auto dumpPresentedScreen(Node::Video::Screen screen, const u32* pixels, u32 pitch, u32 width, u32 height) -> void {
    configure();
    if(!screenshotPath || screenshotDumped || !screenshotPending) return;
    if(!pixels || pitch == 0 || width == 0 || height == 0) return;

    u32 outWidth = screen ? screen->width() : 0;
    u32 outHeight = screen ? screen->height() : 0;
    if(outWidth == 0) outWidth = width;
    if(outHeight == 0) outHeight = height;
    if(outWidth == 0 || outHeight == 0) return;

    FILE* out = fopen(screenshotPath, "wb");
    if(!out) {
      fprintf(stderr, "mgb64 oracle: failed to open screenshot %s\n", screenshotPath);
      screenshotDumped = true;
      screenshotPending = false;
      return;
    }

    fprintf(out, "P6\n%u %u\n255\n", outWidth, outHeight);
    for(u32 y = 0; y < outHeight; y++) {
      u32 sourceY = (u32)(((u64)y * (u64)height) / (u64)outHeight);
      if(sourceY >= height) sourceY = height - 1;
      for(u32 x = 0; x < outWidth; x++) {
        u32 sourceX = (u32)(((u64)x * (u64)width) / (u64)outWidth);
        if(sourceX >= width) sourceX = width - 1;
        writePpmPixel(out, pixels[(u64)sourceY * pitch + sourceX]);
      }
    }
    fclose(out);
    screenshotDumped = true;
    screenshotPending = false;
    fprintf(stderr, "mgb64 oracle: wrote presented screenshot to %s at frame %llu global %d source=%ux%u output=%ux%u\n",
      screenshotPath, (unsigned long long)screenshotPendingFrame, screenshotPendingGlobal,
      width, height, outWidth, outHeight);
  }

  auto loadInputScript(const char* path) -> void {
    FILE* file = fopen(path, "rb");
    if(!file) {
      fprintf(stderr, "mgb64 oracle: failed to open input script %s\n", path);
      return;
    }

    hasInputScript = true;
    char line[512];
    while(fgets(line, sizeof(line), file) && eventCount < 1024) {
      char* cursor = line;
      while(*cursor == ' ' || *cursor == '\t') cursor++;
      if(*cursor == '\0' || *cursor == '\n' || *cursor == '#') continue;

      unsigned long long start = 0;
      unsigned long long length = 0;
      unsigned int buttons = 0;
      int stickX = 0;
      int stickY = 0;
      char phase[32] = "gameplay";
      int parsed = sscanf(cursor, "%llu %llu %x %d %d %31s", &start, &length, &buttons, &stickX, &stickY, phase);
      if(parsed < 5) {
        fprintf(stderr, "mgb64 oracle: ignored malformed input line: %s", line);
        continue;
      }
      if(start == 0 || length == 0) continue;

      auto& event = events[eventCount++];
      event.start = start;
      event.end = start + length - 1;
      event.buttons = buttons & 0xffff;
      event.stickX = clampAxis(stickX);
      event.stickY = clampAxis(stickY);
      if(parsed >= 6 && (strcmp(phase, "menu") == 0 || strcmp(phase, "boot") == 0 || strcmp(phase, "frontend") == 0)) {
        event.phase = PhaseMenu;
      } else if(parsed >= 6 && (strcmp(phase, "global") == 0 || strcmp(phase, "stage-global") == 0 || strcmp(phase, "stage_global") == 0)) {
        event.phase = PhaseGlobal;
      } else {
        event.phase = PhaseGameplay;
      }
    }

    fclose(file);
    fprintf(stderr, "mgb64 oracle: loaded %u input event(s) from %s\n", eventCount, path);
  }

  auto addForcePlayerEvent(u64 start, u64 length, f32 x, f32 y, f32 z, f32 yawDeg, f32 pitchDeg, f32 eyeOffset, s32 targetPad) -> void {
    if(start == 0 || length == 0 || forcePlayerEventCount >= 64) return;
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if(!std::isfinite(yawDeg)) yawDeg = 0.0f;
    if(!std::isfinite(pitchDeg)) pitchDeg = 0.0f;
    if(!std::isfinite(eyeOffset) || eyeOffset < 0.0f || eyeOffset > 300.0f) eyeOffset = 0.0f;
    if(targetPad < -1) targetPad = -1;

    auto& event = forcePlayerEvents[forcePlayerEventCount++];
    event.start = start;
    event.end = start + length - 1;
    event.x = x;
    event.y = y;
    event.z = z;
    event.yawDeg = yawDeg;
    event.pitchDeg = pitchDeg;
    event.eyeOffset = eyeOffset;
    event.targetPad = targetPad;
  }

  auto parseForcePlayerSpec(const char* spec) -> bool {
    if(!spec || !*spec) return false;

    char* end = nullptr;
    u64 start = strtoull(spec, &end, 10);
    if(end == spec || *end != ':') return false;
    spec = end + 1;

    u64 length = strtoull(spec, &end, 10);
    if(end == spec || *end != ':') return false;
    spec = end + 1;

    f32 x = strtof(spec, &end);
    if(end == spec || *end != ':') return false;
    spec = end + 1;

    f32 y = strtof(spec, &end);
    if(end == spec || *end != ':') return false;
    spec = end + 1;

    f32 z = strtof(spec, &end);
    if(end == spec) return false;
    spec = end;

    f32 yawDeg = 0.0f;
    f32 pitchDeg = 0.0f;
    f32 eyeOffset = 0.0f;
    s32 targetPad = -1;

    if(*spec == ':') {
      spec++;
      yawDeg = strtof(spec, &end);
      if(end == spec) return false;
      spec = end;
    }
    if(*spec == ':') {
      spec++;
      pitchDeg = strtof(spec, &end);
      if(end == spec) return false;
      spec = end;
    }
    if(*spec == ':') {
      spec++;
      eyeOffset = strtof(spec, &end);
      if(end == spec) return false;
      spec = end;
    }
    if(*spec == ':') {
      spec++;
      long parsedPad = strtol(spec, &end, 10);
      if(end == spec) return false;
      if(parsedPad >= -1 && parsedPad <= 32767) targetPad = (s32)parsedPad;
      spec = end;
    }
    if(*spec != '\0') return false;

    addForcePlayerEvent(start, length, x, y, z, yawDeg, pitchDeg, eyeOffset, targetPad);
    return true;
  }

  auto loadForcePlayerEvents() -> void {
    if(auto script = getenv("MGB64_ARES_FORCE_PLAYER_SCRIPT")) {
      while(*script && forcePlayerEventCount < 64) {
        while(*script == ' ' || *script == '\t' || *script == ',' || *script == ';') script++;
        if(!*script) break;

        char item[256];
        u32 index = 0;
        while(script[index] && script[index] != ' ' && script[index] != '\t' && script[index] != ',' && script[index] != ';') {
          if(index + 1 < sizeof(item)) item[index] = script[index];
          index++;
        }
        u32 count = index < sizeof(item) ? index : (u32)sizeof(item) - 1;
        item[count] = '\0';
        if(!parseForcePlayerSpec(item)) {
          fprintf(stderr, "mgb64 oracle: ignored malformed force-player spec: %s\n", item);
        }
        script += index;
      }
    }

    const char* frameEnv = getenv("MGB64_ARES_FORCE_PLAYER_FRAME");
    const char* xEnv = getenv("MGB64_ARES_FORCE_PLAYER_X");
    const char* yEnv = getenv("MGB64_ARES_FORCE_PLAYER_Y");
    const char* zEnv = getenv("MGB64_ARES_FORCE_PLAYER_Z");
    if(frameEnv && *frameEnv && xEnv && *xEnv && yEnv && *yEnv && zEnv && *zEnv && forcePlayerEventCount < 64) {
      char* end = nullptr;
      u64 start = strtoull(frameEnv, &end, 10);
      if(end && *end == 0) {
        u64 length = 1;
        if(auto lenEnv = getenv("MGB64_ARES_FORCE_PLAYER_LEN")) {
          if(*lenEnv) {
            char* lenEnd = nullptr;
            u64 parsed = strtoull(lenEnv, &lenEnd, 10);
            if(lenEnd && *lenEnd == 0 && parsed > 0) length = parsed;
          }
        }
        f32 x = strtof(xEnv, nullptr);
        f32 y = strtof(yEnv, nullptr);
        f32 z = strtof(zEnv, nullptr);
        f32 yawDeg = getenv("MGB64_ARES_FORCE_PLAYER_YAW_DEG") ? strtof(getenv("MGB64_ARES_FORCE_PLAYER_YAW_DEG"), nullptr) : 0.0f;
        f32 pitchDeg = getenv("MGB64_ARES_FORCE_PLAYER_PITCH_DEG") ? strtof(getenv("MGB64_ARES_FORCE_PLAYER_PITCH_DEG"), nullptr) : 0.0f;
        f32 eyeOffset = getenv("MGB64_ARES_FORCE_PLAYER_EYE_OFFSET") ? strtof(getenv("MGB64_ARES_FORCE_PLAYER_EYE_OFFSET"), nullptr) : 0.0f;
        s32 targetPad = getenv("MGB64_ARES_FORCE_PLAYER_PAD") ? strtol(getenv("MGB64_ARES_FORCE_PLAYER_PAD"), nullptr, 10) : -1;
        addForcePlayerEvent(start, length, x, y, z, yawDeg, pitchDeg, eyeOffset, targetPad);
      }
    }

    if(forcePlayerEventCount) {
      fprintf(stderr, "mgb64 oracle: loaded %u force-player event(s)\n", forcePlayerEventCount);
    }
  }

  auto addCrosshairEvent(u64 start, u64 length, f32 x, f32 y) -> void {
    if(start == 0 || length == 0 || crosshairEventCount >= 64) return;
    if(!std::isfinite(x) || !std::isfinite(y)) return;

    auto& event = crosshairEvents[crosshairEventCount++];
    event.start = start;
    event.end = start + length - 1;
    event.x = x;
    event.y = y;
  }

  auto parseCrosshairSpec(const char* spec) -> bool {
    if(!spec || !*spec) return false;

    char* end = nullptr;
    u64 start = strtoull(spec, &end, 10);
    if(end == spec) return false;

    u64 length = 1;
    if(*end == '-') {
      spec = end + 1;
      u64 final = strtoull(spec, &end, 10);
      if(end == spec || final < start || *end != ':') return false;
      length = final - start + 1;
      spec = end + 1;
    } else if(*end == ':') {
      spec = end + 1;

      u32 remainingColons = 0;
      for(const char* cursor = spec; *cursor; cursor++) {
        if(*cursor == ':') remainingColons++;
      }

      if(remainingColons >= 2) {
        u64 parsedLength = strtoull(spec, &end, 10);
        if(end == spec || parsedLength == 0 || *end != ':') return false;
        length = parsedLength;
        spec = end + 1;
      }
    } else {
      return false;
    }

    f32 x = strtof(spec, &end);
    if(end == spec || *end != ':') return false;
    spec = end + 1;

    f32 y = strtof(spec, &end);
    if(end == spec || *end != '\0') return false;

    addCrosshairEvent(start, length, x, y);
    return true;
  }

  auto loadCrosshairEvents() -> void {
    if(auto script = getenv("MGB64_ARES_CROSSHAIR_SCRIPT")) {
      while(*script && crosshairEventCount < 64) {
        while(*script == ' ' || *script == '\t' || *script == ',' || *script == ';') script++;
        if(!*script) break;

        char item[128];
        u32 index = 0;
        while(script[index] && script[index] != ' ' && script[index] != '\t' && script[index] != ',' && script[index] != ';') {
          if(index + 1 < sizeof(item)) item[index] = script[index];
          index++;
        }
        u32 count = index < sizeof(item) ? index : (u32)sizeof(item) - 1;
        item[count] = '\0';
        if(!parseCrosshairSpec(item)) {
          fprintf(stderr, "mgb64 oracle: ignored malformed crosshair spec: %s\n", item);
        }
        script += index;
      }
    }

    const char* frameEnv = getenv("MGB64_ARES_CROSSHAIR_FRAME");
    const char* xEnv = getenv("MGB64_ARES_CROSSHAIR_X");
    const char* yEnv = getenv("MGB64_ARES_CROSSHAIR_Y");
    if(frameEnv && *frameEnv && xEnv && *xEnv && yEnv && *yEnv && crosshairEventCount < 64) {
      char* end = nullptr;
      u64 start = strtoull(frameEnv, &end, 10);
      if(end && *end == 0) {
        u64 length = 1;
        if(auto lenEnv = getenv("MGB64_ARES_CROSSHAIR_LEN")) {
          if(*lenEnv) {
            char* lenEnd = nullptr;
            u64 parsed = strtoull(lenEnv, &lenEnd, 10);
            if(lenEnd && *lenEnd == 0 && parsed > 0) length = parsed;
          }
        }
        addCrosshairEvent(start, length, strtof(xEnv, nullptr), strtof(yEnv, nullptr));
      }
    }

    if(crosshairEventCount) {
      fprintf(stderr, "mgb64 oracle: loaded %u crosshair event(s)\n", crosshairEventCount);
    }
  }

  auto addRngSeedEvent(u64 frame, u64 seed) -> void {
    if(frame == 0 || rngSeedEventCount >= 64) return;
    auto& event = rngSeedEvents[rngSeedEventCount++];
    event.frame = frame;
    event.seed = seed;
    event.applied = false;
  }

  auto parseRngSeedSpec(const char* spec) -> bool {
    if(!spec || !*spec) return false;

    char* end = nullptr;
    u64 frame = strtoull(spec, &end, 10);
    if(end == spec || *end != ':') return false;
    spec = end + 1;

    u64 seed = strtoull(spec, &end, 0);
    if(end == spec || *end != '\0') return false;

    addRngSeedEvent(frame, seed);
    return true;
  }

  auto loadRngSeedEvents() -> void {
    if(auto repeatEnv = getenv("MGB64_ARES_RNG_SEED_REPEAT")) {
      rngSeedRepeat = repeatEnv[0] != '\0' && repeatEnv[0] != '0';
    }

    if(auto script = getenv("MGB64_ARES_RNG_SEED_SCRIPT")) {
      while(*script && rngSeedEventCount < 64) {
        while(*script == ' ' || *script == '\t' || *script == ',' || *script == ';') script++;
        if(!*script) break;

        char item[128];
        u32 index = 0;
        while(script[index] && script[index] != ' ' && script[index] != '\t' && script[index] != ',' && script[index] != ';') {
          if(index + 1 < sizeof(item)) item[index] = script[index];
          index++;
        }
        u32 count = index < sizeof(item) ? index : (u32)sizeof(item) - 1;
        item[count] = '\0';
        if(!parseRngSeedSpec(item)) {
          fprintf(stderr, "mgb64 oracle: ignored malformed RNG seed spec: %s\n", item);
        }
        script += index;
      }
    }

    const char* frameEnv = getenv("MGB64_ARES_RNG_SEED_FRAME");
    const char* seedEnv = getenv("MGB64_ARES_RNG_SEED");
    if(rngSeedEventCount == 0 && frameEnv && *frameEnv && seedEnv && *seedEnv) {
      char* frameEnd = nullptr;
      char* seedEnd = nullptr;
      u64 frame = strtoull(frameEnv, &frameEnd, 10);
      u64 seed = strtoull(seedEnv, &seedEnd, 0);
      if(frameEnd && *frameEnd == 0 && seedEnd && *seedEnd == 0) {
        addRngSeedEvent(frame, seed);
      }
    }

    if(rngSeedEventCount) {
      fprintf(stderr,
        "mgb64 oracle: loaded %u RNG seed event(s)%s\n",
        rngSeedEventCount,
        rngSeedRepeat ? " with repeat" : "");
    }
  }

  static auto clampAxis(int value) -> int {
    if(value > 80) return 80;
    if(value < -80) return -80;
    return value;
  }

  auto configureSymbols() -> void {
    if(auto layout = getenv("MGB64_ARES_SYMBOL_LAYOUT")) {
      if(strcmp(layout, "jp") == 0 || strcmp(layout, "jp_bugfix") == 0) {
        symbolLayout = layout;
        currentPlayerAddress = 0x80078bc0;
        playerPointersAddress = 0x800789f0;
        currentSetupAddress = 0x80064c40;
        currentMenuAddress = 0;
        currentStageToLoadAddress = 0x80040fe4;
        clockTimerAddress = 0x80040ff4;
        globalTimerAddress = 0x80040ffc;
        globalTimerDeltaAddress = 0x80041004;
        cameraModeAddress = 0;
        cameraAfterCinemaAddress = 0;
        cameraTransitionTimerAddress = 0;
        introCameraIndexAddress = 0;
        introSwirlAddress = 0;
        selectedIntroCameraAddress = 0;
        introAnimationIndexAddress = 0;
        animationTablePtrAddress = 0x80058478;
        bgCurrentRoomAddress = 0;
        bgRoomsDrawnCountAddress = 0;
        bgPortalDepthLimitAddress = 0;
        bgVisibleRoomsAddress = 0;
        bgVisibleRoomsCountAddress = 0;
        bgRenderedRoomsAddress = 0;
        bgPortalDepthBytesAddress = 0;
        bgPortalProjectionCacheAddress = 0;
        bgPortalsPointerAddress = 0;
        bgRoomInfoAddress = 0;
        chrSlotsAddress = 0;
        numChrSlotsAddress = 0;
        randomSeedAddress = 0;
        shatteredWindowPiecesBufferLenAddress = 0;
        ptrShatteredWindowPiecesAddress = 0;
        nextShardNumAddress = 0;
        bulletImpactBufferAddress = 0;
        numImpactEntriesAddress = 0;
        bgRoomFilePositionListAddress = 0;
        roomDataFloat2Address = 0;
        bgRoomInfoStride = 0x50;
        bgRoomInfoPortalCountOffset = 0x03;
        bgPortalTraceIndex = -1;
      } else if(strcmp(layout, "us") == 0 || strcmp(layout, "usa") == 0) {
        symbolLayout = "us";
      } else {
        symbolLayout = layout;
      }
    }

    currentPlayerAddress = parseAddressEnv("MGB64_ARES_CURRENT_PLAYER", currentPlayerAddress);
    playerPointersAddress = parseAddressEnv("MGB64_ARES_PLAYER_POINTERS", playerPointersAddress);
    currentSetupAddress = parseAddressEnv("MGB64_ARES_CURRENT_SETUP", currentSetupAddress);
    currentMenuAddress = parseAddressEnv("MGB64_ARES_CURRENT_MENU", currentMenuAddress);
    currentStageToLoadAddress = parseAddressEnv("MGB64_ARES_CURRENT_STAGE", currentStageToLoadAddress);
    clockTimerAddress = parseAddressEnv("MGB64_ARES_CLOCK_TIMER", clockTimerAddress);
    globalTimerDeltaAddress = parseAddressEnv("MGB64_ARES_GLOBAL_TIMER_DELTA", globalTimerDeltaAddress);
    globalTimerAddress = parseAddressEnv("MGB64_ARES_GLOBAL_TIMER", globalTimerAddress);
    cameraModeAddress = parseAddressEnv("MGB64_ARES_CAMERA_MODE", cameraModeAddress);
    cameraAfterCinemaAddress = parseAddressEnv("MGB64_ARES_CAMERA_AFTER_CINEMA", cameraAfterCinemaAddress);
    cameraTransitionTimerAddress = parseAddressEnv("MGB64_ARES_CAMERA_TRANSITION_TIMER", cameraTransitionTimerAddress);
    introCameraIndexAddress = parseAddressEnv("MGB64_ARES_INTRO_CAMERA_INDEX", introCameraIndexAddress);
    introSwirlAddress = parseAddressEnv("MGB64_ARES_INTRO_SWIRL", introSwirlAddress);
    selectedIntroCameraAddress = parseAddressEnv("MGB64_ARES_SELECTED_INTRO_CAMERA", selectedIntroCameraAddress);
    introAnimationIndexAddress = parseAddressEnv("MGB64_ARES_INTRO_ANIM_INDEX", introAnimationIndexAddress);
    animationTablePtrAddress = parseAddressEnv("MGB64_ARES_ANIMATION_TABLE_PTR", animationTablePtrAddress);
    bgCurrentRoomAddress = parseAddressEnv("MGB64_ARES_BG_CURRENT_ROOM", bgCurrentRoomAddress);
    bgRoomsDrawnCountAddress = parseAddressEnv("MGB64_ARES_BG_ROOMS_DRAWN_COUNT", bgRoomsDrawnCountAddress);
    bgPortalDepthLimitAddress = parseAddressEnv("MGB64_ARES_BG_PORTAL_DEPTH_LIMIT", bgPortalDepthLimitAddress);
    bgVisibleRoomsAddress = parseAddressEnv("MGB64_ARES_BG_VISIBLE_ROOMS", bgVisibleRoomsAddress);
    bgVisibleRoomsCountAddress = parseAddressEnv("MGB64_ARES_BG_VISIBLE_ROOMS_COUNT", bgVisibleRoomsCountAddress);
    bgRenderedRoomsAddress = parseAddressEnv("MGB64_ARES_BG_RENDERED_ROOMS", bgRenderedRoomsAddress);
    bgPortalDepthBytesAddress = parseAddressEnv("MGB64_ARES_BG_PORTAL_DEPTH_BYTES", bgPortalDepthBytesAddress);
    bgPortalProjectionCacheAddress = parseAddressEnv("MGB64_ARES_BG_PORTAL_PROJECTION_CACHE", bgPortalProjectionCacheAddress);
    bgPortalsPointerAddress = parseAddressEnv("MGB64_ARES_BG_PORTALS_POINTER", bgPortalsPointerAddress);
    bgRoomInfoAddress = parseAddressEnv("MGB64_ARES_BG_ROOM_INFO", bgRoomInfoAddress);
    chrSlotsAddress = parseAddressEnv("MGB64_ARES_CHR_SLOTS", chrSlotsAddress);
    numChrSlotsAddress = parseAddressEnv("MGB64_ARES_NUM_CHR_SLOTS", numChrSlotsAddress);
    randomSeedAddress = parseAddressEnv("MGB64_ARES_RANDOM_SEED", randomSeedAddress);
    shatteredWindowPiecesBufferLenAddress = parseAddressEnv("MGB64_ARES_SHATTERED_WINDOW_LEN", shatteredWindowPiecesBufferLenAddress);
    ptrShatteredWindowPiecesAddress = parseAddressEnv("MGB64_ARES_SHATTERED_WINDOW_PTR", ptrShatteredWindowPiecesAddress);
    nextShardNumAddress = parseAddressEnv("MGB64_ARES_NEXT_SHARD_NUM", nextShardNumAddress);
    bulletImpactBufferAddress = parseAddressEnv("MGB64_ARES_BULLET_IMPACT_BUFFER", bulletImpactBufferAddress);
    numImpactEntriesAddress = parseAddressEnv("MGB64_ARES_NUM_IMPACT_ENTRIES", numImpactEntriesAddress);
    bgRoomFilePositionListAddress = parseAddressEnv("MGB64_ARES_BG_ROOM_FILEPOSITION_LIST", bgRoomFilePositionListAddress);
    bgRoomFilePositionListAddress = parseAddressEnv("MGB64_ARES_BG_ROOM_POS_LIST", bgRoomFilePositionListAddress);
    roomDataFloat2Address = parseAddressEnv("MGB64_ARES_ROOM_DATA_FLOAT2", roomDataFloat2Address);
    bgRoomInfoStride = parseU32Env("MGB64_ARES_BG_ROOM_INFO_STRIDE", bgRoomInfoStride);
    bgRoomInfoPortalCountOffset = parseU32Env("MGB64_ARES_BG_ROOM_INFO_PORTAL_COUNT_OFFSET", bgRoomInfoPortalCountOffset);
    bgPortalTraceIndex = parseS32Env("MGB64_ARES_BG_PORTAL_TRACE_INDEX", bgPortalTraceIndex);
    traceChrnum = parseS32Env("MGB64_ARES_TRACE_CHRNUM", traceChrnum);
    targetStage = parseS32Env("MGB64_ARES_TARGET_STAGE", targetStage);
    gameplayStartGlobal = parseS32Env("MGB64_ARES_GAMEPLAY_START_GLOBAL", gameplayStartGlobal);
    gameplaySpeedFrames = parseU32Env("MGB64_ARES_GAMEPLAY_SPEEDFRAMES", gameplaySpeedFrames);
    closeMenuOnPlayer = parseBoolEnv("MGB64_ARES_CLOSE_MENU_ON_PLAYER", closeMenuOnPlayer);
  }

  static auto parseAddressEnv(const char* name, u32 fallback) -> u32 {
    auto value = getenv(name);
    if(!value || !*value) return fallback;
    char* end = nullptr;
    auto parsed = strtoul(value, &end, 0);
    if(end && *end == 0) return (u32)parsed;
    fprintf(stderr, "mgb64 oracle: ignored invalid %s=%s\n", name, value);
    return fallback;
  }

  static auto parseS32Env(const char* name, s32 fallback) -> s32 {
    auto value = getenv(name);
    if(!value || !*value) return fallback;
    char* end = nullptr;
    auto parsed = strtol(value, &end, 0);
    if(end && *end == 0) return (s32)parsed;
    fprintf(stderr, "mgb64 oracle: ignored invalid %s=%s\n", name, value);
    return fallback;
  }

  static auto parseU32Env(const char* name, u32 fallback) -> u32 {
    auto value = getenv(name);
    if(!value || !*value) return fallback;
    char* end = nullptr;
    auto parsed = strtoul(value, &end, 0);
    if(end && *end == 0) return (u32)parsed;
    fprintf(stderr, "mgb64 oracle: ignored invalid %s=%s\n", name, value);
    return fallback;
  }

  static auto parseBoolEnv(const char* name, bool fallback) -> bool {
    auto value = getenv(name);
    if(!value || !*value) return fallback;
    if(strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 || strcmp(value, "on") == 0) return true;
    if(strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "no") == 0 || strcmp(value, "off") == 0) return false;
    fprintf(stderr, "mgb64 oracle: ignored invalid %s=%s\n", name, value);
    return fallback;
  }

  static auto hashU32(u64 hash, u32 value) -> u64 {
    hash ^= (u64)value;
    hash *= 1099511628211ull;
    return hash;
  }

  static auto hashByte(u64 hash, u8 value) -> u64 {
    hash ^= (u64)value;
    hash *= 1099511628211ull;
    return hash;
  }

  auto hashRdramBytes(u32 address, u32 length, u32& validBytes) -> u64 {
    u64 hash = 1469598103934665603ull;
    validBytes = 0;
    if(address == 0 || length == 0) return hash;
    for(u32 index = 0; index < length; index++) {
      if(!validRdram(address + index, 1)) break;
      hash = hashByte(hash, readU8(address + index));
      validBytes++;
    }
    return hash;
  }

  static auto addValueCount(OracleDlValueCount* values, u32& count, u32 capacity, u32 value) -> void {
    for(u32 index = 0; index < count; index++) {
      if(values[index].value == value) {
        values[index].count++;
        return;
      }
    }
    if(count < capacity) {
      values[count].value = value;
      values[count].count = 1;
      count++;
    }
  }

  static auto traceSetTexTargetTexnum() -> u32 {
    static bool configured = false;
    static u32 value = 654;
    if(!configured) {
      value = parseU32Env("MGB64_ARES_TRACE_SETTEX_TEXNUM", 654);
      configured = true;
    }
    return value;
  }

  static auto traceSetTexSampleAll() -> bool {
    static bool configured = false;
    static bool value = false;
    if(!configured) {
      value = parseBoolEnv("MGB64_ARES_TRACE_SETTEX_ALL", false);
      configured = true;
    }
    return value;
  }

  static auto rdpTextureOpName(u32 op) -> const char* {
    switch(op) {
    case 0xfd: return "settimg";
    case 0xf5: return "settile";
    case 0xf4: return "loadtile";
    case 0xf3: return "loadblock";
    case 0xf2: return "settilesize";
    case 0xf0: return "loadtlut";
    default: return "unknown";
    }
  }

  static auto appendText(char* out, size_t outSize, size_t& used, const char* format, ...) -> void {
    if(used >= outSize) return;
    va_list args;
    va_start(args, format);
    int written = std::vsnprintf(out + used, outSize - used, format, args);
    va_end(args);
    if(written < 0 || (size_t)written >= outSize - used) {
      used = outSize;
      if(outSize > 0) out[outSize - 1] = '\0';
      return;
    }
    used += (size_t)written;
  }

  auto scriptedController(n32 data) -> n32 {
    configure();
    inputFrame++;
    if(!hasInputScript) return data;

    u16 buttons = 0;
    int stickX = 0;
    int stickY = 0;
    const bool hasPlayer = hasGameplayPlayer();
    const u64 gameplayFrame = currentGameplayFrame();
    const bool menuClosed = stockMenuInputClosed(hasPlayer);
    const s32 currentStage = readS32(currentStageToLoadAddress);
    u32 menuEvents = 0;
    u32 gameplayEvents = 0;
    u32 suppressedMenuEvents = 0;
    u32 suppressedGameplayEvents = 0;

    if(forcePlayerEventCount != 0 && gameplayFrame != 0) {
      const char* forcePlayerSource = nullptr;
      u32 forcePlayer = selectedPlayer(forcePlayerSource);
      if(targetStageMatches(currentStage) && validPlayerPointer(forcePlayer)) {
        u32 prop = readU32(forcePlayer + PlayerProp);
        applyForcePlayerEvents(forcePlayer, prop, gameplayFrame);
        applyCrosshairEvents(forcePlayer, gameplayFrame);
      }
    } else if(crosshairEventCount != 0 && gameplayFrame != 0) {
      const char* crosshairPlayerSource = nullptr;
      u32 crosshairPlayer = selectedPlayer(crosshairPlayerSource);
      if(targetStageMatches(currentStage) && validPlayerPointer(crosshairPlayer)) {
        applyCrosshairEvents(crosshairPlayer, gameplayFrame);
      }
    }

    for(u32 n = 0; n < eventCount; n++) {
      const auto& event = events[n];
      if(event.phase == PhaseMenu) {
        if(inputFrame < event.start || inputFrame > event.end) continue;
        if(menuClosed) {
          suppressedMenuEvents++;
          continue;
        }
        menuEvents++;
      } else if(event.phase == PhaseGlobal) {
        const s32 global = readS32(globalTimerAddress);
        if(!hasPlayer || !targetStageMatches(currentStage) || global < (s32)event.start || global > (s32)event.end) {
          if(!hasPlayer) suppressedGameplayEvents++;
          continue;
        }
        gameplayEvents++;
      } else {
        if(gameplayFrame == 0 || gameplayFrame < event.start || gameplayFrame > event.end) {
          if(gameplayFrame == 0) suppressedGameplayEvents++;
          continue;
        }
        gameplayEvents++;
      }
      buttons |= event.buttons;
      stickX += event.stickX;
      stickY += event.stickY;
    }

    stickX = clampAxis(stickX);
    stickY = clampAxis(stickY);
    lastInputFrame = inputFrame;
    lastInputGameplayFrame = gameplayFrame;
    lastInputButtons = buttons;
    lastInputStickX = stickX;
    lastInputStickY = stickY;
    lastInputMenuEvents = menuEvents;
    lastInputGameplayEvents = gameplayEvents;
    lastInputSuppressedMenuEvents = suppressedMenuEvents;
    lastInputSuppressedGameplayEvents = suppressedGameplayEvents;
    lastInputMenuClosed = menuClosed;

    n32 out = ((u32)buttons << 16) | ((u32)(stickX & 0xff) << 8) | (u32)(stickY & 0xff);
    return out;
  }

  auto validRdram(u32 address, u32 bytes = 4) -> bool {
    if(address == 0) return false;
    u32 offset = physicalOffset(address);
    return offset < rdram.ram.size && bytes <= rdram.ram.size - offset;
  }

  auto physicalOffset(u32 address) -> u32 {
    if((address & 0xe0000000) == 0x80000000 || (address & 0xe0000000) == 0xa0000000) {
      return address & 0x1fffffff;
    }
    return address;
  }

  auto readU32(u32 address) -> u32 {
    if(!validRdram(address, 4)) return 0;
    return rdram.ram.read<Word>(physicalOffset(address), RBusDevice::ARES_DEBUGGER);
  }

  auto readU64(u32 address) -> u64 {
    if(!validRdram(address, 8)) return 0;
    return ((u64)readU32(address) << 32) | (u64)readU32(address + 4);
  }

  auto readU8(u32 address) -> u8 {
    u32 aligned = address & ~3u;
    if(!validRdram(aligned, 4)) return 0;
    u32 value = readU32(aligned);
    return (u8)((value >> ((3 - (address & 3u)) * 8)) & 0xff);
  }

  auto readS8(u32 address) -> s8 {
    return (s8)readU8(address);
  }

  auto readU16(u32 address) -> u16 {
    return (u16)(((u16)readU8(address) << 8) | readU8(address + 1));
  }

  auto readS16(u32 address) -> s16 {
    return (s16)readU16(address);
  }

  auto readS32(u32 address) -> s32 {
    return (s32)readU32(address);
  }

  auto animationTableBase() -> u32 {
    if(animationTablePtrAddress == 0 || !validRdram(animationTablePtrAddress, 4)) return 0;
    u32 base = readU32(animationTablePtrAddress);
    return validRdram(base, 0x14) ? base : 0;
  }

  auto logicalAnimationOffset(u32 value) -> u32 {
    u32 base = animationTableBase();
    if(base != 0 && value >= base) {
      u32 offset = value - base;
      if(offset < 0x20000u) return offset;
    }
    return value;
  }

  auto readF32(u32 address) -> f32 {
    u32 bits = readU32(address);
    f32 value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    if(!std::isfinite(value)) return 0.0f;
    return value;
  }

  auto readN64MtxElem(u32 matrix, int row, int col) -> f32 {
    if(row < 0 || row >= 4 || col < 0 || col >= 4 || !validRdram(matrix, 64)) return 0.0f;
    u32 pair = (u32)row * 2u + (u32)col / 2u;
    u32 shift = (col & 1) ? 0u : 16u;
    u32 hiWord = readU32(matrix + pair * 4u);
    u32 loWord = readU32(matrix + (pair + 8u) * 4u);
    s16 hi = (s16)((hiWord >> shift) & 0xffffu);
    u16 lo = (u16)((loWord >> shift) & 0xffffu);
    s32 fixed = ((s32)hi << 16) | (s32)lo;
    return (f32)fixed / 65535.0f;
  }

  auto formatHandViewmodelJson(char* out, size_t outSize, u32 hand, u32 player) -> void {
    if(!out || outSize == 0) return;
    out[0] = '\0';

    u32 model = hand + HandEmbeddedModel;
    u32 modelObj = validRdram(model + ModelObj, 4) ? readU32(model + ModelObj) : 0;
    u32 renderPos = validRdram(model + ModelRenderPos, 4) ? readU32(model + ModelRenderPos) : 0;
    s32 numMatrices = (modelObj != 0 && validRdram(modelObj + ModelFileNumMatrices, 2))
      ? readS16(modelObj + ModelFileNumMatrices)
      : -1;
    f32 modelScale = validRdram(model + ModelScale, 4) ? readF32(model + ModelScale) : 0.0f;

    f32 rootX = readF32(hand + HandRootMatrix + 0x30);
    f32 rootY = readF32(hand + HandRootMatrix + 0x34);
    f32 rootZ = readF32(hand + HandRootMatrix + 0x38);
    f32 worldX = readF32(hand + HandWorldMatrix + 0x30);
    f32 worldY = readF32(hand + HandWorldMatrix + 0x34);
    f32 worldZ = readF32(hand + HandWorldMatrix + 0x38);
    f32 muzzleX = readF32(hand + HandMuzzle + 0);
    f32 muzzleY = readF32(hand + HandMuzzle + 4);
    f32 muzzleZ = readF32(hand + HandMuzzle + 8);
    f32 depth = readF32(hand + HandDepth);
    f32 smoothX = readF32(hand + HandField930);
    f32 smoothY = readF32(hand + HandField934);
    f32 smoothZ = readF32(hand + HandField938);
    f32 accumX = readF32(hand + HandField954);
    f32 accumY = readF32(hand + HandField958);
    f32 accumZ = readF32(hand + HandField95C);
    f32 baseX = readF32(hand + HandFieldA28);
    f32 baseY = (f32)readS32(hand + HandFieldA2C);
    f32 baseZ = readF32(hand + HandFieldA30);
    f32 targetX = readF32(hand + HandFieldA38);
    f32 targetY = readF32(hand + HandFieldA3C);
    f32 targetZ = readF32(hand + HandFieldA40);
    f32 recoil = readF32(hand + HandFieldA84);
    f32 boltRecoil = readF32(hand + HandFieldA88);
    s32 curblendpos = readS32(hand + HandCurblendpos);
    s32 sideflag = readS32(hand + HandSideflag);
    f32 dampt = readF32(hand + HandDampt);
    f32 blendscale = readF32(hand + HandBlendscale);
    f32 blendscale1 = readF32(hand + HandBlendscale1);
    f32 weaponThetaDisplacement = readF32(hand + HandWeaponThetaDisplacement);
    f32 weaponVertaDisplacement = readF32(hand + HandWeaponVertaDisplacement);
    f32 blendpos[4][3] = {};
    for(u32 slot = 0; slot < 4; slot++) {
      u32 pos = hand + HandBlendpos + slot * 12u;
      blendpos[slot][0] = readF32(pos + 0);
      blendpos[slot][1] = readF32(pos + 4);
      blendpos[slot][2] = readF32(pos + 8);
    }
    f32 screenX = validRdram(player + PlayerFieldFFC, 8) ? readF32(player + PlayerFieldFFC + 0) : 0.0f;
    f32 screenY = validRdram(player + PlayerFieldFFC, 8) ? readF32(player + PlayerFieldFFC + 4) : 0.0f;
    f32 gunAzimuth = validRdram(player + PlayerGunAzimuthAngle, 4) ? readF32(player + PlayerGunAzimuthAngle) : 0.0f;
    f32 gunTurning = validRdram(player + PlayerGunAzimuthTurning, 4) ? readF32(player + PlayerGunAzimuthTurning) : 0.0f;
    f32 gunposAmplitude = validRdram(player + PlayerGunposAmplitude, 4) ? readF32(player + PlayerGunposAmplitude) : 0.0f;
    f32 playerField107C = validRdram(player + PlayerField107C, 4) ? readF32(player + PlayerField107C) : 0.0f;
    f32 playerField1080 = validRdram(player + PlayerField1080, 4) ? readF32(player + PlayerField1080) : 0.0f;
    f32 playerGunSync = validRdram(player + PlayerGunSync, 4) ? readF32(player + PlayerGunSync) : 0.0f;
    f32 playerSyncChange = validRdram(player + PlayerSyncChange, 4) ? readF32(player + PlayerSyncChange) : 0.0f;
    f32 playerSyncCount = validRdram(player + PlayerSyncCount, 4) ? readF32(player + PlayerSyncCount) : 0.0f;
    f32 playerSyncOffset = validRdram(player + PlayerSyncOffset, 4) ? (f32)readS32(player + PlayerSyncOffset) : 0.0f;
    f32 cScreenWidth = validRdram(player + PlayerCScreenWidth, 4) ? readF32(player + PlayerCScreenWidth) : 320.0f;
    f32 cScreenHeight = validRdram(player + PlayerCScreenHeight, 4) ? readF32(player + PlayerCScreenHeight) : 240.0f;
    f32 cScreenLeft = validRdram(player + PlayerCScreenLeft, 4) ? readF32(player + PlayerCScreenLeft) : 0.0f;
    f32 cScreenTop = validRdram(player + PlayerCScreenTop, 4) ? readF32(player + PlayerCScreenTop) : 0.0f;
    f32 cPerspFovy = validRdram(player + PlayerCPerspFovy, 4) ? readF32(player + PlayerCPerspFovy) : 60.0f;
    f32 cPerspAspect = validRdram(player + PlayerCPerspAspect, 4) ? readF32(player + PlayerCPerspAspect) : 1.333333f;
    if(cScreenWidth <= 0.0f) cScreenWidth = 320.0f;
    if(cScreenHeight <= 0.0f) cScreenHeight = 240.0f;
    if(cPerspFovy <= 0.0f) cPerspFovy = 60.0f;
    if(cPerspAspect <= 0.0f) cPerspAspect = cScreenWidth / cScreenHeight;

    f32 renderX = 0.0f;
    f32 renderY = 0.0f;
    f32 renderZ = 0.0f;
    f32 renderB00 = 0.0f;
    f32 renderB11 = 0.0f;
    f32 renderB22 = 0.0f;
    s32 renderValid = validRdram(renderPos, 64) ? 1 : 0;
    if(renderValid) {
      renderX = readN64MtxElem(renderPos, 3, 0);
      renderY = readN64MtxElem(renderPos, 3, 1);
      renderZ = readN64MtxElem(renderPos, 3, 2);
      renderB00 = readN64MtxElem(renderPos, 0, 0);
      renderB11 = readN64MtxElem(renderPos, 1, 1);
      renderB22 = readN64MtxElem(renderPos, 2, 2);
    }
    auto projectViewPoint = [&](f32 x, f32 y, f32 z, f32 fovy, f32& sx, f32& sy) -> s32 {
      f32 depth = -z;
      if(cScreenWidth <= 0.0f || cScreenHeight <= 0.0f || cPerspAspect <= 0.0f ||
         fovy <= 0.0f || depth <= 0.001f) {
        sx = 0.0f;
        sy = 0.0f;
        return 0;
      }
      f32 focal = 1.0f / std::tan(fovy * 0.00872664626f);
      if(!std::isfinite(focal)) {
        sx = 0.0f;
        sy = 0.0f;
        return 0;
      }
      f32 ndcX = (x * focal / cPerspAspect) / depth;
      f32 ndcY = (y * focal) / depth;
      sx = cScreenLeft + (ndcX + 1.0f) * 0.5f * cScreenWidth;
      sy = cScreenTop + (1.0f - ndcY) * 0.5f * cScreenHeight;
      return 1;
    };

    char renderMtxJson[4096];
    size_t renderMtxUsed = 0;
    bool renderMtxOverflow = false;
    auto appendRenderMtx = [&](const char* fmt, ...) -> bool {
      if(renderMtxOverflow) return false;
      if(renderMtxUsed >= sizeof(renderMtxJson)) {
        std::snprintf(renderMtxJson, sizeof(renderMtxJson), "\"mtx\":{\"overflow\":1}");
        renderMtxUsed = std::strlen(renderMtxJson);
        renderMtxOverflow = true;
        return false;
      }
      va_list ap;
      va_start(ap, fmt);
      int wrote = std::vsnprintf(renderMtxJson + renderMtxUsed,
        sizeof(renderMtxJson) - renderMtxUsed, fmt, ap);
      va_end(ap);
      if(wrote < 0 || (size_t)wrote >= sizeof(renderMtxJson) - renderMtxUsed) {
        std::snprintf(renderMtxJson, sizeof(renderMtxJson), "\"mtx\":{\"overflow\":1}");
        renderMtxUsed = std::strlen(renderMtxJson);
        renderMtxOverflow = true;
        return false;
      }
      renderMtxUsed += (size_t)wrote;
      return true;
    };

    s32 sampledMatrices = numMatrices;
    if(sampledMatrices < 0 || !renderValid) sampledMatrices = 0;
    if(sampledMatrices > 8) sampledMatrices = 8;
    appendRenderMtx("\"mtx\":{\"count\":%d,\"sampled\":%d,"
      "\"projection\":{\"fovy\":%.4f,\"aspect\":%.6f,\"view\":[%.2f,%.2f,%.2f,%.2f]},"
      "\"pos\":[",
      numMatrices, sampledMatrices, cPerspFovy, cPerspAspect,
      cScreenLeft, cScreenTop, cScreenWidth, cScreenHeight);
    for(s32 slot = 0; slot < sampledMatrices; slot++) {
      u32 mtx = renderPos + (u32)slot * 64u;
      appendRenderMtx("%s[%.2f,%.2f,%.2f]", slot == 0 ? "" : ",",
        readN64MtxElem(mtx, 3, 0), readN64MtxElem(mtx, 3, 1), readN64MtxElem(mtx, 3, 2));
    }
    appendRenderMtx("],\"screen\":[");
    for(s32 slot = 0; slot < sampledMatrices; slot++) {
      u32 mtx = renderPos + (u32)slot * 64u;
      f32 sx = 0.0f, sy = 0.0f;
      s32 valid = projectViewPoint(readN64MtxElem(mtx, 3, 0),
        readN64MtxElem(mtx, 3, 1), readN64MtxElem(mtx, 3, 2),
        cPerspFovy, sx, sy);
      appendRenderMtx("%s{\"valid\":%d,\"xy\":[%.2f,%.2f]}",
        slot == 0 ? "" : ",", valid, sx, sy);
    }
    appendRenderMtx("],\"screen50\":[");
    for(s32 slot = 0; slot < sampledMatrices; slot++) {
      u32 mtx = renderPos + (u32)slot * 64u;
      f32 sx = 0.0f, sy = 0.0f;
      s32 valid = projectViewPoint(readN64MtxElem(mtx, 3, 0),
        readN64MtxElem(mtx, 3, 1), readN64MtxElem(mtx, 3, 2),
        50.0f, sx, sy);
      appendRenderMtx("%s{\"valid\":%d,\"xy\":[%.2f,%.2f]}",
        slot == 0 ? "" : ",", valid, sx, sy);
    }
    appendRenderMtx("]}");

    std::snprintf(out, outSize,
      "{\"root\":[%.2f,%.2f,%.2f],\"world\":[%.2f,%.2f,%.2f],"
      "\"muzzle\":[%.2f,%.2f,%.2f],\"depth\":%.2f,"
      "\"pose\":{\"smooth\":[%.4f,%.4f,%.4f],"
      "\"base\":[%.4f,%.4f,%.4f],"
      "\"target\":[%.4f,%.4f,%.4f],"
      "\"recoil\":[%.5f,%.5f],"
      "\"screen\":[%.4f,%.4f,%.4f,%.4f,%.4f],"
      "\"accum\":[%.4f,%.4f,%.4f],"
      "\"player\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
      "\"blend_meta\":[%d,%d],"
      "\"blend_state\":[%.4f,%.4f,%.4f,%.4f,%.4f],"
      "\"blendpos\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]]},"
      "\"model\":{\"obj\":\"0x%08x\",\"render_pos\":\"0x%08x\","
      "\"num_mtx\":%d,\"scale\":%.4f,"
      "\"root_render\":[%.2f,%.2f,%.2f],"
      "\"root_diag\":[%.4f,%.4f,%.4f],\"render_valid\":%d,%s}}",
      rootX, rootY, rootZ,
      worldX, worldY, worldZ,
      muzzleX, muzzleY, muzzleZ,
      depth,
      smoothX, smoothY, smoothZ,
      baseX, baseY, baseZ,
      targetX, targetY, targetZ,
      recoil, boltRecoil,
      screenX, screenY, gunAzimuth, gunTurning, gunposAmplitude,
      accumX, accumY, accumZ,
      gunposAmplitude, playerField107C, playerField1080,
      playerGunSync, playerSyncChange, playerSyncCount, playerSyncOffset,
      curblendpos, sideflag,
      dampt, blendscale, blendscale1,
      weaponThetaDisplacement, weaponVertaDisplacement,
      blendpos[0][0], blendpos[0][1], blendpos[0][2],
      blendpos[1][0], blendpos[1][1], blendpos[1][2],
      blendpos[2][0], blendpos[2][1], blendpos[2][2],
      blendpos[3][0], blendpos[3][1], blendpos[3][2],
      modelObj, renderPos,
      numMatrices, modelScale,
      renderX, renderY, renderZ,
      renderB00, renderB11, renderB22,
      renderValid, renderMtxJson);
  }

  auto writeU32(u32 address, u32 value) -> void {
    if(!validRdram(address, 4)) return;
    rdram.ram.write<Word>(physicalOffset(address), value, RBusDevice::ARES_DEBUGGER);
  }

  auto writeU64(u32 address, u64 value) -> void {
    if(!validRdram(address, 8)) return;
    writeU32(address, (u32)(value >> 32));
    writeU32(address + 4, (u32)(value & 0xffffffffull));
  }

  auto writeF32(u32 address, f32 value) -> void {
    if(!std::isfinite(value)) return;
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(address, bits);
  }

  auto writeVec3(u32 address, f32 x, f32 y, f32 z) -> void {
    writeF32(address + 0, x);
    writeF32(address + 4, y);
    writeF32(address + 8, z);
  }

  auto appendJsonInt(char* out, size_t outSize, size_t& used, s32 value, bool first) -> void {
    if(used >= outSize) return;
    int written = std::snprintf(out + used, outSize - used, "%s%d", first ? "" : ",", value);
    if(written < 0 || (size_t)written >= outSize - used) {
      used = outSize;
      return;
    }
    used += (size_t)written;
  }

  auto finishJsonArray(char* out, size_t outSize, size_t& used) -> void {
    if(outSize == 0) return;
    if(used + 2 > outSize) {
      std::snprintf(out, outSize, "[]");
      used = 2;
      return;
    }
    out[used++] = ']';
    out[used] = '\0';
  }

  auto formatVisibleRooms(char* out, size_t outSize, s32 count) -> void {
    if(outSize == 0) return;
    size_t used = 0;
    out[used++] = '[';
    if(bgVisibleRoomsAddress != 0 && count > 0 && validRdram(bgVisibleRoomsAddress, 1)) {
      if(count > 64) count = 64;
      for(s32 index = 0; index < count && index < 16; index++) {
        u32 address = bgVisibleRoomsAddress + (u32)index;
        if(!validRdram(address, 1)) break;
        appendJsonInt(out, outSize, used, (s32)readU8(address), index == 0);
        if(used >= outSize) break;
      }
    }
    finishJsonArray(out, outSize, used);
  }

  auto formatRenderedRooms(char* out, size_t outSize, s32 count, s32* sampleRooms, s32* sampleCount) -> void {
    if(outSize == 0) return;
    size_t used = 0;
    out[used++] = '[';
    if(sampleCount) *sampleCount = 0;
    if(bgRenderedRoomsAddress != 0 && count > 0 && validRdram(bgRenderedRoomsAddress, 4)) {
      if(count > 204) count = 204;
      for(s32 index = 0; index < count && index < 16; index++) {
        u32 address = bgRenderedRoomsAddress + (u32)index * 0x1cu;
        if(!validRdram(address, 4)) break;
        s32 room = readS32(address);
        appendJsonInt(out, outSize, used, room, index == 0);
        if(sampleRooms && sampleCount && *sampleCount < 16) {
          sampleRooms[*sampleCount] = room;
          (*sampleCount)++;
        }
        if(used >= outSize) break;
      }
    }
    finishJsonArray(out, outSize, used);
  }

  auto traceRdpTexSamplesActive() -> bool {
    static bool configured = false;
    static bool enabled = false;
    static u32 afterFrame = 0;
    static u32 beforeFrame = 0xffffffffu;
    if(!configured) {
      enabled = parseBoolEnv("MGB64_ARES_TRACE_RDP_TEX_SAMPLES", false);
      afterFrame = parseU32Env("MGB64_ARES_TRACE_RDP_TEX_SAMPLES_AFTER_FRAME", 0);
      beforeFrame = parseU32Env("MGB64_ARES_TRACE_RDP_TEX_SAMPLES_BEFORE_FRAME", 0xffffffffu);
      configured = true;
    }
    if(!enabled) return false;
    return videoFrame >= afterFrame && videoFrame <= beforeFrame;
  }

  auto traceRdpDrawStatesActive() -> bool {
    static bool configured = false;
    static bool enabled = false;
    static u32 afterFrame = 0;
    static u32 beforeFrame = 0xffffffffu;
    if(!configured) {
      enabled = parseBoolEnv("MGB64_ARES_TRACE_RDP_DRAW_STATES", false);
      afterFrame = parseU32Env("MGB64_ARES_TRACE_RDP_DRAW_STATES_AFTER_FRAME", 0);
      beforeFrame = parseU32Env("MGB64_ARES_TRACE_RDP_DRAW_STATES_BEFORE_FRAME", 0xffffffffu);
      configured = true;
    }
    if(!enabled) return false;
    return videoFrame >= afterFrame && videoFrame <= beforeFrame;
  }

  auto traceRoomPointDlActive() -> bool {
    static bool configured = false;
    static bool enabled = false;
    if(!configured) {
      enabled = parseBoolEnv("MGB64_ARES_TRACE_ROOM_POINT_DL", false);
      configured = true;
    }
    return enabled;
  }

  auto dumpRdpTexSampleBytes(OracleRdpTextureSample& sample) -> void {
    const char* dir = std::getenv("MGB64_ARES_TRACE_RDP_TEX_DUMP_DIR");
    if(dir == nullptr || dir[0] == '\0' || sample.image == 0) return;

    u32 limit = parseU32Env("MGB64_ARES_TRACE_RDP_TEX_DUMP_BYTES", 4112);
    if(limit == 0) return;
    if(limit > 65536u) limit = 65536u;

    char path[1024];
    std::snprintf(path,
                  sizeof(path),
                  "%s/rdp_tex_f%05llu_dl%08x_img%08x_idx%04u.bin",
                  dir,
                  (unsigned long long)videoFrame,
                  sample.address,
                  sample.image,
                  sample.commandIndex);

    FILE* fp = std::fopen(path, "wb");
    if(fp == nullptr) return;
    sample.dumpBytes = 0;
    for(u32 index = 0; index < limit; index++) {
      if(!validRdram(sample.image + index, 1)) break;
      u8 value = readU8(sample.image + index);
      if(std::fwrite(&value, 1, 1, fp) != 1) break;
      sample.dumpBytes++;
    }
    std::fclose(fp);
  }

  auto recordF3dDrawState(OracleRoomDlStats& stats,
                          u32 address,
                          u32 commandIndex,
                          u8 op,
                          const OracleDlMaterialState& material,
                          bool sampleEnabled) -> void {
    if(!sampleEnabled) return;

    for(u32 index = 0; index < stats.drawStateCount; index++) {
      auto& state = stats.drawStates[index];
      if(state.textureSerial == material.currentTextureSerial &&
         state.image == material.currentImage &&
         state.fmt == material.currentFmt &&
         state.siz == material.currentSiz &&
         state.tile == material.currentTile &&
         state.width == material.currentTexWidth &&
         state.height == material.currentTexHeight &&
         state.combineHi == material.combineHi &&
         state.combineLo == material.combineLo &&
         state.otherModeL == material.otherModeL &&
         state.env == material.env) {
        if(op == 0x05) {
          state.tri1Commands++;
          state.triangleCount++;
        } else {
          state.tri2Commands++;
          state.triangleCount += 2;
        }
        state.lastAddress = address;
        state.lastCommandIndex = commandIndex;
        return;
      }
    }

    if(stats.drawStateCount >= 96) {
      stats.drawStateTruncated = 1;
      return;
    }

    auto& state = stats.drawStates[stats.drawStateCount++];
    state.textureSerial = material.currentTextureSerial;
    state.image = material.currentImage;
    state.fmt = material.currentFmt;
    state.siz = material.currentSiz;
    state.tile = material.currentTile;
    state.width = material.currentTexWidth;
    state.height = material.currentTexHeight;
    state.combineHi = material.combineHi;
    state.combineLo = material.combineLo;
    state.otherModeL = material.otherModeL;
    state.env = material.env;
    if(op == 0x05) {
      state.tri1Commands = 1;
      state.triangleCount = 1;
    } else {
      state.tri2Commands = 1;
      state.triangleCount = 2;
    }
    state.firstAddress = address;
    state.firstCommandIndex = commandIndex;
    state.lastAddress = address;
    state.lastCommandIndex = commandIndex;
  }

  auto noteRdpTexSample(OracleRoomDlStats& stats,
                        u32 address,
                        u32 commandIndex,
                        u32 w0,
                        u32 w1,
                        u8 op,
                        const OracleDlMaterialState& material,
                        bool sampleEnabled) -> void {
    if(!sampleEnabled) return;
    if(stats.rdpTexSampleCount >= 192) {
      stats.rdpTexSampleTruncated = 1;
      return;
    }

    auto& sample = stats.rdpTexSamples[stats.rdpTexSampleCount++];
    sample.address = address;
    sample.commandIndex = commandIndex;
    sample.w0 = w0;
    sample.w1 = w1;
    sample.op = op;
    sample.combineHi = material.combineHi;
    sample.combineLo = material.combineLo;
    sample.otherModeL = material.otherModeL;
    sample.env = material.env;

    switch(op) {
    case 0xfd:
      sample.fmt = (w0 >> 21) & 0x7u;
      sample.siz = (w0 >> 19) & 0x3u;
      sample.width = w0 & 0x3ffu;
      sample.image = w1;
      sample.imageHash64 = hashRdramBytes(sample.image, 64, sample.imageHashBytes64);
      sample.imageHash512 = hashRdramBytes(sample.image, 512, sample.imageHashBytes512);
      sample.imageHash2916 = hashRdramBytes(sample.image, 2916, sample.imageHashBytes2916);
      sample.imageHash3024 = hashRdramBytes(sample.image, 3024, sample.imageHashBytes3024);
      sample.imageHash3841 = hashRdramBytes(sample.image, 3841, sample.imageHashBytes3841);
      sample.imageHash4112 = hashRdramBytes(sample.image, 4112, sample.imageHashBytes4112);
      sample.imageWord0 = validRdram(sample.image + 0, 4) ? readU32(sample.image + 0) : 0;
      sample.imageWord1 = validRdram(sample.image + 4, 4) ? readU32(sample.image + 4) : 0;
      sample.imageWord2 = validRdram(sample.image + 8, 4) ? readU32(sample.image + 8) : 0;
      sample.imageWord3 = validRdram(sample.image + 12, 4) ? readU32(sample.image + 12) : 0;
      dumpRdpTexSampleBytes(sample);
      break;
    case 0xf5:
      sample.fmt = (w0 >> 21) & 0x7u;
      sample.siz = (w0 >> 19) & 0x3u;
      sample.line = (w0 >> 9) & 0x1ffu;
      sample.tmem = w0 & 0x1ffu;
      sample.tile = (w1 >> 24) & 0x7u;
      sample.palette = (w1 >> 20) & 0xfu;
      sample.cmt = (w1 >> 18) & 0x3u;
      sample.maskt = (w1 >> 14) & 0xfu;
      sample.shiftt = (w1 >> 10) & 0xfu;
      sample.cms = (w1 >> 8) & 0x3u;
      sample.masks = (w1 >> 4) & 0xfu;
      sample.shifts = w1 & 0xfu;
      break;
    case 0xf4:
    case 0xf3:
      sample.tile = (w1 >> 24) & 0x7u;
      sample.uls = (w0 >> 12) & 0xfffu;
      sample.ult = w0 & 0xfffu;
      sample.lrs = (w1 >> 12) & 0xfffu;
      sample.lrt = w1 & 0xfffu;
      sample.dxt = w1 & 0xfffu;
      break;
    case 0xf2:
      sample.tile = (w1 >> 24) & 0x7u;
      sample.uls = (w0 >> 12) & 0xfffu;
      sample.ult = w0 & 0xfffu;
      sample.lrs = (w1 >> 12) & 0xfffu;
      sample.lrt = w1 & 0xfffu;
      break;
    case 0xf0:
      sample.tile = (w1 >> 24) & 0x7u;
      sample.uls = (w0 >> 14) & 0x3ffu;
      sample.ult = (w0 >> 2) & 0x3ffu;
      sample.lrs = (w1 >> 14) & 0x3ffu;
      sample.lrt = (w1 >> 2) & 0x3ffu;
      sample.tlutCount = (w1 >> 14) & 0x3ffu;
      break;
    default:
      break;
    }
  }

  auto scanRoomDlCommands(u32 ptr, u32 bytes, OracleRoomDlStats& stats, u32& budget, u32 depth, OracleDlMaterialState& material, bool sampleRdpTex, bool sampleDrawStates) -> void {
    if(ptr == 0 || bytes < 8 || !validRdram(ptr, 8) || budget == 0) return;

    u32 commands = bytes / 8u;
    if(commands > 2048u) {
      commands = 2048u;
      stats.truncated = 1;
    }

    for(u32 index = 0; index < commands && budget > 0; index++, budget--) {
      u32 address = ptr + index * 8u;
      if(!validRdram(address, 8)) {
        stats.truncated = 1;
        break;
      }

      u32 w0 = readU32(address);
      u32 w1 = readU32(address + 4);
      u8 op = (u8)(w0 >> 24);

      stats.commands++;
      stats.hash = hashU32(hashU32(stats.hash, w0), w1);

      switch(op) {
      case 0x01: stats.vtx++; break;
      case 0x05:
        stats.tri1++;
        recordF3dDrawState(stats, address, stats.commands, op, material, sampleDrawStates);
        break;
      case 0x06:
        stats.tri2++;
        recordF3dDrawState(stats, address, stats.commands, op, material, sampleDrawStates);
        break;
      case 0xc8:
      case 0xc9:
      case 0xca:
      case 0xcb:
      case 0xcc:
      case 0xcd:
      case 0xce:
      case 0xcf:
        stats.rdpTri++;
        break;
      case 0xde:
        stats.dl++;
        if(depth < 2 && validRdram(w1, 8)) {
          scanRoomDlCommands(w1, 4096u, stats, budget, depth + 1, material, sampleRdpTex, sampleDrawStates);
        }
        break;
      case 0xc0: {
        if(w0 == 0 && w1 == 0) break;
        stats.setTex++;
        OracleSetTexSample sample;
        sample.address = address;
        sample.w0 = w0;
        sample.w1 = w1;
        sample.commandIndex = stats.commands;
        sample.texturenum = w1 & 0xfffu;
        sample.minlod = (w1 >> 24) & 0xffu;
        sample.smode = (w0 >> 22) & 0x3u;
        sample.tmode = (w0 >> 20) & 0x3u;
        sample.offset = (w0 >> 18) & 0x3u;
        sample.shifts = (w0 >> 14) & 0xfu;
        sample.shiftt = (w0 >> 10) & 0xfu;
        sample.type = w0 & 0x7u;
        sample.combineHi = material.combineHi;
        sample.combineLo = material.combineLo;
        sample.otherModeL = material.otherModeL;
        sample.env = material.env;
        bool target = sample.texturenum == traceSetTexTargetTexnum();
        if(target) stats.targetSetTex++;
        if(stats.setTexSampleCount < 16 && (target || traceSetTexSampleAll())) {
          stats.setTexSamples[stats.setTexSampleCount++] = sample;
        }
        break;
      }
      case 0xdf:
        stats.endDl++;
        return;
      case 0xe2:
        stats.setOtherMode++;
        material.otherModeL = w1;
        addValueCount(stats.modes, stats.modeCount, 8, w1);
        break;
      case 0xef:
        stats.setOtherMode++;
        material.otherModeL = w1;
        addValueCount(stats.modes, stats.modeCount, 8, w1);
        break;
      case 0xfb:
        stats.setEnv++;
        material.env = w1;
        addValueCount(stats.envAlpha, stats.envAlphaCount, 8, w1 & 0xffu);
        break;
      case 0xfc:
        stats.setCombine++;
        material.combineHi = w0;
        material.combineLo = w1;
        stats.combineHash = hashU32(hashU32(stats.combineHash, w0), w1);
        break;
      case 0xfd:
        stats.setTimg++;
        noteRdpTexSample(stats, address, stats.commands, w0, w1, op, material, sampleRdpTex);
        material.currentTextureSerial++;
        material.currentImage = w1;
        material.currentFmt = (w0 >> 21) & 0x7u;
        material.currentSiz = (w0 >> 19) & 0x3u;
        material.currentTile = 0;
        material.currentTexWidth = 0;
        material.currentTexHeight = 0;
        break;
      case 0xf5:
        material.currentTile = (w1 >> 24) & 0x7u;
        noteRdpTexSample(stats, address, stats.commands, w0, w1, op, material, sampleRdpTex);
        break;
      case 0xf4:
      case 0xf3:
      case 0xf0:
        noteRdpTexSample(stats, address, stats.commands, w0, w1, op, material, sampleRdpTex);
        break;
      case 0xf2: {
        noteRdpTexSample(stats, address, stats.commands, w0, w1, op, material, sampleRdpTex);
        u32 tile = (w1 >> 24) & 0x7u;
        u32 uls = (w0 >> 12) & 0xfffu;
        u32 ult = w0 & 0xfffu;
        u32 lrs = (w1 >> 12) & 0xfffu;
        u32 lrt = w1 & 0xfffu;
        if(lrs >= uls && lrt >= ult) {
          u32 width = (lrs - uls) / 4u + 1u;
          u32 height = (lrt - ult) / 4u + 1u;
          if(tile == 0 || material.currentTexWidth == 0 || material.currentTexHeight == 0) {
            material.currentTile = tile;
            material.currentTexWidth = width;
            material.currentTexHeight = height;
          }
        }
        break;
      }
      default:
        break;
      }
    }

    if(budget == 0) stats.truncated = 1;
  }

  auto scanRoomDl(u32 ptr, s32 csize, s32 usize) -> OracleRoomDlStats {
    OracleRoomDlStats stats;
    stats.ptr = ptr;
    stats.csize = csize;
    stats.usize = usize;

    u32 bytes = 0;
    if(usize > 0) {
      bytes = (u32)usize;
    } else if(csize > 0) {
      bytes = (u32)csize;
    }
    if(bytes > 0x20000u) {
      bytes = 0x20000u;
      stats.truncated = 1;
    }

    u32 budget = 4096u;
    OracleDlMaterialState material;
    bool sampleRdpTex = traceRdpTexSamplesActive();
    bool sampleDrawStates = traceRdpDrawStatesActive();
    scanRoomDlCommands(ptr, bytes, stats, budget, 0, material, sampleRdpTex, sampleDrawStates);
    return stats;
  }

  auto formatDlStats(char* out, size_t outSize, size_t& used, const OracleRoomDlStats& stats) -> void {
    appendText(out, outSize, used,
      "{\"ptr\":\"0x%08x\",\"csize\":%d,\"usize\":%d,"
      "\"cmds\":%u,\"vtx\":%u,\"tri1\":%u,\"tri2\":%u,\"rdptri\":%u,\"dl\":%u,"
      "\"settex\":%u,\"target_settex\":%u,"
      "\"settimg\":%u,\"setcombine\":%u,\"setenv\":%u,\"setothermode\":%u,\"enddl\":%u,"
      "\"truncated\":%u,\"hash\":\"0x%016llX\",\"combine_hash\":\"0x%016llX\","
      "\"modes\":[",
      stats.ptr, stats.csize, stats.usize,
      stats.commands, stats.vtx, stats.tri1, stats.tri2, stats.rdpTri, stats.dl,
      stats.setTex, stats.targetSetTex,
      stats.setTimg, stats.setCombine, stats.setEnv, stats.setOtherMode, stats.endDl,
      stats.truncated,
      (unsigned long long)stats.hash,
      (unsigned long long)stats.combineHash);
    for(u32 index = 0; index < stats.modeCount; index++) {
      appendText(out, outSize, used, "%s{\"raw\":\"0x%08x\",\"count\":%u}",
        index == 0 ? "" : ",", stats.modes[index].value, stats.modes[index].count);
    }
    appendText(out, outSize, used, "],\"env_alpha\":[");
    for(u32 index = 0; index < stats.envAlphaCount; index++) {
      appendText(out, outSize, used, "%s{\"a\":%u,\"count\":%u}",
        index == 0 ? "" : ",", stats.envAlpha[index].value, stats.envAlpha[index].count);
    }
    appendText(out, outSize, used, "],\"settex_sample\":[");
    for(u32 index = 0; index < stats.setTexSampleCount; index++) {
      const auto& sample = stats.setTexSamples[index];
      appendText(out, outSize, used,
        "%s{\"addr\":\"0x%08x\",\"index\":%u,\"w0\":\"0x%08x\",\"w1\":\"0x%08x\","
        "\"texnum\":%u,\"type\":%u,\"smode\":%u,\"tmode\":%u,\"offset\":%u,"
        "\"shifts\":%u,\"shiftt\":%u,\"minlod\":%u,"
        "\"combine\":[\"0x%08x\",\"0x%08x\"],\"other_l\":\"0x%08x\",\"env\":\"0x%08x\"}",
        index == 0 ? "" : ",",
        sample.address,
        sample.commandIndex,
        sample.w0,
        sample.w1,
        sample.texturenum,
        sample.type,
        sample.smode,
        sample.tmode,
        sample.offset,
        sample.shifts,
        sample.shiftt,
        sample.minlod,
        sample.combineHi,
        sample.combineLo,
        sample.otherModeL,
        sample.env);
    }
    appendText(out, outSize, used, "],\"rdp_tex_sample_truncated\":%u,\"rdp_tex_sample\":[",
      stats.rdpTexSampleTruncated);
    for(u32 index = 0; index < stats.rdpTexSampleCount; index++) {
      const auto& sample = stats.rdpTexSamples[index];
      appendText(out, outSize, used,
        "%s{\"op\":\"%s\",\"addr\":\"0x%08x\",\"index\":%u,"
        "\"w0\":\"0x%08x\",\"w1\":\"0x%08x\",\"fmt\":%u,\"siz\":%u,"
        "\"width\":%u,\"image\":\"0x%08x\",\"tile\":%u,\"line\":%u,\"tmem\":%u,"
        "\"palette\":%u,\"cms\":%u,\"cmt\":%u,\"masks\":%u,\"maskt\":%u,"
        "\"shifts\":%u,\"shiftt\":%u,\"uls\":%u,\"ult\":%u,\"lrs\":%u,\"lrt\":%u,"
        "\"dxt\":%u,\"tlut_count\":%u,"
        "\"image_hashes\":{\"64\":\"0x%016llX\",\"512\":\"0x%016llX\","
        "\"2916\":\"0x%016llX\",\"3024\":\"0x%016llX\","
        "\"3841\":\"0x%016llX\",\"4112\":\"0x%016llX\"},"
        "\"image_hash_bytes\":{\"64\":%u,\"512\":%u,\"2916\":%u,\"3024\":%u,"
        "\"3841\":%u,\"4112\":%u},"
        "\"image_first_words\":[\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\"],"
        "\"dump_bytes\":%u,"
        "\"combine\":[\"0x%08x\",\"0x%08x\"],\"other_l\":\"0x%08x\",\"env\":\"0x%08x\"}",
        index == 0 ? "" : ",",
        rdpTextureOpName(sample.op),
        sample.address,
        sample.commandIndex,
        sample.w0,
        sample.w1,
        sample.fmt,
        sample.siz,
        sample.width,
        sample.image,
        sample.tile,
        sample.line,
        sample.tmem,
        sample.palette,
        sample.cms,
        sample.cmt,
        sample.masks,
        sample.maskt,
        sample.shifts,
        sample.shiftt,
        sample.uls,
        sample.ult,
        sample.lrs,
        sample.lrt,
        sample.dxt,
        sample.tlutCount,
        (unsigned long long)sample.imageHash64,
        (unsigned long long)sample.imageHash512,
        (unsigned long long)sample.imageHash2916,
        (unsigned long long)sample.imageHash3024,
        (unsigned long long)sample.imageHash3841,
        (unsigned long long)sample.imageHash4112,
        sample.imageHashBytes64,
        sample.imageHashBytes512,
        sample.imageHashBytes2916,
        sample.imageHashBytes3024,
        sample.imageHashBytes3841,
        sample.imageHashBytes4112,
        sample.imageWord0,
        sample.imageWord1,
        sample.imageWord2,
        sample.imageWord3,
        sample.dumpBytes,
        sample.combineHi,
        sample.combineLo,
        sample.otherModeL,
        sample.env);
    }
    appendText(out, outSize, used, "],\"draw_state_truncated\":%u,\"draw_state\":[",
      stats.drawStateTruncated);
    for(u32 index = 0; index < stats.drawStateCount; index++) {
      const auto& state = stats.drawStates[index];
      appendText(out, outSize, used,
        "%s{\"texture_serial\":%u,\"image\":\"0x%08x\",\"fmt\":%u,\"siz\":%u,"
        "\"tile\":%u,\"width\":%u,\"height\":%u,"
        "\"tri1_cmds\":%u,\"tri2_cmds\":%u,\"triangles\":%u,"
        "\"first_addr\":\"0x%08x\",\"first_index\":%u,"
        "\"last_addr\":\"0x%08x\",\"last_index\":%u,"
        "\"combine\":[\"0x%08x\",\"0x%08x\"],\"other_l\":\"0x%08x\",\"env\":\"0x%08x\"}",
        index == 0 ? "" : ",",
        state.textureSerial,
        state.image,
        state.fmt,
        state.siz,
        state.tile,
        state.width,
        state.height,
        state.tri1Commands,
        state.tri2Commands,
        state.triangleCount,
        state.firstAddress,
        state.firstCommandIndex,
        state.lastAddress,
        state.lastCommandIndex,
        state.combineHi,
        state.combineLo,
        state.otherModeL,
        state.env);
    }
    appendText(out, outSize, used, "]}");
  }

  auto formatRoomDlSummaries(char* out, size_t outSize, const s32* rooms, s32 count) -> void {
    if(outSize == 0) return;
    size_t used = 0;
    out[used++] = '[';

    s32 emitted = 0;
    s32 seen[16] = {};
    s32 seenCount = 0;
    for(s32 index = 0; rooms && index < count && index < 16; index++) {
      s32 room = rooms[index];
      if(room < 0 || room > 255) continue;
      bool duplicate = false;
      for(s32 seenIndex = 0; seenIndex < seenCount; seenIndex++) {
        if(seen[seenIndex] == room) {
          duplicate = true;
          break;
        }
      }
      if(duplicate) continue;
      seen[seenCount++] = room;

      u32 info = bgRoomInfoAddress + (u32)room * bgRoomInfoStride;
      if(bgRoomInfoAddress == 0 || bgRoomInfoStride == 0 || !validRdram(info, 0x38)) continue;

      u32 pointPtr = readU32(info + RoomInfoPointPtr);
      u32 primaryPtr = readU32(info + RoomInfoPrimaryPtr);
      u32 secondaryPtr = readU32(info + RoomInfoSecondaryPtr);
      s32 csizePoint = readS32(info + RoomInfoCSizePoint);
      s32 csizePrimary = readS32(info + RoomInfoCSizePrimary);
      s32 csizeSecondary = readS32(info + RoomInfoCSizeSecondary);
      s32 usizePoint = readS32(info + RoomInfoUSizePoint);
      s32 usizePrimary = readS32(info + RoomInfoUSizePrimary);
      s32 usizeSecondary = readS32(info + RoomInfoUSizeSecondary);
      bool includePointDl = traceRoomPointDlActive();
      OracleRoomDlStats point;
      if(includePointDl) {
        point = scanRoomDl(pointPtr, csizePoint, usizePoint);
      }
      OracleRoomDlStats primary = scanRoomDl(primaryPtr, csizePrimary, usizePrimary);
      OracleRoomDlStats secondary = scanRoomDl(secondaryPtr, csizeSecondary, usizeSecondary);

      appendText(out, outSize, used,
        "%s{\"room\":%d,\"rendered\":%u,\"neighbor\":%u,\"model\":%u,"
        "\"portal_count\":%u,\"loaded_mask\":%u,"
        "\"point\":\"0x%08x\",\"point_csize\":%d,\"point_usize\":%d,"
        "\"point_dl\":",
        emitted == 0 ? "" : ",",
        room,
        (unsigned)readU8(info + RoomInfoRendered),
        (unsigned)readU8(info + RoomInfoNeighbor),
        (unsigned)readU8(info + RoomInfoModelLoaded),
        (unsigned)readU8(info + RoomInfoPortalCount),
        (unsigned)readU8(info + RoomInfoLoadedMask),
        pointPtr, csizePoint, usizePoint);
      if(includePointDl) {
        formatDlStats(out, outSize, used, point);
      } else {
        appendText(out, outSize, used, "null");
      }
      appendText(out, outSize, used, ",\"primary\":");
      formatDlStats(out, outSize, used, primary);
      appendText(out, outSize, used, ",\"secondary\":");
      formatDlStats(out, outSize, used, secondary);
      appendText(out, outSize, used, "}");
      emitted++;
      if(used >= outSize) break;
    }

    finishJsonArray(out, outSize, used);
  }

  auto stanRoom(u32 stan) -> s32 {
    if(!validRdram(stan, 4)) return -1;
    return (s32)readU8(stan + 3);
  }

  /* Combat/floor oracle (FID-0032) tile field readers. */
  auto stanId(u32 stan) -> s32 {
    if(!validRdram(stan, 4)) return -1;
    return (s32)(readU32(stan + StandTileId) & 0x00ffffffu);
  }

  auto stanFlags(u32 stan) -> s32 {
    if(!validRdram(stan + StandTileMid, 2)) return -1;
    return (s32)(s16)readU16(stan + StandTileMid);
  }

  /* Model animation header hash — MUST match the native
   * traceModelAnimationHeaderHash (port_trace.c:1295) word-for-word so guard
   * anim_hash is comparable across the two tracers. */
  auto animHeaderHash(u32 anim) -> u64 {
    if(anim == 0 || !validRdram(anim, 0x14)) return 0;
    u64 hash = 1469598103934665603ull;
    hash = hashU32(hash, (u32)readU16(anim + 0x04));
    hash = hashU32(hash, (u32)readU8(anim + 0x06));
    hash = hashU32(hash, (u32)readU8(anim + 0x07));
    hash = hashU32(hash, readU32(anim + 0x08));
    hash = hashU32(hash, (u32)readU16(anim + 0x0c));
    hash = hashU32(hash, (u32)readU16(anim + 0x0e));
    hash = hashU32(hash, readU32(anim + 0x10));
    return hash;
  }

  auto roomRendered(s32 room, s32 renderedRoomsCount) -> bool {
    if(room < 0 || renderedRoomsCount <= 0 || bgRenderedRoomsAddress == 0) return false;
    if(renderedRoomsCount > 204) renderedRoomsCount = 204;
    for(s32 index = 0; index < renderedRoomsCount; index++) {
      u32 address = bgRenderedRoomsAddress + (u32)index * 0x1cu;
      if(!validRdram(address, 4)) break;
      if(readS32(address) == room) return true;
    }
    return false;
  }

  auto propRenderedInRooms(u32 prop, s32 stanRoomValue, s32 renderedRoomsCount) -> bool {
    bool sawRoom = false;
    if(validRdram(prop + PropRooms, 4)) {
      for(s32 index = 0; index < 4; index++) {
        s32 room = (s32)readU8(prop + PropRooms + (u32)index);
        if(room == 0xff) break;
        sawRoom = true;
        if(roomRendered(room, renderedRoomsCount)) return true;
      }
    }
    return !sawRoom && roomRendered(stanRoomValue, renderedRoomsCount);
  }

  auto formatPropRoomsJson(char* out, size_t outSize, u32 prop, s32 stanRoomValue, s32 renderedRoomsCount, s32* firstRoom, s32* roomCount, s32* anyRendered, s32* firstRendered) -> void {
    if(outSize == 0) return;
    size_t used = 0;
    out[used++] = '[';
    if(firstRoom) *firstRoom = -1;
    if(roomCount) *roomCount = 0;
    if(anyRendered) *anyRendered = 0;
    if(firstRendered) *firstRendered = 0;

    if(validRdram(prop + PropRooms, 4)) {
      for(s32 index = 0; index < 4; index++) {
        s32 room = (s32)readU8(prop + PropRooms + (u32)index);
        if(room == 0xff) break;
        if(firstRoom && *firstRoom < 0) *firstRoom = room;
        if(roomCount) (*roomCount)++;
        bool rendered = roomRendered(room, renderedRoomsCount);
        if(anyRendered && rendered) *anyRendered = 1;
        if(firstRendered && index == 0 && rendered) *firstRendered = 1;
        appendJsonInt(out, outSize, used, room, index == 0);
        if(used >= outSize) break;
      }
    }

    if(roomCount && *roomCount == 0 && stanRoomValue >= 0) {
      if(firstRoom) *firstRoom = stanRoomValue;
      *roomCount = 1;
      bool rendered = roomRendered(stanRoomValue, renderedRoomsCount);
      if(anyRendered && rendered) *anyRendered = 1;
      if(firstRendered && rendered) *firstRendered = 1;
      appendJsonInt(out, outSize, used, stanRoomValue, true);
    }

    finishJsonArray(out, outSize, used);
  }

  auto formatGlassStateJson(char* out, size_t outSize) -> void {
    if(outSize == 0) return;

    u64 hash = 1469598103934665603ull;
    s32 bufferLen = 0;
    s32 nextShard = 0;
    u32 pieces = 0;
    s32 active = 0;
    s32 firstIndex = -1;
    s32 firstPiece = 0;
    f32 firstX = 0.0f;
    f32 firstY = 0.0f;
    f32 firstZ = 0.0f;
    f32 firstAge = 0.0f;
    f32 firstRot = 0.0f;
    char sampleJson[2048];
    size_t sampleUsed = 1;
    s32 sampleCount = 0;

    sampleJson[0] = '[';
    sampleJson[1] = '\0';

    if(shatteredWindowPiecesBufferLenAddress != 0 &&
       validRdram(shatteredWindowPiecesBufferLenAddress, 4)) {
      bufferLen = readS32(shatteredWindowPiecesBufferLenAddress);
    }
    if(nextShardNumAddress != 0 && validRdram(nextShardNumAddress, 4)) {
      nextShard = readS32(nextShardNumAddress);
    }
    if(ptrShatteredWindowPiecesAddress != 0 &&
       validRdram(ptrShatteredWindowPiecesAddress, 4)) {
      pieces = readU32(ptrShatteredWindowPiecesAddress);
    }

    if(bufferLen <= 0 || bufferLen > 512 || pieces == 0 || !validRdram(pieces, 0x68)) {
      std::snprintf(out, outSize,
        "{\"present\":0,\"buffer_len\":%d,\"next\":%d,\"ptr\":\"0x%08x\","
        "\"active\":0,\"first\":{\"index\":-1},\"sample\":[],\"hash\":\"0x%016llX\"}",
        bufferLen, nextShard, pieces, (unsigned long long)hash);
      return;
    }

    for(s32 index = 0; index < bufferLen; index++) {
      u32 piece = pieces + (u32)index * 0x68u;
      if(!validRdram(piece, 0x68)) break;
      s32 pieceState = readS32(piece + 0x00);
      if(pieceState <= 0) continue;

      if(firstIndex < 0) {
        firstIndex = index;
        firstPiece = pieceState;
        firstX = readF32(piece + 0x04);
        firstY = readF32(piece + 0x08);
        firstZ = readF32(piece + 0x0c);
        firstAge = readF32(piece + 0x14);
        firstRot = readF32(piece + 0x18);
      }
      active++;
      if(sampleCount < 4 && sampleUsed < sizeof(sampleJson)) {
        s32 remaining = (s32)(sizeof(sampleJson) - sampleUsed);
        s32 written = std::snprintf(sampleJson + sampleUsed,
          (size_t)remaining,
          "%s{\"index\":%d,\"piece\":%d,"
          "\"pos\":[%.2f,%.2f,%.2f],"
          "\"rot\":[%.2f,%.2f,%.2f],"
          "\"vel\":[%.2f,%.2f,%.2f],"
          "\"angvel\":[%.2f,%.2f,%.2f],"
          "\"v0\":[%d,%d,%d],\"v1\":[%d,%d,%d],"
          "\"v2\":[%d,%d,%d]}",
          sampleCount == 0 ? "" : ",",
          index, pieceState,
          readF32(piece + 0x04), readF32(piece + 0x08), readF32(piece + 0x0c),
          readF32(piece + 0x10), readF32(piece + 0x14), readF32(piece + 0x18),
          readF32(piece + 0x1c), readF32(piece + 0x20), readF32(piece + 0x24),
          readF32(piece + 0x28), readF32(piece + 0x2c), readF32(piece + 0x30),
          (s32)readS16(piece + 0x38), (s32)readS16(piece + 0x3a), (s32)readS16(piece + 0x3c),
          (s32)readS16(piece + 0x48), (s32)readS16(piece + 0x4a), (s32)readS16(piece + 0x4c),
          (s32)readS16(piece + 0x58), (s32)readS16(piece + 0x5a), (s32)readS16(piece + 0x5c));
        if(written > 0) {
          sampleUsed += (size_t)written < (size_t)remaining ? (size_t)written : (size_t)remaining - 1u;
        }
        sampleCount++;
      }
      for(u32 offset = 0; offset < 0x68u; offset += 4u) {
        hash = hashU32(hash, readU32(piece + offset));
      }
    }
    if(sampleUsed < sizeof(sampleJson) - 1u) {
      sampleJson[sampleUsed++] = ']';
      sampleJson[sampleUsed] = '\0';
    } else {
      sampleJson[sizeof(sampleJson) - 2u] = ']';
      sampleJson[sizeof(sampleJson) - 1u] = '\0';
    }

    if(firstIndex >= 0) {
      std::snprintf(out, outSize,
        "{\"present\":1,\"buffer_len\":%d,\"next\":%d,\"ptr\":\"0x%08x\","
        "\"active\":%d,\"first\":{\"index\":%d,\"piece\":%d,\"timer\":%d,"
        "\"pos\":[%.2f,%.2f,%.2f],\"age\":%.2f,"
        "\"rot_y\":%.2f,\"rot\":%.2f,\"rot_z\":%.2f},"
        "\"sample\":%s,\"hash\":\"0x%016llX\"}",
        bufferLen, nextShard, pieces, active, firstIndex, firstPiece,
        firstPiece, firstX, firstY, firstZ, firstAge, firstAge, firstRot, firstRot,
        sampleJson,
        (unsigned long long)hash);
    } else {
      std::snprintf(out, outSize,
        "{\"present\":1,\"buffer_len\":%d,\"next\":%d,\"ptr\":\"0x%08x\","
        "\"active\":0,\"first\":{\"index\":-1},\"sample\":[],\"hash\":\"0x%016llX\"}",
        bufferLen, nextShard, pieces, (unsigned long long)hash);
    }
  }

  auto setPositionRotationXYZ(f32 out[4][4],
                              f32 x,
                              f32 y,
                              f32 z,
                              f32 rx,
                              f32 ry,
                              f32 rz) -> void {
    f32 cosX = std::cos(rx);
    f32 sinX = std::sin(rx);
    f32 cosY = std::cos(ry);
    f32 sinY = std::sin(ry);
    f32 cosZ = std::cos(rz);
    f32 sinZ = std::sin(rz);
    f32 sinXSinZ = sinX * sinZ;
    f32 cosXSinZ = cosX * sinZ;
    f32 sinXCosZ = sinX * cosZ;
    f32 cosXCosZ = cosX * cosZ;

    out[0][0] = cosY * cosZ;
    out[0][1] = cosY * sinZ;
    out[0][2] = -sinY;
    out[0][3] = 0.0f;
    out[1][0] = (sinXCosZ * sinY) - cosXSinZ;
    out[1][1] = (sinXSinZ * sinY) + cosXCosZ;
    out[1][2] = sinX * cosY;
    out[1][3] = 0.0f;
    out[2][0] = (cosXCosZ * sinY) + sinXSinZ;
    out[2][1] = (cosXSinZ * sinY) - sinXCosZ;
    out[2][2] = cosX * cosY;
    out[2][3] = 0.0f;
    out[3][0] = x;
    out[3][1] = y;
    out[3][2] = z;
    out[3][3] = 1.0f;
  }

  auto transformMtxf(const f32 matrix[4][4], f32 x, f32 y, f32 z, f32 out[3]) -> void {
    out[0] = matrix[0][0] * x + matrix[1][0] * y + matrix[2][0] * z + matrix[3][0];
    out[1] = matrix[0][1] * x + matrix[1][1] * y + matrix[2][1] * z + matrix[3][1];
    out[2] = matrix[0][2] * x + matrix[1][2] * y + matrix[2][2] * z + matrix[3][2];
  }

  auto transform4x4(const f32 matrix[4][4], f32 x, f32 y, f32 z, f32 w, f32 out[4]) -> void {
    out[0] = matrix[0][0] * x + matrix[1][0] * y + matrix[2][0] * z + matrix[3][0] * w;
    out[1] = matrix[0][1] * x + matrix[1][1] * y + matrix[2][1] * z + matrix[3][1] * w;
    out[2] = matrix[0][2] * x + matrix[1][2] * y + matrix[2][2] * z + matrix[3][2] * w;
    out[3] = matrix[0][3] * x + matrix[1][3] * y + matrix[2][3] * z + matrix[3][3] * w;
  }

  auto formatGlassProjectionJson(char* out, size_t outSize, u32 player) -> void {
    enum : s32 { SampleDefaultMax = 16, SampleAllMax = 512 };
    if(outSize == 0) return;

    s32 bufferLen = 0;
    u32 pieces = 0;
    u32 projectionPtr = 0;
    s32 active = 0;
    s32 projected = 0;
    s32 onscreen = 0;
    s32 behind = 0;
    s32 sampleCount = 0;
    s32 sampleMax = SampleDefaultMax;
    s32 sampleTruncated = 0;
    const char* sampleAllEnv = std::getenv("MGB64_ARES_TRACE_GLASS_PROJECTION_ALL");
    bool sampleAll = sampleAllEnv != nullptr && sampleAllEnv[0] != '\0' && sampleAllEnv[0] != '0';
    f32 viewportLeft = 0.0f;
    f32 viewportTop = 0.0f;
    f32 viewportWidth = 320.0f;
    f32 viewportHeight = 240.0f;
    f32 playerX = 0.0f;
    f32 playerY = 0.0f;
    f32 playerZ = 0.0f;
    f32 projection[4][4] = {};
    f32 unionMinX = FLT_MAX;
    f32 unionMinY = FLT_MAX;
    f32 unionMaxX = -FLT_MAX;
    f32 unionMaxY = -FLT_MAX;
    f32 maxArea = 0.0f;
    char sampleJson[49152];
    size_t sampleUsed = 1;

    sampleJson[0] = '[';
    sampleJson[1] = '\0';

    if(player != 0 && validRdram(player + PlayerCScreenLeft, 4)) {
      viewportLeft = readF32(player + PlayerCScreenLeft);
      viewportTop = readF32(player + PlayerCScreenTop);
      viewportWidth = readF32(player + PlayerCScreenWidth);
      viewportHeight = readF32(player + PlayerCScreenHeight);
      playerX = readF32(player + PlayerCurrentModelPos + 0);
      playerY = readF32(player + PlayerCurrentModelPos + 4);
      playerZ = readF32(player + PlayerCurrentModelPos + 8);
      projectionPtr = validRdram(player + PlayerField10E0, 4) ? readU32(player + PlayerField10E0) : 0;
    }
    if(shatteredWindowPiecesBufferLenAddress != 0 &&
       validRdram(shatteredWindowPiecesBufferLenAddress, 4)) {
      bufferLen = readS32(shatteredWindowPiecesBufferLenAddress);
    }
    if(ptrShatteredWindowPiecesAddress != 0 &&
       validRdram(ptrShatteredWindowPiecesAddress, 4)) {
      pieces = readU32(ptrShatteredWindowPiecesAddress);
    }
    if(bufferLen <= 0 || bufferLen > 512 || pieces == 0 ||
       !validRdram(pieces, 0x68) || projectionPtr == 0 ||
       !validRdram(projectionPtr, 64)) {
      std::snprintf(out, outSize,
        "{\"present\":0,\"source\":\"stock_fixed_projection\","
      "\"emit_enabled\":1,\"projection_valid\":%d,\"projection_float\":0,"
      "\"compress_full_matrix\":0,\"basis_scale\":0,\"no_basis_scale\":1,"
        "\"active\":0,\"projected\":0,\"onscreen\":0,\"behind\":0,\"sample\":[]}",
        projectionPtr != 0 && validRdram(projectionPtr, 64) ? 1 : 0);
      return;
    }

    for(s32 row = 0; row < 4; row++) {
      for(s32 col = 0; col < 4; col++) {
        projection[row][col] = readN64MtxElem(projectionPtr, row, col);
      }
    }
    sampleMax = sampleAll ? SampleAllMax : SampleDefaultMax;

    for(s32 index = 0; index < bufferLen; index++) {
      u32 piece = pieces + (u32)index * 0x68u;
      if(!validRdram(piece, 0x68)) break;
      s32 pieceState = readS32(piece + 0x00);
      if(pieceState <= 0) continue;
      active++;

      f32 model[4][4];
      setPositionRotationXYZ(model,
        readF32(piece + 0x04),
        readF32(piece + 0x08),
        readF32(piece + 0x0c),
        readF32(piece + 0x10),
        readF32(piece + 0x14),
        readF32(piece + 0x18));
      model[3][0] -= playerX;
      model[3][1] -= playerY;
      model[3][2] -= playerZ;

      s16 v[3][3] = {
        {readS16(piece + 0x38), readS16(piece + 0x3a), readS16(piece + 0x3c)},
        {readS16(piece + 0x48), readS16(piece + 0x4a), readS16(piece + 0x4c)},
        {readS16(piece + 0x58), readS16(piece + 0x5a), readS16(piece + 0x5c)},
      };
      f32 sx[3] = {};
      f32 sy[3] = {};
      f32 sw[3] = {};
      f32 modelPoints[3][3] = {};
      f32 minX = FLT_MAX;
      f32 minY = FLT_MAX;
      f32 maxX = -FLT_MAX;
      f32 maxY = -FLT_MAX;
      bool valid = true;
      bool pieceBehind = false;

      for(s32 vi = 0; vi < 3; vi++) {
        f32 modelPos[3] = {};
        f32 clip[4] = {};
        transformMtxf(model, (f32)v[vi][0], (f32)v[vi][1], (f32)v[vi][2], modelPos);
        modelPoints[vi][0] = modelPos[0];
        modelPoints[vi][1] = modelPos[1];
        modelPoints[vi][2] = modelPos[2];
        transform4x4(projection, modelPos[0], modelPos[1], modelPos[2], 1.0f, clip);
        sw[vi] = clip[3];
        if(!std::isfinite(clip[0]) || !std::isfinite(clip[1]) ||
           !std::isfinite(clip[3]) || std::fabs(clip[3]) < 0.000001f) {
          valid = false;
          continue;
        }
        if(clip[3] <= 0.0f) pieceBehind = true;
        f32 ndcX = clip[0] / clip[3];
        f32 ndcY = clip[1] / clip[3];
        sx[vi] = viewportLeft + (ndcX * 0.5f + 0.5f) * viewportWidth;
        sy[vi] = viewportTop + (0.5f - ndcY * 0.5f) * viewportHeight;
        if(!std::isfinite(sx[vi]) || !std::isfinite(sy[vi])) {
          valid = false;
          continue;
        }
        if(sx[vi] < minX) minX = sx[vi];
        if(sy[vi] < minY) minY = sy[vi];
        if(sx[vi] > maxX) maxX = sx[vi];
        if(sy[vi] > maxY) maxY = sy[vi];
      }
      if(!valid || minX == FLT_MAX || minY == FLT_MAX || maxX == -FLT_MAX || maxY == -FLT_MAX) {
        continue;
      }

      projected++;
      if(pieceBehind) behind++;
      f32 area = (maxX - minX) * (maxY - minY);
      if(area < 0.0f || !std::isfinite(area)) area = 0.0f;
      if(area > maxArea) maxArea = area;
      bool pieceOnscreen = maxX >= viewportLeft &&
                           maxY >= viewportTop &&
                           minX <= viewportLeft + viewportWidth &&
                           minY <= viewportTop + viewportHeight;
      if(pieceOnscreen) {
        onscreen++;
        if(minX < unionMinX) unionMinX = minX;
        if(minY < unionMinY) unionMinY = minY;
        if(maxX > unionMaxX) unionMaxX = maxX;
        if(maxY > unionMaxY) unionMaxY = maxY;
      }

      if(sampleCount < sampleMax && sampleUsed < sizeof(sampleJson)) {
        char pieceJson[1024];
        s32 written = std::snprintf(pieceJson,
          sizeof(pieceJson),
          "%s{\"index\":%d,\"timer\":%d,\"onscreen\":%d,\"behind\":%d,"
          "\"world\":[%.2f,%.2f,%.2f],"
          "\"model\":[[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f]],"
          "\"screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
          "\"screen_area\":%.2f,\"clip_w\":[%.4f,%.4f,%.4f],"
          "\"screen\":[[%.2f,%.2f],[%.2f,%.2f],[%.2f,%.2f]]}",
          sampleCount == 0 ? "" : ",",
          index, pieceState, pieceOnscreen ? 1 : 0, pieceBehind ? 1 : 0,
          readF32(piece + 0x04), readF32(piece + 0x08), readF32(piece + 0x0c),
          modelPoints[0][0], modelPoints[0][1], modelPoints[0][2],
          modelPoints[1][0], modelPoints[1][1], modelPoints[1][2],
          modelPoints[2][0], modelPoints[2][1], modelPoints[2][2],
          minX, minY, maxX, maxY,
          area,
          sw[0], sw[1], sw[2],
          sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
        if(written > 0 &&
           (size_t)written < sizeof(pieceJson) &&
           sampleUsed + (size_t)written < sizeof(sampleJson) - 1u) {
          std::memcpy(sampleJson + sampleUsed, pieceJson, (size_t)written);
          sampleUsed += (size_t)written;
          sampleJson[sampleUsed] = '\0';
          sampleCount++;
        } else {
          sampleTruncated = 1;
        }
      }
    }

    if(sampleUsed < sizeof(sampleJson) - 1u) {
      sampleJson[sampleUsed++] = ']';
      sampleJson[sampleUsed] = '\0';
    } else {
      sampleJson[sizeof(sampleJson) - 2u] = ']';
      sampleJson[sizeof(sampleJson) - 1u] = '\0';
    }
    if(onscreen == 0) {
      unionMinX = 0.0f;
      unionMinY = 0.0f;
      unionMaxX = 0.0f;
      unionMaxY = 0.0f;
    }

	    std::snprintf(out, outSize,
	      "{\"present\":1,\"source\":\"stock_fixed_projection\","
	      "\"emit_enabled\":1,\"projection_valid\":1,\"projection_float\":0,"
	      "\"compress_full_matrix\":0,\"basis_scale\":0,\"no_basis_scale\":1,"
	      "\"room_scale\":1.00000000,"
	      "\"projection_diag\":[%.6f,%.6f,%.6f,%.6f],"
	      "\"projection_row3\":[%.6f,%.6f,%.6f,%.6f],"
	      "\"projection_col3\":[%.6f,%.6f,%.6f,%.6f],"
	      "\"projection_ptr\":\"0x%08x\",\"viewport\":[%.2f,%.2f,%.2f,%.2f],"
	      "\"active\":%d,\"projected\":%d,\"onscreen\":%d,\"behind\":%d,"
	      "\"sample_all\":%d,\"sample_limit\":%d,\"sample_count\":%d,\"sample_truncated\":%d,"
	      "\"union_screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
	      "\"max_screen_area\":%.2f,\"sample\":%s}",
	      projection[0][0], projection[1][1], projection[2][2], projection[3][3],
	      projection[3][0], projection[3][1], projection[3][2], projection[3][3],
	      projection[0][3], projection[1][3], projection[2][3], projection[3][3],
	      projectionPtr,
	      viewportLeft, viewportTop, viewportWidth, viewportHeight,
      active, projected, onscreen, behind,
      sampleAll ? 1 : 0,
      sampleMax,
      sampleCount,
      sampleTruncated,
      unionMinX, unionMinY, unionMaxX, unionMaxY,
      maxArea,
      sampleJson);
  }

  auto roomLocalToWorld(s32 room, f32 localX, f32 localY, f32 localZ, f32& worldX, f32& worldY, f32& worldZ) -> bool {
    if(room < 0 || bgRoomFilePositionListAddress == 0 || roomDataFloat2Address == 0) return false;
    if(!validRdram(bgRoomFilePositionListAddress, 4) || !validRdram(roomDataFloat2Address, 4)) return false;

    u32 rooms = readU32(bgRoomFilePositionListAddress);
    if(rooms == 0) return false;

    u32 roomData = rooms + (u32)room * BgRoomDataStride;
    if(!validRdram(roomData + BgRoomDataPos, 12)) return false;

    f32 scale = readF32(roomDataFloat2Address);
    if(!std::isfinite(scale) || scale == 0.0f) return false;

    worldX = (readF32(roomData + BgRoomDataPos + 0) + localX) * scale;
    worldY = (readF32(roomData + BgRoomDataPos + 4) + localY) * scale;
    worldZ = (readF32(roomData + BgRoomDataPos + 8) + localZ) * scale;
    return std::isfinite(worldX) && std::isfinite(worldY) && std::isfinite(worldZ);
  }

  auto roomLocalToModel(s32 room,
                        f32 localX,
                        f32 localY,
                        f32 localZ,
                        f32 basisX,
                        f32 basisY,
                        f32 basisZ,
                        f32& modelX,
                        f32& modelY,
                        f32& modelZ) -> bool {
    f32 worldX = 0.0f;
    f32 worldY = 0.0f;
    f32 worldZ = 0.0f;
    if(!roomLocalToWorld(room, localX, localY, localZ, worldX, worldY, worldZ)) return false;
    modelX = worldX - basisX;
    modelY = worldY - basisY;
    modelZ = worldZ - basisZ;
    return std::isfinite(modelX) && std::isfinite(modelY) && std::isfinite(modelZ);
  }

  auto projectRoomLocalQuad(s32 room,
                            const s32 vertices[4][3],
                            const f32 projection[4][4],
                            f32 viewportLeft,
                            f32 viewportTop,
                            f32 viewportWidth,
                            f32 viewportHeight,
                            f32 basisX,
                            f32 basisY,
                            f32 basisZ,
                            f32 modelV[4][3],
                            f32 screenV[4][2],
                            f32 clipW[4],
                            f32 screenBbox[4],
                            f32& screenArea,
                            s32& behind,
                            s32& onscreen) -> bool {
    f32 minX = FLT_MAX;
    f32 minY = FLT_MAX;
    f32 maxX = -FLT_MAX;
    f32 maxY = -FLT_MAX;
    behind = 0;
    onscreen = 0;

    for(s32 vi = 0; vi < 4; vi++) {
      f32 clip[4] = {};
      if(!roomLocalToModel(room,
        (f32)vertices[vi][0], (f32)vertices[vi][1], (f32)vertices[vi][2],
        basisX, basisY, basisZ,
        modelV[vi][0], modelV[vi][1], modelV[vi][2])) {
        return false;
      }
      transform4x4(projection, modelV[vi][0], modelV[vi][1], modelV[vi][2], 1.0f, clip);
      clipW[vi] = clip[3];
      if(!std::isfinite(clip[0]) || !std::isfinite(clip[1]) ||
         !std::isfinite(clip[3]) || std::fabs(clip[3]) < 0.000001f) {
        return false;
      }
      if(clip[3] <= 0.0f) behind = 1;

      f32 ndcX = clip[0] / clip[3];
      f32 ndcY = clip[1] / clip[3];
      screenV[vi][0] = viewportLeft + (ndcX * 0.5f + 0.5f) * viewportWidth;
      screenV[vi][1] = viewportTop + (0.5f - ndcY * 0.5f) * viewportHeight;
      if(!std::isfinite(screenV[vi][0]) || !std::isfinite(screenV[vi][1])) return false;

      if(screenV[vi][0] < minX) minX = screenV[vi][0];
      if(screenV[vi][1] < minY) minY = screenV[vi][1];
      if(screenV[vi][0] > maxX) maxX = screenV[vi][0];
      if(screenV[vi][1] > maxY) maxY = screenV[vi][1];
    }

    if(minX == FLT_MAX || minY == FLT_MAX || maxX == -FLT_MAX || maxY == -FLT_MAX) return false;
    screenBbox[0] = minX;
    screenBbox[1] = minY;
    screenBbox[2] = maxX;
    screenBbox[3] = maxY;
    screenArea = (maxX - minX) * (maxY - minY);
    if(screenArea < 0.0f || !std::isfinite(screenArea)) screenArea = 0.0f;
    onscreen = maxX >= viewportLeft &&
               maxY >= viewportTop &&
               minX <= viewportLeft + viewportWidth &&
               minY <= viewportTop + viewportHeight;
    return true;
  }

  auto formatImpactStateJson(char* out, size_t outSize, u32 player) -> void {
    if(outSize == 0) return;

    u64 hash = 1469598103934665603ull;
    s32 currentSlot = -1;
    u32 impacts = 0;
    s32 occupied = 0;
    s32 firstIndex = -1;
    s32 firstRoom = -1;
    s32 firstImpact = -1;
    f32 firstCenterX = 0.0f;
    f32 firstCenterY = 0.0f;
    f32 firstCenterZ = 0.0f;
    f32 firstWorldCenterX = 0.0f;
    f32 firstWorldCenterY = 0.0f;
    f32 firstWorldCenterZ = 0.0f;
    s32 firstHasWorld = 0;
    s32 truncated = 0;
    f32 viewportLeft = 0.0f;
    f32 viewportTop = 0.0f;
    f32 viewportWidth = 320.0f;
    f32 viewportHeight = 240.0f;
    f32 basisX = 0.0f;
    f32 basisY = 0.0f;
    f32 basisZ = 0.0f;
    u32 projectionPtr = 0;
    f32 projection[4][4] = {};
    s32 projectionValid = 0;
    char sampleJson[32768];
    size_t sampleUsed = 1;
    s32 sampleCount = 0;

    sampleJson[0] = '[';
    sampleJson[1] = '\0';

    if(player != 0 && validRdram(player + PlayerCScreenLeft, 4)) {
      viewportLeft = readF32(player + PlayerCScreenLeft);
      viewportTop = readF32(player + PlayerCScreenTop);
      viewportWidth = readF32(player + PlayerCScreenWidth);
      viewportHeight = readF32(player + PlayerCScreenHeight);
      basisX = readF32(player + PlayerCurrentModelPos + 0);
      basisY = readF32(player + PlayerCurrentModelPos + 4);
      basisZ = readF32(player + PlayerCurrentModelPos + 8);
      projectionPtr = validRdram(player + PlayerField10E0, 4) ? readU32(player + PlayerField10E0) : 0;
    }
    if(projectionPtr != 0 && validRdram(projectionPtr, 64)) {
      for(s32 row = 0; row < 4; row++) {
        for(s32 col = 0; col < 4; col++) {
          projection[row][col] = readN64MtxElem(projectionPtr, row, col);
        }
      }
      projectionValid = 1;
    }

    if(numImpactEntriesAddress != 0 && validRdram(numImpactEntriesAddress, 4)) {
      currentSlot = readS32(numImpactEntriesAddress);
    }
    if(bulletImpactBufferAddress != 0 && validRdram(bulletImpactBufferAddress, 4)) {
      impacts = readU32(bulletImpactBufferAddress);
    }

    if(impacts == 0 || !validRdram(impacts, BulletImpactStride)) {
      std::snprintf(out, outSize,
        "{\"present\":0,\"buffer_len\":%d,\"current_slot\":%d,\"ptr\":\"0x%08x\","
        "\"occupied\":0,\"first\":{\"index\":-1},\"sample\":[],"
        "\"projection\":{\"valid\":%d,\"float\":0,\"source\":\"room_matrix_field10e0\","
        "\"projection_ptr\":\"0x%08x\",\"viewport\":[%.2f,%.2f,%.2f,%.2f]},"
        "\"hash\":\"0x%016llX\"}",
        BulletImpactMax, currentSlot, impacts,
        projectionValid, projectionPtr,
        viewportLeft, viewportTop, viewportWidth, viewportHeight,
        (unsigned long long)hash);
      return;
    }

    for(s32 index = 0; index < BulletImpactMax; index++) {
      u32 impact = impacts + (u32)index * BulletImpactStride;
      if(!validRdram(impact, BulletImpactStride)) {
        truncated = 1;
        break;
      }

      s32 room = readS16(impact + BulletImpactRoom);
      if(room < 0) continue;

      s32 impactType = readS16(impact + BulletImpactType);
      s32 modelPos = readS8(impact + BulletImpactModelPos);
      s32 clearFlag = readS8(impact + BulletImpactClear);
      u32 prop = readU32(impact + BulletImpactProp);
      s32 propType = -1;
      s32 propChrnum = -1;
      s32 propObjType = -1;
      s32 propObj = -1;
      s32 propPad = -1;
      s32 v[4][3] = {};
      s32 tc[4][2] = {};
      s32 cn[4][4] = {};
      f32 worldV[4][3] = {};
      f32 modelV[4][3] = {};
      f32 screenV[4][2] = {};
      f32 clipW[4] = {};
      f32 screenBbox[4] = {};
      f32 screenArea = 0.0f;
      s32 projected = 0;
      s32 projectionBehind = 0;
      s32 projectionOnscreen = 0;
      const char* projectionSource = "room_matrix_field10e0";
      bool hasWorld = prop == 0;

      if(prop != 0 && validRdram(prop, PropRooms + 4)) {
        propType = (s32)readU8(prop + PropType);
        u32 propData = readU32(prop + PropChr);
        if(propType == 3 && validRdram(propData + ChrChrnum, 2)) {
          propChrnum = (s32)readS16(propData + ChrChrnum);
        } else if((propType == 1 || propType == 2 || propType == 4) &&
                  validRdram(propData + ObjectDamage, 4)) {
          propObjType = (s32)readU8(propData + ObjectType);
          propObj = (s32)readS16(propData + ObjectObj);
          propPad = (s32)readS16(propData + ObjectPad);
        }
      }

      for(s32 vi = 0; vi < 4; vi++) {
        u32 vertex = impact + BulletImpactVertices + (u32)vi * VtxStride;
        v[vi][0] = readS16(vertex + VtxOb + 0);
        v[vi][1] = readS16(vertex + VtxOb + 2);
        v[vi][2] = readS16(vertex + VtxOb + 4);
        tc[vi][0] = readS16(vertex + VtxTc + 0);
        tc[vi][1] = readS16(vertex + VtxTc + 2);
        cn[vi][0] = readU8(vertex + VtxCn + 0);
        cn[vi][1] = readU8(vertex + VtxCn + 1);
        cn[vi][2] = readU8(vertex + VtxCn + 2);
        cn[vi][3] = readU8(vertex + VtxCn + 3);
        if(hasWorld) {
          hasWorld = roomLocalToWorld(room,
            (f32)v[vi][0], (f32)v[vi][1], (f32)v[vi][2],
            worldV[vi][0], worldV[vi][1], worldV[vi][2]);
        }
      }

      f32 centerX = (f32)(v[0][0] + v[1][0] + v[2][0] + v[3][0]) * 0.25f;
      f32 centerY = (f32)(v[0][1] + v[1][1] + v[2][1] + v[3][1]) * 0.25f;
      f32 centerZ = (f32)(v[0][2] + v[1][2] + v[2][2] + v[3][2]) * 0.25f;
      f32 worldCenterX = centerX;
      f32 worldCenterY = centerY;
      f32 worldCenterZ = centerZ;
      if(hasWorld) {
        worldCenterX = (worldV[0][0] + worldV[1][0] + worldV[2][0] + worldV[3][0]) * 0.25f;
        worldCenterY = (worldV[0][1] + worldV[1][1] + worldV[2][1] + worldV[3][1]) * 0.25f;
        worldCenterZ = (worldV[0][2] + worldV[1][2] + worldV[2][2] + worldV[3][2]) * 0.25f;
        if(projectionValid) {
          projected = projectRoomLocalQuad(room,
            v,
            projection,
            viewportLeft, viewportTop, viewportWidth, viewportHeight,
            basisX, basisY, basisZ,
            modelV, screenV, clipW, screenBbox, screenArea,
            projectionBehind, projectionOnscreen) ? 1 : 0;
        }
      } else {
        projectionSource = "prop_matrix_not_traced";
        for(s32 vi = 0; vi < 4; vi++) {
          worldV[vi][0] = (f32)v[vi][0];
          worldV[vi][1] = (f32)v[vi][1];
          worldV[vi][2] = (f32)v[vi][2];
        }
      }

      if(firstIndex < 0) {
        firstIndex = index;
        firstRoom = room;
        firstImpact = impactType;
        firstCenterX = centerX;
        firstCenterY = centerY;
        firstCenterZ = centerZ;
        firstWorldCenterX = worldCenterX;
        firstWorldCenterY = worldCenterY;
        firstWorldCenterZ = worldCenterZ;
        firstHasWorld = hasWorld ? 1 : 0;
      }

      occupied++;
      hash = hashU32(hash, (u32)index);
      hash = hashU32(hash, (u32)(u16)room);
      hash = hashU32(hash, (u32)(u16)impactType);
      hash = hashU32(hash, (u32)(u8)modelPos);
      hash = hashU32(hash, (u32)(u8)clearFlag);
      hash = hashU32(hash, prop != 0 ? 1u : 0u);
      for(s32 vi = 0; vi < 4; vi++) {
        u32 vertex = impact + BulletImpactVertices + (u32)vi * VtxStride;
        hash = hashU32(hash, readU32(vertex + 0x00));
        hash = hashU32(hash, readU32(vertex + 0x04));
        hash = hashU32(hash, readU32(vertex + 0x08));
        hash = hashU32(hash, readU32(vertex + 0x0c));
      }

      if(sampleCount < 4 && sampleUsed < sizeof(sampleJson)) {
        s32 remaining = (s32)(sizeof(sampleJson) - sampleUsed);
        s32 written = std::snprintf(sampleJson + sampleUsed,
          (size_t)remaining,
          "%s{\"index\":%d,\"room\":%d,\"impact\":%d,\"model_pos\":%d,"
          "\"clear\":%d,\"prop\":%d,\"prop_addr\":\"0x%08x\","
          "\"prop_type\":%d,\"prop_chrnum\":%d,\"prop_obj_type\":%d,"
          "\"prop_obj\":%d,\"prop_pad\":%d,"
          "\"center\":[%.2f,%.2f,%.2f],"
          "\"world\":%d,\"world_center\":[%.2f,%.2f,%.2f],"
          "\"v\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]],"
          "\"world_v\":[[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f],"
          "[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f]],"
          "\"tc\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],"
          "\"rgba\":[[%d,%d,%d,%d],[%d,%d,%d,%d],[%d,%d,%d,%d],[%d,%d,%d,%d]],"
          "\"projection\":{\"valid\":%d,\"source\":\"%s\","
          "\"onscreen\":%d,\"behind\":%d,"
          "\"screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
          "\"screen_area\":%.2f,"
          "\"clip_w\":[%.4f,%.4f,%.4f,%.4f],"
          "\"model\":[[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f],"
          "[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f]],"
          "\"screen\":[[%.2f,%.2f],[%.2f,%.2f],"
          "[%.2f,%.2f],[%.2f,%.2f]]}}",
          sampleCount == 0 ? "" : ",",
          index, room, impactType, modelPos, clearFlag, prop != 0 ? 1 : 0,
          prop,
          propType, propChrnum, propObjType, propObj, propPad,
          centerX, centerY, centerZ,
          hasWorld ? 1 : 0,
          worldCenterX, worldCenterY, worldCenterZ,
          v[0][0], v[0][1], v[0][2],
          v[1][0], v[1][1], v[1][2],
          v[2][0], v[2][1], v[2][2],
          v[3][0], v[3][1], v[3][2],
          worldV[0][0], worldV[0][1], worldV[0][2],
          worldV[1][0], worldV[1][1], worldV[1][2],
          worldV[2][0], worldV[2][1], worldV[2][2],
          worldV[3][0], worldV[3][1], worldV[3][2],
          tc[0][0], tc[0][1],
          tc[1][0], tc[1][1],
          tc[2][0], tc[2][1],
          tc[3][0], tc[3][1],
          cn[0][0], cn[0][1], cn[0][2], cn[0][3],
          cn[1][0], cn[1][1], cn[1][2], cn[1][3],
          cn[2][0], cn[2][1], cn[2][2], cn[2][3],
          cn[3][0], cn[3][1], cn[3][2], cn[3][3],
          projected,
          projectionSource,
          projectionOnscreen,
          projectionBehind,
          screenBbox[0], screenBbox[1], screenBbox[2], screenBbox[3],
          screenArea,
          clipW[0], clipW[1], clipW[2], clipW[3],
          modelV[0][0], modelV[0][1], modelV[0][2],
          modelV[1][0], modelV[1][1], modelV[1][2],
          modelV[2][0], modelV[2][1], modelV[2][2],
          modelV[3][0], modelV[3][1], modelV[3][2],
          screenV[0][0], screenV[0][1],
          screenV[1][0], screenV[1][1],
          screenV[2][0], screenV[2][1],
          screenV[3][0], screenV[3][1]);
        if(written > 0) {
          sampleUsed += (size_t)written < (size_t)remaining ? (size_t)written : (size_t)remaining - 1u;
        }
        sampleCount++;
      }
    }

    if(sampleUsed < sizeof(sampleJson) - 1u) {
      sampleJson[sampleUsed++] = ']';
      sampleJson[sampleUsed] = '\0';
    } else {
      sampleJson[sizeof(sampleJson) - 2u] = ']';
      sampleJson[sizeof(sampleJson) - 1u] = '\0';
    }

    if(firstIndex >= 0) {
      std::snprintf(out, outSize,
        "{\"present\":1,\"buffer_len\":%d,\"current_slot\":%d,\"ptr\":\"0x%08x\","
        "\"occupied\":%d,\"truncated\":%d,\"first\":{\"index\":%d,"
        "\"room\":%d,\"impact\":%d,\"center\":[%.2f,%.2f,%.2f],"
        "\"world\":%d,\"world_center\":[%.2f,%.2f,%.2f]},"
        "\"projection\":{\"valid\":%d,\"float\":0,\"source\":\"room_matrix_field10e0\","
        "\"projection_ptr\":\"0x%08x\",\"viewport\":[%.2f,%.2f,%.2f,%.2f]},"
        "\"sample\":%s,\"hash\":\"0x%016llX\"}",
        BulletImpactMax, currentSlot, impacts, occupied, truncated, firstIndex,
        firstRoom, firstImpact, firstCenterX, firstCenterY, firstCenterZ,
        firstHasWorld, firstWorldCenterX, firstWorldCenterY, firstWorldCenterZ,
        projectionValid, projectionPtr,
        viewportLeft, viewportTop, viewportWidth, viewportHeight,
        sampleJson, (unsigned long long)hash);
    } else {
      std::snprintf(out, outSize,
        "{\"present\":1,\"buffer_len\":%d,\"current_slot\":%d,\"ptr\":\"0x%08x\","
        "\"occupied\":0,\"truncated\":%d,\"first\":{\"index\":-1},"
        "\"projection\":{\"valid\":%d,\"float\":0,\"source\":\"room_matrix_field10e0\","
        "\"projection_ptr\":\"0x%08x\",\"viewport\":[%.2f,%.2f,%.2f,%.2f]},"
        "\"sample\":[],\"hash\":\"0x%016llX\"}",
        BulletImpactMax, currentSlot, impacts, truncated,
        projectionValid, projectionPtr,
        viewportLeft, viewportTop, viewportWidth, viewportHeight,
        (unsigned long long)hash);
    }
  }

  auto propDefSizeN64(u8 type) -> u32 {
    switch(type) {
      case 1: return 256u;
      case 2: return 8u;
      case 3: return 128u;
      case 4: return 0x21u * 4u;
      case 5: return 0x20u * 4u;
      case 6: return 0x3bu * 4u;
      case 7: return 0x21u * 4u;
      case 8: return 0x22u * 4u;
      case 9: return 28u;
      case 10: return 0x40u * 4u;
      case 11: return 0x95u * 4u;
      case 12: return 0x20u * 4u;
      case 13: return 0x36u * 4u;
      case 14: return 3u * 4u;
      case 17: return 0x20u * 4u;
      case 18: return 3u * 4u;
      case 19: return 4u * 4u;
      case 20: return 0x2du * 4u;
      case 21: return 0x22u * 4u;
      case 22: return 4u * 4u;
      case 23: return 4u * 4u;
      case 24: return 1u * 4u;
      case 25: return 2u * 4u;
      case 26: return 2u * 4u;
      case 27: return 2u * 4u;
      case 28: return 2u * 4u;
      case 29: return 2u * 4u;
      case 30: return 4u * 4u;
      case 31: return 1u * 4u;
      case 32: return 4u * 4u;
      case 33: return 5u * 4u;
      case 34: return 1u * 4u;
      case 35: return 4u * 4u;
      case 36: return 0x20u * 4u;
      case 37: return 10u * 4u;
      case 38: return 4u * 4u;
      case 39: return 0x2cu * 4u;
      case 40: return 0x2du * 4u;
      case 41: return 0x20u * 4u;
      case 42: return 128u;
      case 43: return 0x20u * 4u;
      case 44: return 5u * 4u;
      case 45: return 0x38u * 4u;
      case 46: return 7u * 4u;
      case 47: return 0x25u * 4u;
      case 48: return 4u;
      default: return 4u;
    }
  }

  auto objectShotsTakenN64(u32 obj) -> s32 {
    u8 state = readU8(obj + ObjectState);
    f32 maxDamage = readF32(obj + ObjectMaxDamage);
    f32 damage = readF32(obj + ObjectDamage);
    if((state & 0x80u) == 0) {
      if(damage == 0.0f) return 0;
      return (s32)((maxDamage * 3.0f) / damage);
    }
    return (s32)(maxDamage + 4.0f);
  }

  auto objectDestroyedLevelN64(u32 obj) -> s32 {
    u8 state = readU8(obj + ObjectState);
    if((state & 0x80u) == 0) return 0;
    return ((s32)readF32(obj + ObjectMaxDamage) >> 2) + 1;
  }

  auto formatGlassPropObjectJson(char* out, size_t outSize, u32 obj, s32 setupIndex, f32 distSq) -> void {
    if(outSize == 0) return;
    if(obj == 0 || !validRdram(obj, 128)) {
      std::snprintf(out, outSize, "{\"index\":-1}");
      return;
    }

    u8 state = readU8(obj + ObjectState);
    u32 runtime = readU32(obj + ObjectRuntimeFlags);
    std::snprintf(out, outSize,
      "{\"index\":%d,\"type\":%u,\"obj\":%d,\"pad\":%d,"
      "\"state\":%u,\"runtime\":\"0x%08x\",\"remove\":%d,"
      "\"destroyed_level\":%d,\"shots\":%d,\"has_prop\":%d,\"has_model\":%d,"
      "\"damage\":%.4f,\"maxdamage\":%.4f,\"pos\":[%.2f,%.2f,%.2f],"
      "\"dist_sq\":%.2f}",
      setupIndex,
      (unsigned)readU8(obj + ObjectType),
      (s32)readS16(obj + ObjectObj),
      (s32)readS16(obj + ObjectPad),
      (unsigned)state,
      runtime,
      (runtime & 0x00000004u) != 0 ? 1 : 0,
      objectDestroyedLevelN64(obj),
      objectShotsTakenN64(obj),
      readU32(obj + ObjectProp) != 0 ? 1 : 0,
      readU32(obj + ObjectModel) != 0 ? 1 : 0,
      readF32(obj + ObjectDamage),
      readF32(obj + ObjectMaxDamage),
      readF32(obj + ObjectRuntimePos + 0),
      readF32(obj + ObjectRuntimePos + 4),
      readF32(obj + ObjectRuntimePos + 8),
      distSq);
  }

  auto formatGlassPropsJson(char* out, size_t outSize, bool hasPlayer, f32 playerX, f32 playerY, f32 playerZ) -> void {
    if(outSize == 0) return;

    constexpr s32 glassPropSampleMax = 32;
    u64 hash = 1469598103934665603ull;
    u32 propDefs = currentSetupAddress != 0 && validRdram(currentSetupAddress + CurrentSetupPropDefs, 4)
      ? readU32(currentSetupAddress + CurrentSetupPropDefs)
      : 0;
    s32 count = 0;
    s32 live = 0;
    s32 withProp = 0;
    s32 withModel = 0;
    s32 destroyed = 0;
    s32 remove = 0;
    s32 truncated = 0;
    s32 sampleTruncated = 0;
    s32 sampleCount = 0;
    size_t sampleLen = 0;
    s32 setupIndex = 0;
    s32 nearestIndex = -1;
    s32 firstRemovedIndex = -1;
    s32 firstDestroyedIndex = -1;
    u32 nearestObj = 0;
    u32 firstRemovedObj = 0;
    u32 firstDestroyedObj = 0;
    f32 nearestDistSq = 0.0f;
    f32 firstRemovedDistSq = 0.0f;
    f32 firstDestroyedDistSq = 0.0f;
    bool haveNearest = false;
    char nearestJson[512] = {};
    char firstRemovedJson[512] = {};
    char firstDestroyedJson[512] = {};
    char sampleJson[12288] = {};
    sampleJson[0] = '[';
    sampleJson[1] = 0;
    sampleLen = 1;

    if(propDefs == 0 || !validRdram(propDefs, 4)) {
      std::snprintf(out, outSize,
        "{\"present\":0,\"count\":0,\"live\":0,\"with_prop\":0,\"with_model\":0,"
        "\"destroyed\":0,\"remove\":0,\"truncated\":0,\"nearest\":{\"index\":-1},"
        "\"first_removed\":{\"index\":-1},\"first_destroyed\":{\"index\":-1},"
        "\"sample\":[],\"sample_truncated\":0,\"hash\":\"0x%016llX\"}",
        (unsigned long long)hash);
      return;
    }

    for(u32 cmd = propDefs; setupIndex < 2048 && validRdram(cmd, 4); setupIndex++) {
      u8 type = readU8(cmd + ObjectType);
      if(type == 48) break;

      if(type == 42 || type == 47) {
        u8 state = readU8(cmd + ObjectState);
        u32 runtime = readU32(cmd + ObjectRuntimeFlags);
        u32 prop = readU32(cmd + ObjectProp);
        u32 model = readU32(cmd + ObjectModel);
        bool objDestroyed = (state & 0x80u) != 0;
        bool objRemove = (runtime & 0x00000004u) != 0;
        f32 x = readF32(cmd + ObjectRuntimePos + 0);
        f32 y = readF32(cmd + ObjectRuntimePos + 4);
        f32 z = readF32(cmd + ObjectRuntimePos + 8);
        f32 distSq = 0.0f;

        count++;
        if(prop != 0) withProp++;
        if(model != 0) withModel++;
        if(prop != 0 && !objRemove) live++;
        if(objDestroyed) destroyed++;
        if(objRemove) remove++;

        if(hasPlayer) {
          f32 dx = x - playerX;
          f32 dy = y - playerY;
          f32 dz = z - playerZ;
          distSq = dx * dx + dy * dy + dz * dz;
        }

        if(!haveNearest || (hasPlayer && distSq < nearestDistSq)) {
          nearestObj = cmd;
          nearestIndex = setupIndex;
          nearestDistSq = distSq;
          haveNearest = true;
        }
        if(objRemove && firstRemovedObj == 0) {
          firstRemovedObj = cmd;
          firstRemovedIndex = setupIndex;
          firstRemovedDistSq = distSq;
        }
        if(objDestroyed && firstDestroyedObj == 0) {
          firstDestroyedObj = cmd;
          firstDestroyedIndex = setupIndex;
          firstDestroyedDistSq = distSq;
        }
        if(sampleCount < glassPropSampleMax) {
          char objectJson[512] = {};
          formatGlassPropObjectJson(objectJson, sizeof(objectJson), cmd, setupIndex, distSq);
          int written = std::snprintf(
            sampleJson + sampleLen,
            sizeof(sampleJson) - sampleLen,
            "%s%s",
            sampleCount == 0 ? "" : ",",
            objectJson);
          if(written > 0 && (size_t)written < sizeof(sampleJson) - sampleLen) {
            sampleLen += (size_t)written;
            sampleCount++;
          } else {
            sampleTruncated = 1;
          }
        } else {
          sampleTruncated = 1;
        }

        hash = hashU32(hash, (u32)setupIndex);
        hash = hashU32(hash, (u32)type);
        hash = hashU32(hash, (u32)(u16)readU16(cmd + ObjectObj));
        hash = hashU32(hash, (u32)(u16)readU16(cmd + ObjectPad));
        hash = hashU32(hash, (u32)state);
        hash = hashU32(hash, readU32(cmd + ObjectFlags));
        hash = hashU32(hash, readU32(cmd + ObjectFlags2));
        hash = hashU32(hash, runtime);
        hash = hashU32(hash, prop != 0 ? 1u : 0u);
        hash = hashU32(hash, model != 0 ? 1u : 0u);
        hash = hashU32(hash, readU32(cmd + ObjectRuntimePos + 0));
        hash = hashU32(hash, readU32(cmd + ObjectRuntimePos + 4));
        hash = hashU32(hash, readU32(cmd + ObjectRuntimePos + 8));
        hash = hashU32(hash, readU32(cmd + ObjectMaxDamage));
        hash = hashU32(hash, readU32(cmd + ObjectDamage));
      }

      u32 step = propDefSizeN64(type);
      if(step == 0) break;
      cmd += step;
    }

    if(setupIndex >= 2048) truncated = 1;

    formatGlassPropObjectJson(nearestJson, sizeof(nearestJson), nearestObj, nearestIndex, nearestDistSq);
    formatGlassPropObjectJson(firstRemovedJson, sizeof(firstRemovedJson), firstRemovedObj, firstRemovedIndex, firstRemovedDistSq);
    formatGlassPropObjectJson(firstDestroyedJson, sizeof(firstDestroyedJson), firstDestroyedObj, firstDestroyedIndex, firstDestroyedDistSq);
    if(sampleLen < sizeof(sampleJson) - 1) {
      sampleJson[sampleLen++] = ']';
      sampleJson[sampleLen] = 0;
    } else {
      std::snprintf(sampleJson, sizeof(sampleJson), "[]");
      sampleTruncated = 1;
    }

    std::snprintf(out, outSize,
      "{\"present\":1,\"count\":%d,\"live\":%d,\"with_prop\":%d,\"with_model\":%d,"
      "\"destroyed\":%d,\"remove\":%d,\"truncated\":%d,"
      "\"nearest\":%s,\"first_removed\":%s,\"first_destroyed\":%s,"
      "\"sample\":%s,\"sample_truncated\":%d,\"hash\":\"0x%016llX\"}",
      count, live, withProp, withModel, destroyed, remove, truncated,
      nearestJson[0] ? nearestJson : "{\"index\":-1}",
      firstRemovedJson[0] ? firstRemovedJson : "{\"index\":-1}",
      firstDestroyedJson[0] ? firstDestroyedJson : "{\"index\":-1}",
      sampleJson,
      sampleTruncated,
      (unsigned long long)hash);
  }

  auto findChrByNum(s32 wantedChrnum, s32* slotOut) -> u32 {
    if(slotOut) *slotOut = -1;
    if(wantedChrnum < 0 || chrSlotsAddress == 0 || numChrSlotsAddress == 0 || !validRdram(chrSlotsAddress, 4) || !validRdram(numChrSlotsAddress, 4)) return 0;
    u32 chrSlots = readU32(chrSlotsAddress);
    s32 slots = readS32(numChrSlotsAddress);
    if(slots < 0 || slots > 256 || !validRdram(chrSlots, ChrStride)) return 0;

    for(s32 index = 0; index < slots; index++) {
      u32 chr = chrSlots + (u32)index * ChrStride;
      if(!validRdram(chr, ChrStride)) break;
      if(readS16(chr + ChrChrnum) == wantedChrnum) {
        if(slotOut) *slotOut = index;
        return chr;
      }
    }
    return 0;
  }

  auto formatTrackedChrJson(char* out, size_t outSize, bool hasPlayer, f32 playerX, f32 playerY, f32 playerZ, s32 renderedRoomsCount) -> void {
    if(outSize == 0) return;
    out[0] = '\0';
    if(traceChrnum < 0) return;

    s32 slot = -1;
    u32 chr = findChrByNum(traceChrnum, &slot);
    if(chr == 0) {
      std::snprintf(out, outSize, "\"track\":{\"chrnum\":%d,\"present\":0},", traceChrnum);
      return;
    }

    u32 prop = readU32(chr + ChrProp);
    u32 model = readU32(chr + ChrModel);
    u32 ailist = readU32(chr + ChrAilist);
    s32 aioffset = readU16(chr + ChrAioffset);
    s32 aiCmd = -1;
    s32 aiArg0 = -1;
    if(validRdram(ailist + (u32)aioffset, 2)) {
      aiCmd = readU8(ailist + (u32)aioffset);
      aiArg0 = readU8(ailist + (u32)aioffset + 1);
    }

    s32 stanRoomValue = -1;
    s32 firstRoom = -1;
    s32 roomCount = 0;
    s32 anyRendered = 0;
    s32 firstRendered = 0;
    char roomsJson[64] = "[]";
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    s32 onscreen = 0;
    s32 propFlags = 0;
    s32 rendered = 0;
    bool validProp = validRdram(prop, PropRooms + 4) && readU8(prop + PropType) == 3;

    if(validProp) {
      u32 stan = readU32(prop + PropStan);
      stanRoomValue = stanRoom(stan);
      x = readF32(prop + PropPos + 0);
      y = readF32(prop + PropPos + 4);
      z = readF32(prop + PropPos + 8);
      propFlags = readU8(prop + PropFlags);
      onscreen = (propFlags & 0x02) != 0 ? 1 : 0;
      rendered = propRenderedInRooms(prop, stanRoomValue, renderedRoomsCount) ? 1 : 0;
      formatPropRoomsJson(roomsJson, sizeof(roomsJson), prop, stanRoomValue, renderedRoomsCount, &firstRoom, &roomCount, &anyRendered, &firstRendered);
    }

    s32 hiddenBits = (s32)readU16(chr + ChrHidden);
    u32 chrflags = readU32(chr + ChrChrflags);
    f32 damage = readF32(chr + ChrDamage);
    f32 maxDamage = readF32(chr + ChrMaxDamage);
    f32 distToBond = 0.0f;
    if(hasPlayer && validProp) {
      f32 dx = x - playerX;
      f32 dy = y - playerY;
      f32 dz = z - playerZ;
      distToBond = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    }

    std::snprintf(out, outSize,
      "\"track\":{\"chrnum\":%d,\"present\":1,\"slot\":%d,"
      "\"hidden\":%d,\"hidden_bits\":%d,\"chrflags\":\"0x%08x\","
      "\"alive\":%d,\"flag_hidden\":%d,\"flag_update_action\":%d,\"bg_ai\":%d,"
      "\"action\":%d,\"alert\":%d,\"sleep\":%d,"
      "\"firecount\":%d,\"shotbondsum\":%.5f,\"accuracyrating\":%d,"
      "\"damage\":%.4f,\"maxdamage\":%.4f,\"padpreset\":%d,\"chrpreset\":%d,"
      "\"dist_to_bond\":%.2f,\"pos\":[%.2f,%.2f,%.2f],"
      "\"prev\":[%.2f,%.2f,%.2f],\"ground\":%.2f,"
      "\"ai\":{\"ptr\":\"0x%08x\",\"offset\":%d,\"return\":%d,\"cmd\":%d,\"arg0\":%d},"
      "\"patrol\":{\"path\":\"0x%08x\",\"nextstep\":%d,\"forward\":%d,"
      "\"mode\":%d,\"age\":%d,\"lastvisible\":%d,\"speed\":%.4f,"
      "\"segdistdone\":%.2f,\"segdisttotal\":%.2f},"
      "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,\"any_rendered\":%d,"
      "\"first_rendered\":%d,\"rooms\":%s},"
      "\"render\":{\"prop_flags\":\"0x%08x\",\"onscreen\":%d,\"rendered\":%d,"
      "\"seen_onscreen\":%d,\"field20\":%d,\"model_mtx\":%d}},",
      traceChrnum, slot,
      hiddenBits != 0 ? 1 : 0, hiddenBits, chrflags,
      damage < maxDamage ? 1 : 0,
      (chrflags & 0x00000400u) != 0 ? 1 : 0,
      (chrflags & 0x00040000u) != 0 ? 1 : 0,
      (hiddenBits & 0x0200) != 0 ? 1 : 0,
      readS8(chr + ChrActiontype), readU8(chr + ChrAlertness), readS8(chr + ChrSleep),
      readU8(chr + ChrFirecount) + readU8(chr + ChrFirecount + 1),
      readF32(chr + ChrShotbondsum),
      readS8(chr + ChrAccuracyrating),
      damage, maxDamage, readS16(chr + ChrPadpreset1), readS16(chr + ChrChrpreset1),
      distToBond, x, y, z,
      readF32(chr + ChrPrevpos + 0), readF32(chr + ChrPrevpos + 4), readF32(chr + ChrPrevpos + 8),
      readF32(chr + ChrGround),
      ailist, aioffset, readS16(chr + ChrAireturnlist), aiCmd, aiArg0,
      readU32(chr + ChrActPatrolPath), readS32(chr + ChrActPatrolNextstep), readS32(chr + ChrActPatrolForward),
      readS8(chr + ChrActPatrolWayMode), readS32(chr + ChrActPatrolWayAge), readS32(chr + ChrActPatrolLastVisible60),
      readF32(chr + ChrActPatrolSpeed), readF32(chr + ChrActPatrolWaySegDone), readF32(chr + ChrActPatrolWaySegTotal),
      stanRoomValue, firstRoom, roomCount, anyRendered, firstRendered, roomsJson,
      propFlags, onscreen, rendered,
      (chrflags & 0x00000008u) != 0 ? 1 : 0,
      readU32(chr + ChrField20) != 0 ? 1 : 0,
      validRdram(model + ModelRenderPos, 4) && readU32(model + ModelRenderPos) != 0 ? 1 : 0);
  }

  auto insertActorSample(OracleActorSample* samples, s32& sampleCount, const OracleActorSample& sample) -> void {
    constexpr s32 maxSamples = 6;
    s32 slot = sampleCount;
    if(slot < maxSamples) {
      sampleCount++;
    } else if(sample.distSq < samples[maxSamples - 1].distSq) {
      slot = maxSamples - 1;
    } else {
      return;
    }

    while(slot > 0 && sample.distSq < samples[slot - 1].distSq) {
      samples[slot] = samples[slot - 1];
      slot--;
    }
    samples[slot] = sample;
  }

  auto formatActorSummaryJson(char* out, size_t outSize, bool hasPlayer, f32 playerX, f32 playerY, f32 playerZ, s32 renderedRoomsCount) -> void {
    if(outSize == 0) return;

    s32 slots = 0;
    s32 live = 0;
    s32 aliveCount = 0;
    s32 hiddenCount = 0;
    s32 onscreenCount = 0;
    s32 renderedCount = 0;
    s32 sampleCount = 0;
    OracleActorSample samples[6];

    if(chrSlotsAddress != 0 && numChrSlotsAddress != 0 && validRdram(chrSlotsAddress, 4) && validRdram(numChrSlotsAddress, 4)) {
      u32 chrSlots = readU32(chrSlotsAddress);
      slots = readS32(numChrSlotsAddress);
      if(slots < 0 || slots > 256 || !validRdram(chrSlots, ChrMaxDamage + 4)) {
        slots = 0;
      }

      for(s32 index = 0; index < slots; index++) {
        u32 chr = chrSlots + (u32)index * ChrStride;
        if(!validRdram(chr, ChrMaxDamage + 4)) break;

        s32 chrnum = readS16(chr + ChrChrnum);
        u32 model = readU32(chr + ChrModel);
        u32 prop = readU32(chr + ChrProp);
        if(model == 0 || chrnum < 0 || chrnum >= 1000) continue;

        live++;

        s32 hiddenBits = (s32)readU16(chr + ChrHidden);
        f32 damage = readF32(chr + ChrDamage);
        f32 maxDamage = readF32(chr + ChrMaxDamage);
        s32 alive = damage < maxDamage ? 1 : 0;
        s32 onscreen = 0;
        s32 rendered = 0;
        s32 room = -1;
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        bool validProp = validRdram(prop, PropRooms + 4) && readU8(prop + PropType) == 3;

        if(alive) aliveCount++;
        if(hiddenBits != 0) hiddenCount++;

        if(validProp) {
          u32 stan = readU32(prop + PropStan);
          room = stanRoom(stan);
          x = readF32(prop + PropPos + 0);
          y = readF32(prop + PropPos + 4);
          z = readF32(prop + PropPos + 8);
          onscreen = (readU8(prop + PropFlags) & 0x02) != 0 ? 1 : 0;
          rendered = propRenderedInRooms(prop, room, renderedRoomsCount) ? 1 : 0;
        }

        if(onscreen) onscreenCount++;
        if(rendered) renderedCount++;

        if(validProp) {
          f32 dx = hasPlayer ? x - playerX : 0.0f;
          f32 dy = hasPlayer ? y - playerY : 0.0f;
          f32 dz = hasPlayer ? z - playerZ : 0.0f;
          OracleActorSample sample;
          sample.slot = index;
          sample.chrnum = chrnum;
          sample.action = readS8(chr + ChrActiontype);
          sample.alert = readU8(chr + ChrAlertness);
          sample.sleep = readS8(chr + ChrSleep);
          sample.hidden = hiddenBits != 0 ? 1 : 0;
          sample.hiddenBits = hiddenBits;
          sample.alive = alive;
          sample.onscreen = onscreen;
          sample.rendered = rendered;
          sample.room = room;
          sample.distSq = (dx * dx) + (dy * dy) + (dz * dz);
          sample.x = x;
          sample.y = y;
          sample.z = z;
          insertActorSample(samples, sampleCount, sample);
        }
      }
    }

    size_t used = 0;
    appendText(out, outSize, used,
      "{\"slots\":%d,\"live\":%d,\"alive\":%d,\"hidden\":%d,"
      "\"onscreen\":%d,\"rendered\":%d,\"sample\":[",
      slots, live, aliveCount, hiddenCount, onscreenCount, renderedCount);
    for(s32 index = 0; index < sampleCount; index++) {
      const auto& sample = samples[index];
      appendText(out, outSize, used,
        "%s{\"slot\":%d,\"chrnum\":%d,\"action\":%d,\"alert\":%d,\"sleep\":%d,"
        "\"hidden\":%d,\"hidden_bits\":%d,\"alive\":%d,\"onscreen\":%d,"
        "\"rendered\":%d,\"room\":%d,\"dist\":%.2f,\"pos\":[%.2f,%.2f,%.2f]}",
        index == 0 ? "" : ",",
        sample.slot, sample.chrnum, sample.action, sample.alert, sample.sleep,
        sample.hidden, sample.hiddenBits, sample.alive, sample.onscreen,
        sample.rendered, sample.room,
        std::sqrt(sample.distSq),
        sample.x, sample.y, sample.z);
    }
    appendText(out, outSize, used, "]}");

    if(used >= outSize) {
      std::snprintf(out, outSize,
        "{\"slots\":%d,\"live\":%d,\"alive\":%d,\"hidden\":%d,"
        "\"onscreen\":%d,\"rendered\":%d,\"sample\":[]}",
        slots, live, aliveCount, hiddenCount, onscreenCount, renderedCount);
    }
  }

  /*
   * Combat / floor-field oracle (FID-0032). Emits the "combat_oracle" namespace
   * with schema parity to the native tracer (port_trace.c traceBuildCombatOracleJson).
   * See docs/fidelity/combat_oracle_fields.md. projectiles[] is emitted empty on the
   * ares side (the prop position list is not mapped in the symbol layout yet) — a
   * documented instrumentation-gap sub-finding; the native side populates it.
   */
  auto formatCombatOracleJson(char* out, size_t outSize, bool hasPlayer, u32 player, s32 renderedRoomsCount) -> void {
    if(outSize == 0) return;
    (void)renderedRoomsCount;
    size_t used = 0;
    s32 global = readS32(globalTimerAddress);

    appendText(out, outSize, used, "\"combat_oracle\":{\"guards\":[");

    s32 emitted = 0;
    s32 overflow = 0;
    if(chrSlotsAddress != 0 && numChrSlotsAddress != 0 && validRdram(chrSlotsAddress, 4) && validRdram(numChrSlotsAddress, 4)) {
      u32 chrSlots = readU32(chrSlotsAddress);
      s32 slots = readS32(numChrSlotsAddress);
      if(slots < 0 || slots > 256 || !validRdram(chrSlots, ChrMaxDamage + 4)) slots = 0;

      for(s32 index = 0; index < slots; index++) {
        u32 chr = chrSlots + (u32)index * ChrStride;
        if(!validRdram(chr, ChrShotbondsum + 4)) break;

        s32 chrnum = readS16(chr + ChrChrnum);
        u32 model = readU32(chr + ChrModel);
        u32 prop = readU32(chr + ChrProp);
        if(model == 0 || chrnum < 0 || chrnum >= 1000) continue;
        if(!(validRdram(prop, PropRooms + 4) && readU8(prop + PropType) == 3)) continue;

        if(emitted >= 64) { overflow = 1; break; }

        f32 x = readF32(prop + PropPos + 0);
        f32 y = readF32(prop + PropPos + 4);
        f32 z = readF32(prop + PropPos + 8);
        s32 actiontype = readS8(chr + ChrActiontype);
        s32 aimode = readU8(chr + ChrAlertness);
        f32 health = readF32(chr + ChrMaxDamage) - readF32(chr + ChrDamage);
        f32 shotbondsum = readF32(chr + ChrShotbondsum);
        s32 flagsOnscreen = (readU8(prop + PropFlags) & 0x02) != 0 ? 1 : 0;
        s32 lastSee = readS32(chr + ChrLastseetarget60);
        s32 targetVisible = (lastSee > 0 && (global - lastSee) < 600) ? 1 : 0;
        u64 animHash = animHeaderHash(readU32(model + ModelAnim));
        s32 room = stanRoom(readU32(prop + PropStan));

        appendText(out, outSize, used,
          "%s{\"chrnum\":%d,\"pos\":[%.2f,%.2f,%.2f],"
          "\"actiontype\":%d,\"aimode\":%d,\"health\":%.4f,"
          "\"shotbondsum\":%.4f,\"flags_onscreen\":%d,"
          "\"target_visible\":%d,\"anim_hash\":\"0x%016llX\",\"room\":%d}",
          emitted == 0 ? "" : ",",
          chrnum, x, y, z,
          actiontype, aimode, health, shotbondsum,
          flagsOnscreen, targetVisible,
          (unsigned long long)animHash, room);
        emitted++;
      }
    }
    appendText(out, outSize, used, "],\"guards_overflow\":%d,", overflow);

    /* floor{} — player's current stan tile */
    s32 stanIdVal = -1;
    s32 stanRoomVal = -1;
    s32 stanFlagsVal = -1;
    f32 height = 0.0f;
    if(hasPlayer && player != 0) {
      u32 tile = readU32(player + PlayerField488 + CollisionCurrentTile);
      if(tile != 0) {
        stanIdVal = stanId(tile);
        stanRoomVal = stanRoom(tile);
        stanFlagsVal = stanFlags(tile);
      }
      height = readF32(player + PlayerStanHeight);
    }
    appendText(out, outSize, used,
      "\"floor\":{\"stan_id\":%d,\"stan_room\":%d,\"stan_flags\":%d,\"height\":%.2f},",
      stanIdVal, stanRoomVal, stanFlagsVal, height);

    /* combat{} — scalar player summary. hits_landed_total has no retail-equivalent
     * accumulator read here (documented divergence); shots_fired_total likewise 0. */
    f32 playerHealth = 0.0f;
    f32 playerArmor = 0.0f;
    /* FID-0063: g_randomSeed is a 64-bit doubleword; the LOW word lives at
     * base+4 (big-endian). readU32(base) is the HIGH word, which collapses to
     * 0/1 after the first randomGetNext step (the PRNG state is 33-bit), so it
     * made combat.rng_seed diverge from native's low-word emission by
     * construction. The movement-record "rng" block (readU64) was always
     * correct; only this combat-block scalar was wrong. */
    u32 rngSeedLow = readU32(randomSeedAddress + 4);
    if(hasPlayer && player != 0) {
      playerHealth = readF32(player + PlayerBondHealth);
      playerArmor = readF32(player + PlayerBondArmour);
    }
    appendText(out, outSize, used,
      "\"combat\":{\"player_health\":%.4f,\"player_armor\":%.4f,"
      "\"shots_fired_total\":0,\"hits_landed_total\":0,\"rng_seed\":\"0x%08X\"},",
      playerHealth, playerArmor, rngSeedLow);

    /* projectiles[] — prop list not mapped ares-side yet (documented gap) */
    appendText(out, outSize, used, "\"projectiles\":[],\"projectiles_overflow\":0},");

    if(used >= outSize) {
      std::snprintf(out, outSize,
        "\"combat_oracle\":{\"guards\":[],\"guards_overflow\":1,"
        "\"floor\":{\"stan_id\":-1,\"stan_room\":-1,\"stan_flags\":-1,\"height\":0.00},"
        "\"combat\":{\"player_health\":0.0000,\"player_armor\":0.0000,"
        "\"shots_fired_total\":0,\"hits_landed_total\":0,\"rng_seed\":\"0x00000000\"},"
        "\"projectiles\":[],\"projectiles_overflow\":1},");
    }
  }

  auto resolvePadStan(s32 pad, u32* padTableOut, u32* padAddressOut) -> u32 {
    if(padTableOut) *padTableOut = 0;
    if(padAddressOut) *padAddressOut = 0;
    if(pad < 0) return 0;
    u32 pads = 0;
    u32 padIndex = (u32)pad;
    u32 padSize = PadRecordSize;
    if(pad >= 10000) {
      pads = readU32(currentSetupAddress + CurrentSetupBoundPads);
      padIndex = (u32)(pad - 10000);
      padSize = BoundPadRecordSize;
    } else {
      pads = readU32(currentSetupAddress + CurrentSetupPads);
    }
    if(padTableOut) *padTableOut = pads;
    if(!validRdram(pads, padSize)) return 0;
    u32 padAddress = pads + padIndex * padSize;
    if(padAddressOut) *padAddressOut = padAddress;
    if(!validRdram(padAddress, padSize)) return 0;
    u32 stan = readU32(padAddress + PadRecordStan);
    if(!validRdram(stan, 4)) return 0;
    return stan;
  }

  auto applyForcePlayerEvents(u32 player, u32 prop, u64 gameplayFrame) -> void {
    if(forcePlayerEventCount == 0 || gameplayFrame == 0) return;
    if(!validPlayerPointer(player) || prop == 0 || !validRdram(prop, PropPos + 12)) return;

    for(u32 index = 0; index < forcePlayerEventCount; index++) {
      const auto& event = forcePlayerEvents[index];
      if(gameplayFrame < event.start || gameplayFrame > event.end) continue;

      constexpr f32 pi = 3.14159265358979323846f;
      f32 yaw = event.yawDeg * (pi / 180.0f);
      f32 pitch = event.pitchDeg * (pi / 180.0f);
      f32 pitchCos = std::cos(pitch);
      f32 pitchSin = std::sin(pitch);
      f32 viewX = -std::sin(yaw) * pitchCos;
      f32 viewY = pitchSin;
      f32 viewZ = std::cos(yaw) * pitchCos;
      f32 upX = std::sin(yaw) * pitchSin;
      f32 upY = pitchCos;
      f32 upZ = -std::cos(yaw) * pitchSin;
      f32 pitchDeg360 = std::fmod(event.pitchDeg, 360.0f);
      f32 floorY = event.eyeOffset > 0.0f ? event.y - event.eyeOffset : event.y;
      u32 padTable = 0;
      u32 padAddress = 0;
      u32 stan = resolvePadStan(event.targetPad, &padTable, &padAddress);

      if(pitchDeg360 < 0.0f) pitchDeg360 += 360.0f;

      writeVec3(prop + PropPos, event.x, event.y, event.z);
      // current_model_pos is the room render basis, not Bond's viewer
      // position. Let the ROM maintain it so forced visual checkpoints keep
      // the same world-to-room transform that the renderer consumes.
      writeVec3(player + PlayerBondPrevPos, event.x, event.y, event.z);
      writeVec3(player + PlayerCollisionPos, event.x, event.y, event.z);
      writeVec3(player + PlayerField488 + CollisionFloor, event.x, floorY, event.z);
      writeVec3(player + PlayerField488 + CollisionCameraPos, event.x, event.y, event.z);
      writeVec3(player + PlayerField488 + CollisionTheta, viewX, 0.0f, viewZ);
      writeVec3(player + PlayerField488 + CollisionAppliedView, viewX, viewY, viewZ);
      writeVec3(player + PlayerField488 + CollisionAppliedUp, upX, upY, upZ);
      writeVec3(player + PlayerHeadLook, 0.0f, 0.0f, 1.0f);
      writeVec3(player + PlayerHeadUp, 0.0f, 1.0f, 0.0f);
      writeF32(player + PlayerField488 + CollisionRadius, 30.0f);
      writeF32(player + PlayerField6C, floorY / 0.170000016689f);
      writeF32(player + PlayerField70, floorY);
      writeF32(player + PlayerStanHeight, floorY);
      writeF32(player + PlayerSpeedTheta, 0.0f);
      writeF32(player + PlayerSpeedVerta, 0.0f);
      writeF32(player + PlayerSpeedSideways, 0.0f);
      writeF32(player + PlayerSpeedStrafe, 0.0f);
      writeF32(player + PlayerSpeedForwards, 0.0f);
      writeF32(player + PlayerSpeedGo, 0.0f);
      writeF32(player + PlayerVvTheta, event.yawDeg);
      writeF32(player + PlayerVvVerta, event.pitchDeg);
      writeF32(player + PlayerVvVerta360, pitchDeg360);
      writeF32(player + PlayerVvCosVerta, pitchCos);
      writeF32(player + PlayerVvSinVerta, pitchSin);
      writeVec3(
        player + PlayerField3B8,
        event.x / 0.100000023842f,
        event.y / 0.100000023842f,
        event.z / 0.100000023842f
      );
      writeU32(player + PlayerViewMode, 0);
      if(stan != 0) {
        writeU32(player + PlayerField488 + CollisionCurrentTile, stan);
        writeU32(player + PlayerField488 + CollisionPortalTile, stan);
        writeU32(prop + PropStan, stan);
        s32 room = stanRoom(stan);
        if(room >= 0 && bgCurrentRoomAddress != 0) writeU32(bgCurrentRoomAddress, (u32)room);
      }
      if(cameraModeAddress != 0) writeU32(cameraModeAddress, 4);
      if(cameraAfterCinemaAddress != 0) writeU32(cameraAfterCinemaAddress, 0);
      if(cameraTransitionTimerAddress != 0) writeF32(cameraTransitionTimerAddress, 0.0f);

      forcePlayerApplyCount++;
      lastForcePlayerEvent = (int)index;
      lastForcePlayerGameplayFrame = gameplayFrame;
      lastForcePlayerPad = event.targetPad;
      lastForcePlayerStan = stan;
      lastForcePlayerStanRoom = stanRoom(stan);
      lastForcePlayerPadTable = padTable;
      lastForcePlayerPadAddress = padAddress;
      lastForcePlayerFloorY = floorY;
    }
  }

  auto applyCrosshairEvents(u32 player, u64 gameplayFrame) -> void {
    if(crosshairEventCount == 0 || gameplayFrame == 0) return;
    if(!validPlayerPointer(player) || !validRdram(player + PlayerCrosshairAngle, 8) || !validRdram(player + PlayerFieldFFC, 8)) return;

    for(u32 index = 0; index < crosshairEventCount; index++) {
      const auto& event = crosshairEvents[index];
      if(gameplayFrame < event.start || gameplayFrame > event.end) continue;

      writeF32(player + PlayerCrosshairAngle + 0, event.x);
      writeF32(player + PlayerCrosshairAngle + 4, event.y);
      writeF32(player + PlayerFieldFFC + 0, event.x);
      writeF32(player + PlayerFieldFFC + 4, event.y);

      crosshairApplyCount++;
      lastCrosshairEvent = (int)index;
      lastCrosshairGameplayFrame = gameplayFrame;
      lastCrosshairX = event.x;
      lastCrosshairY = event.y;
    }
  }

  auto applyRngSeedEvents(u64 gameplayFrame) -> void {
    if(rngSeedEventCount == 0 || gameplayFrame == 0 || randomSeedAddress == 0) return;
    if(!validRdram(randomSeedAddress, 8)) return;

    for(u32 index = 0; index < rngSeedEventCount; index++) {
      auto& event = rngSeedEvents[index];
      if((event.applied && !rngSeedRepeat) || event.frame != gameplayFrame) continue;

      writeU64(randomSeedAddress, event.seed);
      event.applied = true;
      rngSeedApplyCount++;
      lastRngSeedEvent = (int)index;
      lastRngSeedGameplayFrame = gameplayFrame;
      lastRngSeedValue = event.seed;
      fprintf(stderr,
        "mgb64 oracle: applied RNG seed event %u gameplay_frame=%llu seed=0x%016llX\n",
        index,
        (unsigned long long)gameplayFrame,
        (unsigned long long)event.seed);
    }
  }

  /* FID-0063: one line per retail randomGetNext() entry. `seed` is
   * g_randomSeed BEFORE this call's step. This is NOT a per-line "exactly one
   * PRNG step apart" log: the RDRAM-sampled seed lags the guest dcache
   * writeback by a few draws and catches up in bursts, so most consecutive
   * lines repeat the same seed and single-pair gaps reach into the hundreds
   * (measured: docs/fidelity/derivations/FID-0063-rng-phase.md sec 9.1). The
   * offline analyzer instead proves completeness against the LOCK chain
   * established by a seed WRITE (randomSetSeed / scripted lock): every
   * sampled seed after the write is on that chain in monotone order, with
   * nonnegative lag (line offset - chain position) everywhere and lag == 0 at
   * every frame-boundary catch-up point. `ra` is the guest $ra at entry
   * (caller PC = ra - 8 for a jal). `global` is g_GlobalTimer at entry (exact
   * sim-tick attribution, no VI-window smear). */
  auto rngPcCall(u64 ra) -> void {
    if(!rngPcTrace) return;
    rngPcCallIndex++;
    u64 seed = randomSeedAddress != 0 && validRdram(randomSeedAddress, 8)
      ? readU64(randomSeedAddress)
      : 0;
    s32 global = readS32(globalTimerAddress);
    fprintf(rngPcTrace,
      "{\"i\":%llu,\"ra\":\"0x%08X\",\"seed\":\"0x%016llX\",\"global\":%d,\"vf\":%llu}\n",
      (unsigned long long)rngPcCallIndex,
      (u32)ra,
      (unsigned long long)seed,
      global,
      (unsigned long long)videoFrame);
  }

  auto validPlayerPointer(u32 player) -> bool {
    return player != 0 && validRdram(player, PlayerSpeedGo + 4);
  }

  auto targetStageMatches(s32 currentStage) -> bool {
    return targetStage < 0 || currentStage == targetStage;
  }

  auto selectedPlayer(const char*& source) -> u32 {
    u32 player = readU32(currentPlayerAddress);
    source = "current";
    if(validPlayerPointer(player)) return player;

    for(u32 index = 0; index < 4; index++) {
      player = readU32(playerPointersAddress + index * 4);
      if(!validPlayerPointer(player)) continue;
      switch(index) {
      case 0: source = "players[0]"; break;
      case 1: source = "players[1]"; break;
      case 2: source = "players[2]"; break;
      default: source = "players[3]"; break;
      }
      return player;
    }

    source = "none";
    return readU32(currentPlayerAddress);
  }

  auto hasGameplayPlayer() -> bool {
    const char* source = nullptr;
    return targetStageMatches(readS32(currentStageToLoadAddress)) && validPlayerPointer(selectedPlayer(source));
  }

  auto currentMenu() -> s32 {
    if(currentMenuAddress == 0 || !validRdram(currentMenuAddress, 4)) return MenuUnknown;
    return readS32(currentMenuAddress);
  }

  auto stockMenuInputClosed(bool hasPlayer) -> bool {
    if(gameplayTimelineStarted) return true;

    s32 menu = currentMenu();
    if(menu == MenuRunStage) return true;

    s32 currentStage = readS32(currentStageToLoadAddress);
    if(closeMenuOnPlayer && hasPlayer && targetStageMatches(currentStage)) {
      return true;
    }

    s32 global = readS32(globalTimerAddress);
    if(gameplayStartGlobal > 0 && targetStageMatches(currentStage) && global >= gameplayStartGlobal) {
      return true;
    }

    return hasPlayer && gameplayStartGlobal <= 0;
  }

  auto currentGameplayFrame() -> u64 {
    const char* source = nullptr;
    u32 player = selectedPlayer(source);
    s32 currentStage = readS32(currentStageToLoadAddress);
    if(!targetStageMatches(currentStage) || !validPlayerPointer(player)) return 0;

    u32 prop = readU32(player + PlayerProp);
    if(prop == 0 || !validRdram(prop, PropPos + 12)) return 0;

    s32 global = readS32(globalTimerAddress);
    if(global < gameplayStartGlobal) return 0;

    if(!gameplayTimelineStarted) {
      gameplayTimelineStarted = true;
      gameplayOriginGlobal = gameplayStartGlobal > 0 ? gameplayStartGlobal : global;
      gameplayOriginInputFrame = inputFrame;
    }

    u32 speedFrames = gameplaySpeedFrames;
    if(speedFrames == 0) {
      f32 delta = readF32(globalTimerDeltaAddress);
      s32 rounded = (s32)std::lround(delta);
      speedFrames = rounded >= 1 && rounded <= 10 ? (u32)rounded : 1;
    }
    if(speedFrames == 0) speedFrames = 1;
    if(global <= gameplayOriginGlobal) return 1;
    return (u64)((global - gameplayOriginGlobal) / (s32)speedFrames) + 1;
  }

  auto traceFrame() -> void {
    configure();
    if(complete) return;

    videoFrame++;

    const char* playerSource = nullptr;
    u32 player = selectedPlayer(playerSource);
    s32 currentStage = readS32(currentStageToLoadAddress);
    bool hasPlayer = targetStageMatches(currentStage) && validPlayerPointer(player);
    u64 gameplayFrame = currentGameplayFrame();
    u32 players[4] = {
      readU32(playerPointersAddress + 0),
      readU32(playerPointersAddress + 4),
      readU32(playerPointersAddress + 8),
      readU32(playerPointersAddress + 12),
    };
    if(hasPlayer && gameplayFrame != 0) {
      applyRngSeedEvents(gameplayFrame);
      applyForcePlayerEvents(player, readU32(player + PlayerProp), gameplayFrame);
      applyCrosshairEvents(player, gameplayFrame);
    }

    f32 posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    f32 colX = 0.0f, colY = 0.0f, colZ = 0.0f;
    f32 camX = 0.0f, camY = 0.0f, camZ = 0.0f;
    f32 camTargetX = 0.0f, camTargetY = 0.0f, camTargetZ = 0.0f;
    f32 camUpX = 0.0f, camUpY = 0.0f, camUpZ = 0.0f;
    f32 camFloorX = 0.0f, camFloorY = 0.0f, camFloorZ = 0.0f;
    f32 camDx = 0.0f, camDy = 0.0f, camDz = 0.0f;
    f32 renderCamX = 0.0f, renderCamY = 0.0f, renderCamZ = 0.0f;
    f32 renderCamTargetX = 0.0f, renderCamTargetY = 0.0f, renderCamTargetZ = 0.0f;
    f32 renderCamDx = 0.0f, renderCamDy = 0.0f, renderCamDz = 0.0f;
    f32 facingX = 0.0f, facingY = 0.0f, facingZ = 0.0f;
    f32 roomBasisX = 0.0f, roomBasisY = 0.0f, roomBasisZ = 0.0f;
    f32 vvTheta = 0.0f, vvVerta = 0.0f, vvVerta360 = 0.0f;
    f32 vvCosVerta = 0.0f, vvSinVerta = 0.0f;
    f32 field70 = 0.0f, stanHeight = 0.0f;
    f32 colourFadeMax = 0.0f;
    f32 speedForwards = 0.0f, speedSideways = 0.0f;
    f32 speedGo = 0.0f, speedStrafe = 0.0f, speedBoost = 0.0f;
    f32 speedTheta = 0.0f, speedVerta = 0.0f;
    f32 headX = 0.0f, headY = 0.0f, headZ = 0.0f;
    f32 headLookX = 0.0f, headLookY = 0.0f, headLookZ = 0.0f;
    f32 headUpX = 0.0f, headUpY = 0.0f, headUpZ = 0.0f;
    f32 prevX = 0.0f, prevY = 0.0f, prevZ = 0.0f;
    s32 speedMaxTime60 = 0;
    s32 watchPause = 0;
    s32 watchState = 0;
    s32 outsideWatchMenu = 0;
    s32 watchOpenReq = 0;
    s32 pausingFlag = 0;
    s32 handItemRight = 0, handItemLeft = 0;
    s32 handPendingRight = 0, handPendingLeft = 0;
    s32 handInvisibleRight = 0, handInvisibleLeft = 0;
    s32 handLockRight = 0, handLockLeft = 0;
    s32 handWeaponRight = 0, handWeaponLeft = 0;
    s32 handWatchmenuRight = 0, handWatchmenuLeft = 0;
    s32 handNextRight = -1, handNextLeft = -1;
    s32 handMagRight = 0, handMagLeft = 0;
    s32 handAnimRight = 0, handAnimLeft = 0;
    s32 handFiringRight = 0, handFiringLeft = 0;
    s32 handHoldRight = 0, handHoldLeft = 0;
    s32 handDetStateRight = 0;
    f32 crosshairX = 0.0f;
    f32 crosshairY = 0.0f;
    f32 fieldFfcX = 0.0f;
    f32 fieldFfcY = 0.0f;
    f32 bondHealth = 0.0f;
    f32 bondArmour = 0.0f;
    f32 actualHealth = 0.0f;
    f32 actualArmour = 0.0f;
    s32 damageShowTime = -1;
    s32 healthShowTime = -1;
    s32 damageType = -1;
    s32 healthDamageType = -1;
    s32 colourScreenRed = 0;
    s32 colourScreenGreen = 0;
    s32 colourScreenBlue = 0;
    f32 colourScreenFrac = 0.0f;
    char handRightVmJson[2048] = "{}";
    char handLeftVmJson[2048] = "{}";
    s32 viewLeft = 0, viewTop = 0, viewWidth = 0, viewHeight = 0;
    f32 cScreenLeft = 0.0f, cScreenTop = 0.0f, cScreenWidth = 0.0f, cScreenHeight = 0.0f;
    s32 tileRoom = -1, portalRoom = -1, propRoom = -1, curRoom = -1, bgCurrentRoom = -1;
    s32 portalDepthLimit = -1;
    s32 visibleRoomsCount = 0, renderedRoomsCount = 0;
    char visibleRoomsSample[128] = "[]";
    char renderedRoomsSample[128] = "[]";
    s32 renderedRoomSampleValues[16] = {};
    s32 renderedRoomSampleCount = 0;
    char renderedRoomDlSummaryFallback[3] = "[]";
    constexpr size_t RenderedRoomDlSummarySize = 524288;
    char* renderedRoomDlSummary = (char*)std::malloc(RenderedRoomDlSummarySize);
    size_t renderedRoomDlSummarySize = RenderedRoomDlSummarySize;
    if(renderedRoomDlSummary) {
      std::snprintf(renderedRoomDlSummary, renderedRoomDlSummarySize, "[]");
    } else {
      renderedRoomDlSummary = renderedRoomDlSummaryFallback;
      renderedRoomDlSummarySize = sizeof(renderedRoomDlSummaryFallback);
    }
    char actorSummaryJson[4096] = "{\"slots\":0,\"live\":0,\"alive\":0,\"hidden\":0,\"onscreen\":0,\"rendered\":0,\"sample\":[]}";
    char trackedChrJson[4096] = "";
    char combatOracleJson[16384] =
      "\"combat_oracle\":{\"guards\":[],\"guards_overflow\":0,"
      "\"floor\":{\"stan_id\":-1,\"stan_room\":-1,\"stan_flags\":-1,\"height\":0.00},"
      "\"combat\":{\"player_health\":0.0000,\"player_armor\":0.0000,"
      "\"shots_fired_total\":0,\"hits_landed_total\":0,\"rng_seed\":\"0x00000000\"},"
      "\"projectiles\":[],\"projectiles_overflow\":0},";
    char glassStateJson[4096] = "{\"present\":0,\"buffer_len\":0,\"next\":0,\"ptr\":\"0x00000000\",\"active\":0,\"first\":{\"index\":-1},\"sample\":[],\"hash\":\"0x0000000000000000\"}";
    char glassProjectionJson[65536] = "{\"present\":0,\"source\":\"stock_fixed_projection\",\"emit_enabled\":1,\"projection_valid\":0,\"projection_float\":0,\"compress_full_matrix\":0,\"basis_scale\":0,\"no_basis_scale\":1,\"active\":0,\"projected\":0,\"onscreen\":0,\"behind\":0,\"sample\":[]}";
    char glassPropsJson[16384] = "{\"present\":0,\"count\":0,\"live\":0,\"with_prop\":0,\"with_model\":0,\"destroyed\":0,\"remove\":0,\"truncated\":0,\"nearest\":{\"index\":-1},\"first_removed\":{\"index\":-1},\"first_destroyed\":{\"index\":-1},\"sample\":[],\"sample_truncated\":0,\"hash\":\"0x0000000000000000\"}";
    char impactStateJson[32768] = "{\"present\":0,\"buffer_len\":100,\"current_slot\":-1,\"ptr\":\"0x00000000\",\"occupied\":0,\"first\":{\"index\":-1},\"projection\":{\"valid\":0,\"float\":0,\"source\":\"room_matrix_field10e0\",\"projection_ptr\":\"0x00000000\",\"viewport\":[0.00,0.00,320.00,240.00]},\"sample\":[],\"hash\":\"0x0000000000000000\"}";
    s32 portalTraceIndex = -1;
    s32 portalTraceDepthByte = -1;
    s32 portalTraceCacheValid = -9999;
    s32 portalTraceOffset = -1;
    s32 portalTraceRoom1 = -1;
    s32 portalTraceRoom2 = -1;
    s32 portalTraceControl1 = -1;
    s32 portalTraceControl2 = -1;
    s32 portalTraceDestFromCur = -1;
    s32 portalTraceDestRoomCount = -1;
    s32 portalTraceRoom1Count = -1;
    s32 portalTraceRoom2Count = -1;
    u32 portalTracePortalsPtr = 0;
    f32 portalTraceBbox0 = 0.0f;
    f32 portalTraceBbox1 = 0.0f;
    f32 portalTraceBbox2 = 0.0f;
    f32 portalTraceBbox3 = 0.0f;
    u32 prop = 0;
    u32 currentTile = 0;
    u32 portalTile = 0;
    bool frozenIntroCamera = false;
    s32 cameraMode = readS32(cameraModeAddress);
    s32 cameraAfterCinema = readS32(cameraAfterCinemaAddress);
    f32 cameraTransitionTimer = readF32(cameraTransitionTimerAddress);
    s32 introCameraIndex = readS32(introCameraIndexAddress);
    u32 introSwirl = readU32(introSwirlAddress);
    s32 introAnimationIndex = readS32(introAnimationIndexAddress);
    s32 introSwirlPresent = validRdram(introSwirl, 0x20) ? 1 : 0;
    s32 introSwirlCount = 0;
    u64 introSwirlHash = 0;
    s32 introSwirlCurrentIndex = -1;
    s32 introSwirlCurrentFlags = 0;
    f32 introSwirlCurrentX = 0.0f;
    f32 introSwirlCurrentY = 0.0f;
    f32 introSwirlCurrentZ = 0.0f;
    f32 introSwirlCurrentCurve = 0.0f;
    f32 introSwirlCurrentDuration = 0.0f;
    s32 introSwirlCurrentPad = -1;
    u32 selectedIntroCamera = readU32(selectedIntroCameraAddress);
    bool selectedIntroCameraPresent = validRdram(selectedIntroCamera, 0x1c);
    f32 selectedIntroCameraX = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x04) : 0.0f;
    f32 selectedIntroCameraY = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x08) : 0.0f;
    f32 selectedIntroCameraZ = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x0c) : 0.0f;
    f32 selectedIntroCameraYaw = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x10) : 0.0f;
    f32 selectedIntroCameraPitch = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x14) : 0.0f;
    s32 selectedIntroCameraPad = selectedIntroCameraPresent ? readS32(selectedIntroCamera + 0x18) : -1;
    bool introBondPresent = false;
    s32 introBondChrnum = -1;
    s32 introBondAction = -1;
    s32 introBondSleep = -1;
    s32 introBondField20 = 0;
    s32 introBondOnscreen = 0;
    s32 introBondModelMtx = 0;
    s32 introBondAnimValid = 0;
    f32 introBondAnimFrame = 0.0f;
    f32 introBondAnimEnd = 0.0f;
    f32 introBondAnimSpeed = 0.0f;
    f32 introBondAnimAbsSpeed = 0.0f;
    s32 introBondAnimLooping = 0;
    s32 introBondAnimGunhand = -1;
    u32 introBondAnimPtr = 0;
    s32 introBondAnimFrames = -1;
    s32 introBondAnimEntryOffset = -1;
    s32 introBondAnimBitsOffset = -1;
    u64 introBondAnimHash = 0;
    s32 currentMenuValue = currentMenu();
    bool menuClosed = stockMenuInputClosed(hasPlayer);
    u64 randomSeed = randomSeedAddress != 0 && validRdram(randomSeedAddress, 8)
      ? readU64(randomSeedAddress)
      : 0;

    if(introSwirlPresent) {
      introSwirlHash = 1469598103934665603ull;
      for(s32 index = 0; index < 64; index++) {
        u32 entry = introSwirl + (u32)index * 0x20u;
        if(!validRdram(entry, 0x20)) break;
        if(readS32(entry + 0x00) != 3) break;

        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x00));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x04));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x08));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x0c));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x10));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x14));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x18));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x1c));
        introSwirlCount++;
      }

      if(introSwirlCount > 0) {
        introSwirlCurrentIndex = introCameraIndex;
        if(introSwirlCurrentIndex < 0 || introSwirlCurrentIndex >= introSwirlCount) {
          introSwirlCurrentIndex = 0;
        }

        u32 entry = introSwirl + (u32)introSwirlCurrentIndex * 0x20u;
        introSwirlCurrentFlags = readS32(entry + 0x04);
        introSwirlCurrentX = readF32(entry + 0x08);
        introSwirlCurrentY = readF32(entry + 0x0c);
        introSwirlCurrentZ = readF32(entry + 0x10);
        introSwirlCurrentCurve = readF32(entry + 0x14);
        introSwirlCurrentDuration = readF32(entry + 0x18);
        introSwirlCurrentPad = readS32(entry + 0x1c);
      }
    }

    if(hasPlayer) {
      prop = readU32(player + PlayerProp);
      if(prop != 0 && validRdram(prop, PropPos + 12)) {
        posX = readF32(prop + PropPos + 0);
        posY = readF32(prop + PropPos + 4);
        posZ = readF32(prop + PropPos + 8);
      }

      if(prop != 0 && validRdram(prop, PropChr + 4)) {
        u32 introChr = readU32(prop + PropChr);
        if(validRdram(introChr, ChrField20 + 4)) {
          u32 introModel = readU32(introChr + ChrModel);
          u32 introProp = readU32(introChr + ChrProp);

          introBondPresent = true;
          introBondChrnum = readS16(introChr + ChrChrnum);
          introBondAction = readS8(introChr + ChrActiontype);
          introBondSleep = readS8(introChr + ChrSleep);
          introBondField20 = readU32(introChr + ChrField20) != 0 ? 1 : 0;

          if(validRdram(introProp, PropFlags + 1)) {
            introBondOnscreen = (readU8(introProp + PropFlags) & 0x02) != 0 ? 1 : 0;
          }

          if(validRdram(introModel, ModelSpeed + 4)) {
            u32 introAnim = readU32(introModel + ModelAnim);
            introBondModelMtx = readU32(introModel + ModelRenderPos) != 0 ? 1 : 0;

            if(validRdram(introAnim, 0x14)) {
              f32 endFrame = readF32(introModel + ModelEndFrame);
              u16 animFrameCount = readU16(introAnim + ModelAnimationFrameCount);

              introBondAnimValid = 1;
              introBondAnimPtr = introAnim;
              introBondAnimFrames = animFrameCount;
              introBondAnimEntryOffset = (s32)logicalAnimationOffset(readU32(introAnim + 0x08));
              introBondAnimBitsOffset = (s32)logicalAnimationOffset(readU32(introAnim + 0x10));
              introBondAnimHash = 1469598103934665603ull;
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU16(introAnim + 0x04));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU8(introAnim + 0x06));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU8(introAnim + 0x07));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)introBondAnimEntryOffset);
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU16(introAnim + 0x0c));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU16(introAnim + 0x0e));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)introBondAnimBitsOffset);
              introBondAnimFrame = readF32(introModel + ModelFrame);
              introBondAnimEnd = endFrame >= 0.0f ? endFrame : (f32)animFrameCount - 1.0f;
              introBondAnimSpeed = readF32(introModel + ModelSpeed);
              introBondAnimAbsSpeed = introBondAnimSpeed < 0.0f ? -introBondAnimSpeed : introBondAnimSpeed;
              introBondAnimLooping = readS8(introModel + ModelAnimLooping);
              introBondAnimGunhand = readS8(introModel + ModelGunhand);
            }
          }
        }
      }

      frozenIntroCamera = readS32(player + PlayerViewMode) == 1;
      vvTheta = readF32(player + PlayerVvTheta);
      vvVerta = readF32(player + PlayerVvVerta);
      vvVerta360 = readF32(player + PlayerVvVerta360);
      vvCosVerta = readF32(player + PlayerVvCosVerta);
      vvSinVerta = readF32(player + PlayerVvSinVerta);
      field70 = readF32(player + PlayerField70);
      stanHeight = readF32(player + PlayerStanHeight);
      colourFadeMax = readF32(player + PlayerColourFadeTimeMax60);
      roomBasisX = readF32(player + PlayerCurrentModelPos + 0);
      roomBasisY = readF32(player + PlayerCurrentModelPos + 4);
      roomBasisZ = readF32(player + PlayerCurrentModelPos + 8);
      currentTile = readU32(player + PlayerField488 + CollisionCurrentTile);
      portalTile = readU32(player + PlayerField488 + CollisionPortalTile);
      tileRoom = stanRoom(currentTile);
      portalRoom = stanRoom(portalTile);
      if(prop != 0) propRoom = stanRoom(readU32(prop + PropStan));
      curRoom = readS32(player + PlayerCurRoomIndex);
      bgCurrentRoom = bgCurrentRoomAddress != 0 ? readS32(bgCurrentRoomAddress) : -1;
      portalDepthLimit = bgPortalDepthLimitAddress != 0 ? readS32(bgPortalDepthLimitAddress) : -1;
      visibleRoomsCount = bgVisibleRoomsCountAddress != 0 ? readS32(bgVisibleRoomsCountAddress) : 0;
      renderedRoomsCount = bgRoomsDrawnCountAddress != 0 ? readS32(bgRoomsDrawnCountAddress) : 0;
      if(visibleRoomsCount < 0 || visibleRoomsCount > 512) visibleRoomsCount = 0;
      if(renderedRoomsCount < 0 || renderedRoomsCount > 512) renderedRoomsCount = 0;
      formatVisibleRooms(visibleRoomsSample, sizeof(visibleRoomsSample), visibleRoomsCount);
      formatRenderedRooms(
        renderedRoomsSample, sizeof(renderedRoomsSample), renderedRoomsCount,
        renderedRoomSampleValues, &renderedRoomSampleCount
      );
      formatRoomDlSummaries(
        renderedRoomDlSummary, renderedRoomDlSummarySize,
        renderedRoomSampleValues, renderedRoomSampleCount
      );
      if(bgPortalTraceIndex >= 0) {
        portalTraceIndex = bgPortalTraceIndex;
        if(bgPortalDepthBytesAddress != 0 && validRdram(bgPortalDepthBytesAddress + (u32)portalTraceIndex, 1)) {
          portalTraceDepthByte = readU8(bgPortalDepthBytesAddress + (u32)portalTraceIndex);
        }
        if(bgPortalProjectionCacheAddress != 0) {
          u32 cache = bgPortalProjectionCacheAddress + (u32)portalTraceIndex * 20u;
          if(validRdram(cache, 20)) {
            portalTraceCacheValid = readS32(cache + 0);
            portalTraceBbox0 = readF32(cache + 4);
            portalTraceBbox1 = readF32(cache + 8);
            portalTraceBbox2 = readF32(cache + 12);
            portalTraceBbox3 = readF32(cache + 16);
          }
        }
        if(bgPortalsPointerAddress != 0 && validRdram(bgPortalsPointerAddress, 4)) {
          portalTracePortalsPtr = readU32(bgPortalsPointerAddress);
          u32 entry = portalTracePortalsPtr + (u32)portalTraceIndex * 8u;
          if(validRdram(entry, 8)) {
            portalTraceOffset = (s32)readU32(entry + 0);
            portalTraceRoom1 = readU8(entry + 4);
            portalTraceRoom2 = readU8(entry + 5);
            portalTraceControl1 = readU8(entry + 6);
            portalTraceControl2 = readU8(entry + 7);
            if(bgRoomInfoAddress != 0 && bgRoomInfoStride != 0) {
              u32 room1Info = bgRoomInfoAddress + (u32)portalTraceRoom1 * bgRoomInfoStride + bgRoomInfoPortalCountOffset;
              u32 room2Info = bgRoomInfoAddress + (u32)portalTraceRoom2 * bgRoomInfoStride + bgRoomInfoPortalCountOffset;
              if(validRdram(room1Info, 1)) portalTraceRoom1Count = readU8(room1Info);
              if(validRdram(room2Info, 1)) portalTraceRoom2Count = readU8(room2Info);
            }
            if(curRoom == portalTraceRoom1) {
              portalTraceDestFromCur = portalTraceRoom2;
            } else if(curRoom == portalTraceRoom2) {
              portalTraceDestFromCur = portalTraceRoom1;
            }
            if(portalTraceDestFromCur >= 0 && bgRoomInfoAddress != 0 && bgRoomInfoStride != 0) {
              u32 roomInfo = bgRoomInfoAddress + (u32)portalTraceDestFromCur * bgRoomInfoStride + bgRoomInfoPortalCountOffset;
              if(validRdram(roomInfo, 1)) {
                portalTraceDestRoomCount = readU8(roomInfo);
              }
            }
          }
        }
      }
      colX = readF32(player + PlayerCollisionPos + 0);
      colY = readF32(player + PlayerCollisionPos + 4);
      colZ = readF32(player + PlayerCollisionPos + 8);
      viewWidth = readS16(player + PlayerViewX);
      viewHeight = readS16(player + PlayerViewY);
      viewLeft = readS16(player + PlayerViewLeft);
      viewTop = readS16(player + PlayerViewTop);
      cScreenWidth = readF32(player + PlayerCScreenWidth);
      cScreenHeight = readF32(player + PlayerCScreenHeight);
      cScreenLeft = readF32(player + PlayerCScreenLeft);
      cScreenTop = readF32(player + PlayerCScreenTop);
      crosshairX = readF32(player + PlayerCrosshairAngle + 0);
      crosshairY = readF32(player + PlayerCrosshairAngle + 4);
      fieldFfcX = readF32(player + PlayerFieldFFC + 0);
      fieldFfcY = readF32(player + PlayerFieldFFC + 4);
      if(frozenIntroCamera) {
        camX = readF32(player + PlayerIntroPos + 0);
        camY = readF32(player + PlayerIntroPos + 4);
        camZ = readF32(player + PlayerIntroPos + 8);
        camTargetX = readF32(player + PlayerIntroTarget + 0);
        camTargetY = readF32(player + PlayerIntroTarget + 4);
        camTargetZ = readF32(player + PlayerIntroTarget + 8);
        camUpX = readF32(player + PlayerIntroUp + 0);
        camUpY = readF32(player + PlayerIntroUp + 4);
        camUpZ = readF32(player + PlayerIntroUp + 8);
        camFloorX = readF32(player + PlayerIntroFloor + 0);
        camFloorY = readF32(player + PlayerIntroFloor + 4);
        camFloorZ = readF32(player + PlayerIntroFloor + 8);
        renderCamX = camX;
        renderCamY = camY;
        renderCamZ = camZ;
        renderCamTargetX = camTargetX;
        renderCamTargetY = camTargetY;
        renderCamTargetZ = camTargetZ;
        f32 lookX = camTargetX - camX;
        f32 lookY = camTargetY - camY;
        f32 lookZ = camTargetZ - camZ;
        f32 lookLen = std::sqrt((lookX * lookX) + (lookY * lookY) + (lookZ * lookZ));
        if(lookLen > 0.0001f) {
          facingX = lookX / lookLen;
          facingY = lookY / lookLen;
          facingZ = lookZ / lookLen;
        }
      } else {
        camX = readF32(player + PlayerField488 + CollisionCameraPos + 0);
        camY = readF32(player + PlayerField488 + CollisionCameraPos + 4);
        camZ = readF32(player + PlayerField488 + CollisionCameraPos + 8);
        f32 viewX = readF32(player + PlayerField488 + CollisionAppliedView + 0);
        f32 viewY = readF32(player + PlayerField488 + CollisionAppliedView + 4);
        f32 viewZ = readF32(player + PlayerField488 + CollisionAppliedView + 8);
        camTargetX = camX + viewX;
        camTargetY = camY + viewY;
        camTargetZ = camZ + viewZ;
        camUpX = readF32(player + PlayerField488 + CollisionAppliedUp + 0);
        camUpY = readF32(player + PlayerField488 + CollisionAppliedUp + 4);
        camUpZ = readF32(player + PlayerField488 + CollisionAppliedUp + 8);
        camFloorX = readF32(player + PlayerField488 + CollisionFloor + 0);
        camFloorY = readF32(player + PlayerField488 + CollisionFloor + 4);
        camFloorZ = readF32(player + PlayerField488 + CollisionFloor + 8);
        renderCamX = camX;
        renderCamY = camY;
        renderCamZ = camZ;
        renderCamTargetX = camTargetX;
        renderCamTargetY = camTargetY;
        renderCamTargetZ = camTargetZ;
        facingX = readF32(player + PlayerField488 + CollisionTheta + 0);
        facingY = readF32(player + PlayerField488 + CollisionTheta + 4);
        facingZ = readF32(player + PlayerField488 + CollisionTheta + 8);
      }
      camDx = camX - colX;
      camDy = camY - colY;
      camDz = camZ - colZ;
      renderCamDx = renderCamX - colX;
      renderCamDy = renderCamY - colY;
      renderCamDz = renderCamZ - colZ;

      speedForwards = readF32(player + PlayerSpeedForwards);
      speedSideways = readF32(player + PlayerSpeedSideways);
      speedGo = readF32(player + PlayerSpeedGo);
      speedStrafe = readF32(player + PlayerSpeedStrafe);
      speedBoost = readF32(player + PlayerSpeedBoost);
      speedTheta = readF32(player + PlayerSpeedTheta);
      speedVerta = readF32(player + PlayerSpeedVerta);
      speedMaxTime60 = readS32(player + PlayerSpeedMaxTime60);
      headX = readF32(player + PlayerHeadPos + 0);
      headY = readF32(player + PlayerHeadPos + 4);
      headZ = readF32(player + PlayerHeadPos + 8);
      headLookX = readF32(player + PlayerHeadLook + 0);
      headLookY = readF32(player + PlayerHeadLook + 4);
      headLookZ = readF32(player + PlayerHeadLook + 8);
      headUpX = readF32(player + PlayerHeadUp + 0);
      headUpY = readF32(player + PlayerHeadUp + 4);
      headUpZ = readF32(player + PlayerHeadUp + 8);
      prevX = readF32(player + PlayerBondPrevPos + 0);
      prevY = readF32(player + PlayerBondPrevPos + 4);
      prevZ = readF32(player + PlayerBondPrevPos + 8);
      bondHealth = readF32(player + PlayerBondHealth);
      bondArmour = readF32(player + PlayerBondArmour);
      actualHealth = readF32(player + PlayerActualHealth);
      actualArmour = readF32(player + PlayerActualArmour);
      damageShowTime = readS32(player + PlayerDamageShowTime);
      healthShowTime = readS32(player + PlayerHealthShowTime);
      damageType = readS32(player + PlayerDamageType);
      healthDamageType = readS32(player + PlayerHealthDamageType);
      colourScreenRed = readS32(player + PlayerColourScreenRed);
      colourScreenGreen = readS32(player + PlayerColourScreenGreen);
      colourScreenBlue = readS32(player + PlayerColourScreenBlue);
      colourScreenFrac = readF32(player + PlayerColourScreenFrac);
      watchPause = readS32(player + PlayerWatchPauseTime);
      watchState = readS32(player + PlayerWatchAnimationState);
      outsideWatchMenu = readS32(player + PlayerOutsideWatchMenu);
      watchOpenReq = readS32(player + PlayerOpenCloseSoloWatchMenu);
      pausingFlag = readS32(player + PlayerPausingFlag);
      handInvisibleRight = readS32(player + PlayerHandInvisible + 0);
      handInvisibleLeft = readS32(player + PlayerHandInvisible + 4);
      handItemRight = readS32(player + PlayerHandItem + 0);
      handItemLeft = readS32(player + PlayerHandItem + 4);
      handPendingRight = readS32(player + PlayerHandPending + 0);
      handPendingLeft = readS32(player + PlayerHandPending + 4);
      handLockRight = readS32(player + PlayerHandLockModel + 0);
      handLockLeft = readS32(player + PlayerHandLockModel + 4);

      u32 rightHand = player + PlayerHands;
      u32 leftHand = player + PlayerHands + HandStride;
      handWeaponRight = readS32(rightHand + HandWeaponNum);
      handWeaponLeft = readS32(leftHand + HandWeaponNum);
      handWatchmenuRight = readS32(rightHand + HandWeaponNumWatchmenu);
      handWatchmenuLeft = readS32(leftHand + HandWeaponNumWatchmenu);
      handNextRight = readS32(rightHand + HandWeaponNextWeapon);
      handNextLeft = readS32(leftHand + HandWeaponNextWeapon);
      handMagRight = readS32(rightHand + HandWeaponAmmoInMagazine);
      handMagLeft = readS32(leftHand + HandWeaponAmmoInMagazine);
      handAnimRight = readS32(rightHand + HandWeaponCurrentAnimation);
      handAnimLeft = readS32(leftHand + HandWeaponCurrentAnimation);
      handFiringRight = readS8(rightHand + HandWeaponFiringStatus);
      handFiringLeft = readS8(leftHand + HandWeaponFiringStatus);
      handHoldRight = readS32(rightHand + HandWeaponHoldTime);
      handHoldLeft = readS32(leftHand + HandWeaponHoldTime);
      handDetStateRight = readS32(rightHand + HandWhenDetonatingMinesIs0);
      formatHandViewmodelJson(handRightVmJson, sizeof(handRightVmJson), rightHand, player);
      formatHandViewmodelJson(handLeftVmJson, sizeof(handLeftVmJson), leftHand, player);
    }

    formatActorSummaryJson(actorSummaryJson, sizeof(actorSummaryJson), hasPlayer, posX, posY, posZ, renderedRoomsCount);
    formatTrackedChrJson(trackedChrJson, sizeof(trackedChrJson), hasPlayer, posX, posY, posZ, renderedRoomsCount);
    formatCombatOracleJson(combatOracleJson, sizeof(combatOracleJson), hasPlayer, player, renderedRoomsCount);
    formatGlassStateJson(glassStateJson, sizeof(glassStateJson));
    formatGlassProjectionJson(glassProjectionJson, sizeof(glassProjectionJson), player);
    formatGlassPropsJson(glassPropsJson, sizeof(glassPropsJson), hasPlayer, posX, posY, posZ);
    formatImpactStateJson(impactStateJson, sizeof(impactStateJson), player);

    fprintf(trace,
      "{\"f\":%llu,\"p\":%d,\"source\":\"ares_mgb64_oracle\","
      "\"pos\":[%.2f,%.2f,%.2f],\"col\":[%.2f,%.2f,%.2f],"
      "\"cam_pos\":[%.2f,%.2f,%.2f],"
      "\"cam_target\":[%.2f,%.2f,%.2f],"
      "\"cam_up\":[%.2f,%.2f,%.2f],"
      "\"cam_floor\":[%.2f,%.2f,%.2f],"
      "\"cam_delta\":[%.2f,%.2f,%.2f],"
      "\"render_cam_pos\":[%.2f,%.2f,%.2f],"
      "\"render_cam_target\":[%.2f,%.2f,%.2f],"
      "\"render_cam_delta\":[%.2f,%.2f,%.2f],"
      "\"room_basis\":[%.2f,%.2f,%.2f],"
      "\"view\":[%d,%d,%d,%d],"
      "\"c_view\":[%.2f,%.2f,%.2f,%.2f],"
      "\"theta\":%.4f,\"floor\":%.2f,\"stan_h\":%.2f,"
      "\"facing\":[%.4f,%.4f,%.4f],"
      "\"view_basis\":{\"vv_verta\":%.4f,\"vv_verta360\":%.4f,"
      "\"vv_cosverta\":%.6f,\"vv_sinverta\":%.6f,"
      "\"headlook\":[%.4f,%.4f,%.4f],\"headup\":[%.4f,%.4f,%.4f]},"
      "\"cam\":%d,\"cam_after\":%d,\"icam\":%d,\"p_unk\":%d,"
      "\"move\":{\"speed\":[%.5f,%.5f],\"raw\":[%.5f,%.5f],\"boost\":%.5f,"
      "\"turn\":%.5f,\"pitch\":%.5f,\"max_t\":%d,"
      "\"head\":[%.3f,%.3f,%.3f],\"prev\":[%.2f,%.2f,%.2f],"
      "\"clock\":%d,\"dt\":%.2f,\"global\":%d},"
      "\"rng\":{\"seed\":\"0x%016llX\",\"seed_low\":%u,\"address\":\"0x%08x\"},"
      "\"rooms\":{\"tile\":%d,\"portal\":%d,\"prop\":%d,\"cur\":%d,\"render\":%d,\"portal_depth_limit\":%d,"
      "\"vis\":{\"visible\":%d,\"visible_sample\":%s,\"rendered\":%d,\"rendered_sample\":%s},"
      "\"dl\":%s,"
      "\"portal_trace\":{\"index\":%d,\"depth_byte\":%d,\"cache_valid\":%d,\"bbox\":[%.2f,%.2f,%.2f,%.2f],"
      "\"portals_ptr\":\"0x%08x\",\"offset\":\"0x%08x\",\"rooms\":[%d,%d],\"control\":[%d,%d],"
      "\"room_counts\":[%d,%d],\"dest_from_cur\":%d,\"dest_room_count\":%d}},"
      "\"actors\":%s,%s%s"
      "\"glass\":%s,"
      "\"glass_projection\":%s,"
      "\"glass_props\":%s,"
      "\"impact_state\":%s,"
      "\"combat\":{\"crosshair\":[%.2f,%.2f],\"screen\":[%.2f,%.2f],"
      "\"health\":{\"bond\":%.4f,\"armor\":%.4f,\"actual_h\":%.4f,\"actual_a\":%.4f,"
      "\"damage_show\":%d,\"health_show\":%d,\"damage_type\":%d,\"health_type\":%d,"
      "\"fade_rgba\":[%d,%d,%d,%.4f]}},"
      "\"watch\":{\"state\":%d,\"pause\":%d,\"outside\":%d,\"open_req\":%d,\"pausing\":%d,"
      "\"hands\":{\"right\":{\"item\":%d,\"pending\":%d,\"invis\":%d,\"lock\":%d,"
      "\"weaponnum\":%d,\"watchmenu\":%d,\"next\":%d,\"mag\":%d,\"anim\":%d,"
      "\"fire\":%d,\"hold\":%d,\"det\":%d,\"vm\":%s},"
      "\"left\":{\"item\":%d,\"pending\":%d,\"invis\":%d,\"lock\":%d,"
      "\"weaponnum\":%d,\"watchmenu\":%d,\"next\":%d,\"mag\":%d,\"anim\":%d,"
      "\"fire\":%d,\"hold\":%d,\"vm\":%s}}},"
      "\"front\":{\"active_stage\":%d,\"menu\":%d},"
      "\"intro\":{\"frozen\":%d,\"timer\":%.2f,\"colour_fade_max\":%.2f,"
      "\"setup\":{\"anim_index\":%d,\"swirl\":{\"present\":%d,\"count\":%d,\"hash\":\"0x%016llX\","
      "\"current\":{\"index\":%d,\"flags\":%d,\"pos\":[%.4f,%.4f,%.4f],"
      "\"curve\":%.4f,\"duration\":%.4f,\"pad\":%d}}},"
      "\"swirl\":\"0x%08x\",\"selected_camera_ptr\":\"0x%08x\","
      "\"selected_camera\":{\"present\":%d,"
      "\"pos\":[%.2f,%.2f,%.2f],\"yaw\":%.6f,\"pitch\":%.6f,\"pad\":%d},"
      "\"bond_present\":%d,\"bond_chrnum\":%d,\"bond_action\":%d,\"bond_sleep\":%d,"
      "\"bond_field20\":%d,\"bond_model_mtx\":%d,\"bond_onscreen\":%d,"
      "\"bond_anim\":{\"valid\":%d,\"ptr\":\"0x%08x\",\"frames\":%d,\"hash\":\"0x%016llX\","
      "\"entry_offset\":%d,\"bits_offset\":%d,\"frame\":%.2f,\"end\":%.2f,\"speed\":%.4f,"
      "\"abs_speed\":%.4f,\"looping\":%d,\"gunhand\":%d},"
      "\"tile\":\"0x%08x\",\"portal_tile\":\"0x%08x\"},"
      "\"oracle\":{\"provider\":\"ares\",\"layout\":\"%s\",\"stage\":%d,\"target_stage\":%d,\"player_source\":\"%s\","
      "\"player\":\"0x%08x\",\"prop\":\"0x%08x\","
      "\"bg_current_room\":%d,"
      "\"players\":[\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\"],"
      "\"input_frame\":%llu,\"gameplay_frame\":%llu,"
      "\"gameplay_origin_global\":%d,\"gameplay_origin_input\":%llu,"
      "\"gate\":{\"menu_closed\":%d,\"gameplay_start_global\":%d,\"close_menu_on_player\":%d},"
      "\"force\":{\"events\":%u,\"applies\":%u,\"last_event\":%d,\"last_gameplay_frame\":%llu,"
      "\"last_pad\":%d,\"last_stan\":\"0x%08x\",\"last_stan_room\":%d,"
      "\"last_pad_table\":\"0x%08x\",\"last_pad_address\":\"0x%08x\","
      "\"last_floor_y\":%.2f},"
      "\"crosshair_force\":{\"events\":%u,\"applies\":%u,\"last_event\":%d,"
      "\"last_gameplay_frame\":%llu,\"last_xy\":[%.2f,%.2f]},"
      "\"rng_seed\":{\"events\":%u,\"applies\":%u,\"last_event\":%d,"
      "\"last_gameplay_frame\":%llu,\"last_seed\":\"0x%016llX\"},"
      "\"input\":{\"frame\":%llu,\"gameplay_frame\":%llu,\"buttons\":\"0x%04x\","
      "\"stick\":[%d,%d],\"menu_events\":%u,\"gameplay_events\":%u,"
      "\"suppressed_menu_events\":%u,\"suppressed_gameplay_events\":%u,"
      "\"menu_closed\":%d}}}\n",
      (unsigned long long)videoFrame, hasPlayer ? 1 : 0,
      posX, posY, posZ, colX, colY, colZ,
      camX, camY, camZ,
      camTargetX, camTargetY, camTargetZ,
      camUpX, camUpY, camUpZ,
      camFloorX, camFloorY, camFloorZ,
      camDx, camDy, camDz,
      renderCamX, renderCamY, renderCamZ,
      renderCamTargetX, renderCamTargetY, renderCamTargetZ,
      renderCamDx, renderCamDy, renderCamDz,
      roomBasisX, roomBasisY, roomBasisZ,
      viewLeft, viewTop, viewWidth, viewHeight,
      cScreenLeft, cScreenTop, cScreenWidth, cScreenHeight,
      vvTheta, field70, stanHeight,
      facingX, facingY, facingZ,
      vvVerta, vvVerta360, vvCosVerta, vvSinVerta,
      headLookX, headLookY, headLookZ,
      headUpX, headUpY, headUpZ,
      cameraMode, cameraAfterCinema, introCameraIndex, readS32(player + PlayerViewMode),
      speedForwards, speedSideways, speedGo, speedStrafe, speedBoost,
      speedTheta, speedVerta, speedMaxTime60,
      headX, headY, headZ, prevX, prevY, prevZ,
      readS32(clockTimerAddress), readF32(globalTimerDeltaAddress), readS32(globalTimerAddress),
      (unsigned long long)randomSeed,
      (u32)(randomSeed & 0xffffffffull),
      randomSeedAddress,
      tileRoom, portalRoom, propRoom, curRoom, bgCurrentRoom, portalDepthLimit,
      visibleRoomsCount, visibleRoomsSample, renderedRoomsCount, renderedRoomsSample,
      renderedRoomDlSummary,
      portalTraceIndex, portalTraceDepthByte, portalTraceCacheValid,
      portalTraceBbox0, portalTraceBbox1, portalTraceBbox2, portalTraceBbox3,
      portalTracePortalsPtr, (u32)portalTraceOffset,
      portalTraceRoom1, portalTraceRoom2, portalTraceControl1, portalTraceControl2,
      portalTraceRoom1Count, portalTraceRoom2Count,
      portalTraceDestFromCur, portalTraceDestRoomCount,
      actorSummaryJson,
      trackedChrJson,
      combatOracleJson,
      glassStateJson,
      glassProjectionJson,
      glassPropsJson,
      impactStateJson,
      crosshairX, crosshairY, fieldFfcX, fieldFfcY,
      bondHealth, bondArmour, actualHealth, actualArmour,
      damageShowTime, healthShowTime, damageType, healthDamageType,
      colourScreenRed, colourScreenGreen, colourScreenBlue, colourScreenFrac,
      watchState, watchPause, outsideWatchMenu, watchOpenReq, pausingFlag,
      handItemRight, handPendingRight, handInvisibleRight, handLockRight,
      handWeaponRight, handWatchmenuRight, handNextRight, handMagRight,
      handAnimRight, handFiringRight, handHoldRight, handDetStateRight,
      handRightVmJson,
      handItemLeft, handPendingLeft, handInvisibleLeft, handLockLeft,
      handWeaponLeft, handWatchmenuLeft, handNextLeft, handMagLeft,
      handAnimLeft, handFiringLeft, handHoldLeft,
      handLeftVmJson,
      currentStage, currentMenuValue,
      frozenIntroCamera ? 1 : 0, cameraTransitionTimer, colourFadeMax,
      introAnimationIndex,
      introSwirlPresent, introSwirlCount, (unsigned long long)introSwirlHash,
      introSwirlCurrentIndex, introSwirlCurrentFlags,
      introSwirlCurrentX, introSwirlCurrentY, introSwirlCurrentZ,
      introSwirlCurrentCurve, introSwirlCurrentDuration, introSwirlCurrentPad,
      introSwirl, selectedIntroCamera,
      selectedIntroCameraPresent ? 1 : 0,
      selectedIntroCameraX, selectedIntroCameraY, selectedIntroCameraZ,
      selectedIntroCameraYaw, selectedIntroCameraPitch, selectedIntroCameraPad,
      introBondPresent ? 1 : 0, introBondChrnum, introBondAction, introBondSleep,
      introBondField20, introBondModelMtx, introBondOnscreen,
      introBondAnimValid, introBondAnimPtr, introBondAnimFrames,
      (unsigned long long)introBondAnimHash,
      introBondAnimEntryOffset, introBondAnimBitsOffset,
      introBondAnimFrame, introBondAnimEnd,
      introBondAnimSpeed, introBondAnimAbsSpeed, introBondAnimLooping,
      introBondAnimGunhand,
      currentTile, portalTile,
      symbolLayout, currentStage, targetStage, playerSource ? playerSource : "none",
      player, prop, bgCurrentRoom,
      players[0], players[1], players[2], players[3],
      (unsigned long long)inputFrame, (unsigned long long)gameplayFrame,
      gameplayTimelineStarted ? gameplayOriginGlobal : -1,
      (unsigned long long)(gameplayTimelineStarted ? gameplayOriginInputFrame : 0),
      menuClosed ? 1 : 0, gameplayStartGlobal, closeMenuOnPlayer ? 1 : 0,
      forcePlayerEventCount, forcePlayerApplyCount, lastForcePlayerEvent,
      (unsigned long long)lastForcePlayerGameplayFrame,
      lastForcePlayerPad, lastForcePlayerStan, lastForcePlayerStanRoom,
      lastForcePlayerPadTable, lastForcePlayerPadAddress, lastForcePlayerFloorY,
      crosshairEventCount, crosshairApplyCount, lastCrosshairEvent,
      (unsigned long long)lastCrosshairGameplayFrame,
      lastCrosshairX, lastCrosshairY,
      rngSeedEventCount, rngSeedApplyCount, lastRngSeedEvent,
      (unsigned long long)lastRngSeedGameplayFrame,
      (unsigned long long)lastRngSeedValue,
      (unsigned long long)lastInputFrame,
      (unsigned long long)lastInputGameplayFrame,
      lastInputButtons,
      lastInputStickX, lastInputStickY,
      lastInputMenuEvents, lastInputGameplayEvents,
      lastInputSuppressedMenuEvents, lastInputSuppressedGameplayEvents,
      lastInputMenuClosed ? 1 : 0);

    if(renderedRoomDlSummary != renderedRoomDlSummaryFallback) {
      std::free(renderedRoomDlSummary);
    }

    if(frameLimit && videoFrame >= frameLimit) {
      fclose(trace);
      trace = nullptr;
      complete = true;
      if(rngPcTrace) {
        mgb64OracleRngPcHookAddress = 0;
        fclose(rngPcTrace);
        rngPcTrace = nullptr;
        fprintf(stderr,
          "mgb64 oracle: RNG PC trace captured %llu call(s)\n",
          (unsigned long long)rngPcCallIndex);
      }
      fprintf(stderr, "mgb64 oracle: captured %llu frame(s)\n", (unsigned long long)videoFrame);
    }
  }

  ~OracleState() {
    if(trace) fclose(trace);
    if(rngPcTrace) {
      mgb64OracleRngPcHookAddress = 0;
      fclose(rngPcTrace);
    }
  }
};

auto oracleState() -> OracleState& {
  static OracleState state;
  return state;
}

}

/* FID-0063: guest PC the CPU dispatcher compares against on every block/
 * instruction dispatch (ares/n64/cpu/cpu.cpp). 0 = disabled (default);
 * configure() arms it only when MGB64_ARES_RNG_PC_TRACE is set. */
u32 mgb64OracleRngPcHookAddress = 0;

auto mgb64OracleRngPcCall(u64 ra) -> void {
  oracleState().rngPcCall(ra);
}

auto mgb64OracleControllerRead(n32 data) -> n32 {
  return oracleState().scriptedController(data);
}

auto mgb64OracleFrameHook() -> void {
  oracleState().traceFrame();
}

auto mgb64OracleVideoFrame() -> u64 {
  return oracleState().videoFrame;
}

auto mgb64OracleVideoDump(Node::Video::Screen screen) -> void {
  oracleState().dumpScreen(screen);
}

auto mgb64OraclePresentedVideoDump(Node::Video::Screen screen, const u32* pixels, u32 pitch, u32 width, u32 height) -> void {
  oracleState().dumpPresentedScreen(screen, pixels, pitch, width, height);
}

}

namespace ares::Core::Video {

auto mgb64OraclePresentedVideoDump(std::shared_ptr<Screen> screen, const u32* pixels, u32 pitch, u32 width, u32 height) -> void {
  ares::Nintendo64::mgb64OraclePresentedVideoDump(screen, pixels, pitch, width, height);
}

}
'''

n64_hpp = src / "ares/n64/n64.hpp"
text = n64_hpp.read_text(encoding="utf-8")
if "#include <cstdlib>" not in text:
    text = text.replace("#include <float.h>\n", "#include <float.h>\n#include <cstdio>\n#include <cstdlib>\n", 1)
if "mgb64OracleFrameHook" not in text:
    text = text.replace(
        "  auto option(string name, string value) -> bool;\n",
        "  auto option(string name, string value) -> bool;\n"
        "  auto mgb64OracleControllerRead(n32 data) -> n32;\n"
        "  auto mgb64OracleFrameHook() -> void;\n",
        1,
    )
if "mgb64OracleVideoDump" not in text:
    text = text.replace(
        "  auto mgb64OracleFrameHook() -> void;\n",
        "  auto mgb64OracleFrameHook() -> void;\n"
        "  auto mgb64OracleVideoFrame() -> u64;\n"
        "  auto mgb64OracleVideoDump(Node::Video::Screen screen) -> void;\n",
        1,
    )
    n64_hpp.write_text(text, encoding="utf-8")
elif "mgb64OracleVideoFrame" not in text:
    text = text.replace(
        "  auto mgb64OracleFrameHook() -> void;\n",
        "  auto mgb64OracleFrameHook() -> void;\n"
        "  auto mgb64OracleVideoFrame() -> u64;\n",
        1,
    )
    n64_hpp.write_text(text, encoding="utf-8")
elif text != n64_hpp.read_text(encoding="utf-8"):
    n64_hpp.write_text(text, encoding="utf-8")

# FID-0063: randomGetNext caller-PC hook declarations (used by cpu.cpp).
text = n64_hpp.read_text(encoding="utf-8")
if "mgb64OracleRngPcCall" not in text:
    text = text.replace(
        "  auto mgb64OracleFrameHook() -> void;\n",
        "  auto mgb64OracleFrameHook() -> void;\n"
        "  extern u32 mgb64OracleRngPcHookAddress;\n"
        "  auto mgb64OracleRngPcCall(u64 ra) -> void;\n",
        1,
    )
    n64_hpp.write_text(text, encoding="utf-8")

gamepad_cpp = src / "ares/n64/controller/gamepad/gamepad.cpp"
text = gamepad_cpp.read_text(encoding="utf-8")
if "mgb64OracleControllerRead" not in text:
    text = text.replace("\n  return data;\n}\n\nauto Gamepad::getInodeChecksum", "\n  return mgb64OracleControllerRead(data);\n}\n\nauto Gamepad::getInodeChecksum", 1)
    gamepad_cpp.write_text(text, encoding="utf-8")

# FID-0063: randomGetNext caller-PC hook in the CPU dispatcher. Every entry
# into the retail randomGetNext lands here with ipu.pc == the function entry:
# jal/jalr are hard block terminals in the recompiler ("Unconditional jumps
# (J/JR/JAL/JALR)" in recompiler.cpp's compilation-window model, cross-block
# chaining disabled), so a called function's first instruction is always
# reached through this dispatch; the interpreter path dispatches every
# instruction. The compare sits AFTER the interrupt/NMI/devirtualize
# early-outs so an interrupted or faulting dispatch at the hook PC is not
# double-counted when it re-enters. Disabled (address 0) unless
# MGB64_ARES_RNG_PC_TRACE is set; the hook only reads guest state.
cpu_cpp = src / "ares/n64/cpu/cpu.cpp"
text = cpu_cpp.read_text(encoding="utf-8")
if "mgb64OracleRngPcCall" not in text:
    anchor = "  auto access = devirtualize<Read, Word>(ipu.pc);\n  if(!access) return true;\n"
    if anchor not in text:
        raise SystemExit("FAIL: cpu.cpp dispatch anchor not found for the RNG PC hook")
    text = text.replace(
        anchor,
        anchor
        + "\n"
        + "  if(mgb64OracleRngPcHookAddress && (u32)ipu.pc == mgb64OracleRngPcHookAddress) {\n"
        + "    mgb64OracleRngPcCall(ipu.r[31].u64);\n"
        + "  }\n",
        1,
    )
    cpu_cpp.write_text(text, encoding="utf-8")

vi_cpp = src / "ares/n64/vi/vi.cpp"
text = vi_cpp.read_text(encoding="utf-8")
if "mgb64OracleFrameHook" not in text:
    text = text.replace(
        "        refreshed = true;\n        screen->frame();\n",
        "        refreshed = true;\n        mgb64OracleFrameHook();\n        screen->frame();\n",
        1,
    )
    vi_cpp.write_text(text, encoding="utf-8")
text = vi_cpp.read_text(encoding="utf-8")
if "mgb64OracleVideoDump(screen);" not in text:
    text = text.replace(
        "    if(Model::Aleck64()) aleck64.vdp.render(screen); //aleck64 supports overlay graphics\n"
        "    return;\n",
        "    if(Model::Aleck64()) aleck64.vdp.render(screen); //aleck64 supports overlay graphics\n"
        "    mgb64OracleVideoDump(screen);\n"
        "    return;\n",
        1,
    )
    marker = "\n}\n\nauto VI::power"
    software_start = text.find("  if(vi.io.colorDepth == 3) {\n    //24bpp\n")
    software_end = text.find(marker, software_start)
    if software_start >= 0 and software_end >= 0:
        text = text[:software_end] + "\n\n  mgb64OracleVideoDump(screen);" + text[software_end:]
    vi_cpp.write_text(text, encoding="utf-8")
text = vi_cpp.read_text(encoding="utf-8")
oracle_struct = text.find("struct OracleInputEvent")
oracle_block_start = -1
if oracle_struct >= 0:
    oracle_block_start = text.rfind("#include <n64/n64.hpp>", 0, oracle_struct)
if oracle_block_start >= 0:
    vi_cpp.write_text(text[:oracle_block_start].rstrip() + "\n\n" + oracle_cpp.rstrip() + "\n", encoding="utf-8")
elif "struct OracleInputEvent" not in text:
    vi_cpp.write_text(text.rstrip() + "\n\n" + oracle_cpp.rstrip() + "\n", encoding="utf-8")

rdp_render_cpp = src / "ares/n64/rdp/render.cpp"
text = rdp_render_cpp.read_text(encoding="utf-8")
rdp_trace_block = r'''
struct Mgb64RdpDrawSummary {
  u32 textureSerial = 0;
  u32 image = 0;
  u32 imageKseg0 = 0;
  u32 fmt = 0;
  u32 siz = 0;
  u32 tile = 0;
  u32 width = 0;
  u32 height = 0;
  u32 combineHi = 0;
  u32 combineLo = 0;
  u32 otherHi = 0;
  u32 otherLo = 0;
  u32 env = 0;
  u32 colorImage = 0;
  u64 opMask = 0;
  u32 draws = 0;
  u32 firstAddress = 0;
  u32 lastAddress = 0;
};

struct Mgb64RdpDrawOp {
  u32 address = 0;
  u32 opCode = 0;
  u32 textureSerial = 0;
  u32 image = 0;
  u32 imageKseg0 = 0;
  u32 fmt = 0;
  u32 siz = 0;
  u32 tile = 0;
  u32 width = 0;
  u32 height = 0;
  u32 combineHi = 0;
  u32 combineLo = 0;
  u32 otherHi = 0;
  u32 otherLo = 0;
  u32 env = 0;
  u32 colorImage = 0;
  s32 bboxX0 = 0;
  s32 bboxY0 = 0;
  s32 bboxX1 = 0;
  s32 bboxY1 = 0;
  s32 rawXh = 0;
  s32 rawXm = 0;
  s32 rawXl = 0;
  s32 rawYh = 0;
  s32 rawYm = 0;
  s32 rawYl = 0;
  s32 rawDxhdy = 0;
  s32 rawDxmdy = 0;
  s32 rawDxldy = 0;
  u32 flags = 0;
  u32 valid = 0;
};

struct Mgb64RdpTraceConfig {
  bool configured = false;
  bool enabled = false;
  u32 afterFrame = 0;
  u32 beforeFrame = 0xffffffffu;
};

static auto mgb64RdpTraceU32Env(const char* name, u32 fallback) -> u32 {
  const char* text = std::getenv(name);
  if(text == nullptr || text[0] == '\0') return fallback;
  char* end = nullptr;
  unsigned long value = std::strtoul(text, &end, 0);
  if(end == text) return fallback;
  return (u32)value;
}

static auto mgb64RdpTraceBoolEnv(const char* name, bool fallback = false) -> bool {
  const char* text = std::getenv(name);
  if(text == nullptr || text[0] == '\0') return fallback;
  return !(text[0] == '0' || text[0] == 'n' || text[0] == 'N' ||
           text[0] == 'f' || text[0] == 'F');
}

static auto mgb64RdpTracePath() -> const char* {
  const char* path = std::getenv("MGB64_ARES_RDP_COMMAND_TRACE_PATH");
  if(path == nullptr || path[0] == '\0') path = std::getenv("MGB64_ARES_TRACE_RDP_COMMANDS_PATH");
  return path != nullptr && path[0] != '\0' ? path : nullptr;
}

static auto mgb64RdpTraceConfig() -> Mgb64RdpTraceConfig& {
  static Mgb64RdpTraceConfig config;
  if(!config.configured) {
    config.enabled = mgb64RdpTraceBoolEnv("MGB64_ARES_TRACE_RDP_COMMANDS", false);
    config.afterFrame = mgb64RdpTraceU32Env("MGB64_ARES_TRACE_RDP_COMMANDS_AFTER_FRAME", 0);
    config.beforeFrame = mgb64RdpTraceU32Env("MGB64_ARES_TRACE_RDP_COMMANDS_BEFORE_FRAME", 0xffffffffu);
    config.configured = true;
  }
  return config;
}

static auto mgb64RdpTraceEnabled() -> bool {
  auto& config = mgb64RdpTraceConfig();
  return config.enabled && mgb64RdpTracePath() != nullptr;
}

static auto mgb64RdpTraceWriteActive(u64 frame) -> bool {
  auto& config = mgb64RdpTraceConfig();
  return config.enabled && frame >= config.afterFrame && frame <= config.beforeFrame && mgb64RdpTracePath() != nullptr;
}

static auto mgb64RdpCommandQwords(u32 opCode) -> u32 {
  switch(opCode) {
  case 0x08: return 4;
  case 0x09: return 6;
  case 0x0a: return 12;
  case 0x0b: return 14;
  case 0x0c: return 12;
  case 0x0d: return 14;
  case 0x0e: return 20;
  case 0x0f: return 22;
  case 0x24:
  case 0x25:
    return 2;
  default:
    return 1;
  }
}

static auto mgb64RdpIsDrawOp(u32 opCode) -> bool {
  return (opCode >= 0x08 && opCode <= 0x0f) || opCode == 0x24 || opCode == 0x25 || opCode == 0x36;
}

static auto mgb64RdpTraceDrawOps() -> bool {
  static bool configured = false;
  static bool enabled = false;
  if(!configured) {
    enabled = mgb64RdpTraceBoolEnv("MGB64_ARES_TRACE_RDP_DRAW_OPS", false);
    configured = true;
  }
  return enabled;
}

static auto mgb64RdpSignExtend(u32 value, u32 bits) -> s32 {
  u32 mask = 1u << (bits - 1u);
  return (s32)((value ^ mask) - mask);
}

static auto mgb64RdpReadCommandWord(Memory::Writable& memory, u32 address, u32 index) -> u32 {
  return memory.readUnaligned<Word>(address + index * 4u);
}

static auto mgb64RdpInterpolateX(s32 xh,
                                 s32 xm,
                                 s32 xl,
                                 s32 dxhdy,
                                 s32 dxmdy,
                                 s32 dxldy,
                                 s32 yh,
                                 s32 ym,
                                 s32 y,
                                 bool flip,
                                 s32* left,
                                 s32* right) -> void {
  s64 yhBase = (s64)(yh & ~3);
  s64 ymBase = (s64)ym;
  s64 rightEdge = (s64)xh + (s64)(y - yhBase) * (s64)dxhdy;
  s64 midEdge = (s64)xm + (s64)(y - yhBase) * (s64)dxmdy;
  s64 leftEdge = (s64)xl + (s64)(y - ymBase) * (s64)dxldy;
  if(y < ym) leftEdge = midEdge;
  s32 rightShifted = (s32)(rightEdge >> 15);
  s32 leftShifted = (s32)(leftEdge >> 15);
  if(flip) {
    *left = rightShifted;
    *right = leftShifted;
  } else {
    *left = leftShifted;
    *right = rightShifted;
  }
}

static auto mgb64RdpAddDrawOp(Memory::Writable& memory,
                              Mgb64RdpDrawOp* drawOps,
                              u32& drawOpCount,
                              u32& drawOpTruncated,
                              u32 address,
                              u32 opCode,
                              u32 textureSerial,
                              u32 image,
                              u32 fmt,
                              u32 siz,
                              u32 tile,
                              u32 width,
                              u32 height,
                              u32 combineHi,
                              u32 combineLo,
                              u32 otherHi,
                              u32 otherLo,
                              u32 env,
                              u32 colorImage,
                              u32 scissorXlo,
                              u32 scissorYlo,
                              u32 scissorXhi,
                              u32 scissorYhi) -> void {
  if(drawOpCount >= 256) {
    drawOpTruncated = 1;
    return;
  }

  auto& op = drawOps[drawOpCount++];
  op.address = address;
  op.opCode = opCode;
  op.textureSerial = textureSerial;
  op.image = image;
  op.imageKseg0 = image | 0x80000000u;
  op.fmt = fmt;
  op.siz = siz;
  op.tile = tile;
  op.width = width;
  op.height = height;
  op.combineHi = combineHi;
  op.combineLo = combineLo;
  op.otherHi = otherHi;
  op.otherLo = otherLo;
  op.env = env;
  op.colorImage = colorImage;

  if(opCode >= 0x08 && opCode <= 0x0f) {
    u32 words[8] = {};
    for(u32 index = 0; index < 8; index++) words[index] = mgb64RdpReadCommandWord(memory, address, index);
    bool flip = (words[0] & 0x00800000u) != 0;
    op.flags = flip ? 1u : 0u;
    op.rawYl = mgb64RdpSignExtend(words[0] & 0x3fffu, 14);
    op.rawYm = mgb64RdpSignExtend((words[1] >> 16) & 0x3fffu, 14);
    op.rawYh = mgb64RdpSignExtend(words[1] & 0x3fffu, 14);
    op.rawXl = mgb64RdpSignExtend(words[2] & 0x0fffffffu, 28) >> 1;
    op.rawXh = mgb64RdpSignExtend(words[4] & 0x0fffffffu, 28) >> 1;
    op.rawXm = mgb64RdpSignExtend(words[6] & 0x0fffffffu, 28) >> 1;
    op.rawDxldy = mgb64RdpSignExtend((words[3] >> 2) & 0x0fffffffu, 28) >> 1;
    op.rawDxhdy = mgb64RdpSignExtend((words[5] >> 2) & 0x0fffffffu, 28) >> 1;
    op.rawDxmdy = mgb64RdpSignExtend((words[7] >> 2) & 0x0fffffffu, 28) >> 1;

    if(op.rawYl > op.rawYh) {
      s32 startY = op.rawYh & ~3;
      s32 endY = (op.rawYl - 1) | 3;
      startY = startY < (s32)scissorYlo ? (s32)scissorYlo : startY;
      endY = endY > (s32)scissorYhi - 1 ? (s32)scissorYhi - 1 : endY;
      if(endY >= startY) {
        s32 upperLeft = 0, upperRight = 0;
        s32 lowerLeft = 0, lowerRight = 0;
        s32 midLeft = 0, midRight = 0;
        s32 mid1Left = 0, mid1Right = 0;
        mgb64RdpInterpolateX(op.rawXh, op.rawXm, op.rawXl, op.rawDxhdy, op.rawDxmdy, op.rawDxldy,
                             op.rawYh, op.rawYm, startY, flip, &upperLeft, &upperRight);
        mgb64RdpInterpolateX(op.rawXh, op.rawXm, op.rawXl, op.rawDxhdy, op.rawDxmdy, op.rawDxldy,
                             op.rawYh, op.rawYm, endY, flip, &lowerLeft, &lowerRight);
        midLeft = upperLeft;
        midRight = upperRight;
        mid1Left = upperLeft;
        mid1Right = upperRight;
        if(op.rawYm > startY && op.rawYm < endY) {
          mgb64RdpInterpolateX(op.rawXh, op.rawXm, op.rawXl, op.rawDxhdy, op.rawDxmdy, op.rawDxldy,
                               op.rawYh, op.rawYm, op.rawYm, flip, &midLeft, &midRight);
          mgb64RdpInterpolateX(op.rawXh, op.rawXm, op.rawXl, op.rawDxhdy, op.rawDxmdy, op.rawDxldy,
                               op.rawYh, op.rawYm, op.rawYm - 1, flip, &mid1Left, &mid1Right);
        }
        s32 startX = upperLeft;
        if(lowerLeft < startX) startX = lowerLeft;
        if(midLeft < startX) startX = midLeft;
        if(mid1Left < startX) startX = mid1Left;
        s32 endX = upperRight;
        if(lowerRight > endX) endX = lowerRight;
        if(midRight > endX) endX = midRight;
        if(mid1Right > endX) endX = mid1Right;
        s32 clipX0 = (s32)(scissorXlo >> 2);
        s32 clipX1 = (s32)((scissorXhi + 3u) >> 2) - 1;
        if(startX < clipX0) startX = clipX0;
        if(endX > clipX1) endX = clipX1;
        if(endX >= startX) {
          op.bboxX0 = startX;
          op.bboxY0 = startY / 4;
          op.bboxX1 = endX + 1;
          op.bboxY1 = endY / 4 + 1;
          op.valid = 1;
        }
      }
    }
  } else if(opCode == 0x24 || opCode == 0x25 || opCode == 0x36) {
    u32 w0 = mgb64RdpReadCommandWord(memory, address, 0);
    u32 w1 = mgb64RdpReadCommandWord(memory, address, 1);
    s32 x0 = (s32)((w1 >> 12) & 0xfffu) / 4;
    s32 y0 = (s32)(w1 & 0xfffu) / 4;
    s32 x1 = (s32)((w0 >> 12) & 0xfffu) / 4 + 1;
    s32 y1 = (s32)(w0 & 0xfffu) / 4 + 1;
    if(x1 < x0) { s32 tmp = x0; x0 = x1; x1 = tmp; }
    if(y1 < y0) { s32 tmp = y0; y0 = y1; y1 = tmp; }
    op.bboxX0 = x0;
    op.bboxY0 = y0;
    op.bboxX1 = x1;
    op.bboxY1 = y1;
    op.valid = 1;
  }
}

static auto mgb64RdpAddDrawSummary(Mgb64RdpDrawSummary* summaries,
                                   u32& summaryCount,
                                   u32& summaryTruncated,
                                   u32 address,
                                   u32 opCode,
                                   u32 textureSerial,
                                   u32 image,
                                   u32 fmt,
                                   u32 siz,
                                   u32 tile,
                                   u32 width,
                                   u32 height,
                                   u32 combineHi,
                                   u32 combineLo,
                                   u32 otherHi,
                                   u32 otherLo,
                                   u32 env,
                                   u32 colorImage) -> void {
  u32 imageKseg0 = image | 0x80000000u;
  for(u32 index = 0; index < summaryCount; index++) {
    auto& summary = summaries[index];
    if(summary.textureSerial == textureSerial &&
       summary.image == image &&
       summary.fmt == fmt &&
       summary.siz == siz &&
       summary.tile == tile &&
       summary.width == width &&
       summary.height == height &&
       summary.combineHi == combineHi &&
       summary.combineLo == combineLo &&
       summary.otherHi == otherHi &&
       summary.otherLo == otherLo &&
       summary.env == env &&
       summary.colorImage == colorImage) {
      summary.draws++;
      summary.lastAddress = address;
      if(opCode < 64) summary.opMask |= 1ull << opCode;
      return;
    }
  }

  if(summaryCount >= 128) {
    summaryTruncated = 1;
    return;
  }

  auto& summary = summaries[summaryCount++];
  summary.textureSerial = textureSerial;
  summary.image = image;
  summary.imageKseg0 = imageKseg0;
  summary.fmt = fmt;
  summary.siz = siz;
  summary.tile = tile;
  summary.width = width;
  summary.height = height;
  summary.combineHi = combineHi;
  summary.combineLo = combineLo;
  summary.otherHi = otherHi;
  summary.otherLo = otherLo;
  summary.env = env;
  summary.colorImage = colorImage;
  summary.draws = 1;
  summary.firstAddress = address;
  summary.lastAddress = address;
  if(opCode < 64) summary.opMask = 1ull << opCode;
}

static auto mgb64RdpTraceCommandStream(Memory::Writable& memory,
                                       u32 source,
                                       u32 start,
                                       u32 end) -> void {
  u64 frame = mgb64OracleVideoFrame();
  if(!mgb64RdpTraceEnabled()) return;
  if(end <= start) return;
  bool writeActive = mgb64RdpTraceWriteActive(frame);

  u32 opCounts[64] = {};
  Mgb64RdpDrawSummary summaries[128] = {};
  Mgb64RdpDrawOp drawOps[256] = {};
  u32 summaryCount = 0;
  u32 summaryTruncated = 0;
  u32 drawOpCount = 0;
  u32 drawOpTruncated = 0;
  u32 commands = 0;
  u32 truncated = 0;
  u32 truncatedOp = 0;
  u32 truncatedAddress = 0;
  u32 truncatedRemaining = 0;
  u32 truncatedNeeded = 0;
  u32 maxCommands = mgb64RdpTraceU32Env("MGB64_ARES_TRACE_RDP_COMMANDS_MAX", 8192);
  if(maxCommands == 0) maxCommands = 8192;

  static u32 textureSerial = 0;
  static u32 currentImage = 0;
  static u32 currentFmt = 0;
  static u32 currentSiz = 0;
  static u32 currentTile = 0;
  static u32 currentTexWidth = 0;
  static u32 currentTexHeight = 0;
  static u32 combineHi = 0;
  static u32 combineLo = 0;
  static u32 otherHi = 0;
  static u32 otherLo = 0;
  static u32 env = 0;
  static u32 colorImage = 0;
  static u32 scissorXlo = 0;
  static u32 scissorYlo = 0;
  static u32 scissorXhi = 1280;
  static u32 scissorYhi = 960;
  u32 cursor = start;
  bool traceDrawOps = writeActive && mgb64RdpTraceDrawOps();

  while(cursor < end && commands < maxCommands) {
    u32 address = cursor;
    u64 op = memory.readUnaligned<Dual>(cursor);
    u32 opCode = (u32)((op >> 56) & 0x3fu);
    u32 w0 = (u32)(op >> 32);
    u32 w1 = (u32)op;
    opCounts[opCode]++;
    commands++;

    u32 qwords = mgb64RdpCommandQwords(opCode);
    if(cursor + qwords * 8u > end) {
      if(writeActive) {
        truncated = 1;
        truncatedOp = opCode;
        truncatedAddress = address;
        truncatedRemaining = end - cursor;
        truncatedNeeded = qwords * 8u;
      }
      break;
    }

    switch(opCode) {
	    case 0x2f:
	      otherHi = w0;
	      otherLo = w1;
	      break;
	    case 0x2d:
	      scissorXlo = (w0 >> 12) & 0xfffu;
	      scissorYlo = w0 & 0xfffu;
	      scissorXhi = (w1 >> 12) & 0xfffu;
	      scissorYhi = w1 & 0xfffu;
	      break;
    case 0x32: {
      u32 tileIndex = (w1 >> 24) & 0x7u;
      u32 uls = (w0 >> 12) & 0xfffu;
      u32 ult = w0 & 0xfffu;
      u32 lrs = (w1 >> 12) & 0xfffu;
      u32 lrt = w1 & 0xfffu;
      if(lrs >= uls && lrt >= ult && (tileIndex == currentTile || currentTexWidth == 0 || currentTexHeight == 0)) {
        currentTile = tileIndex;
        currentTexWidth = (lrs - uls) / 4u + 1u;
        currentTexHeight = (lrt - ult) / 4u + 1u;
      }
      break;
    }
    case 0x35:
      currentTile = (w1 >> 24) & 0x7u;
      break;
    case 0x3b:
      env = w1;
      break;
    case 0x3c:
      combineHi = w0;
      combineLo = w1;
      break;
    case 0x3d:
      textureSerial++;
      currentFmt = (w0 >> 21) & 0x7u;
      currentSiz = (w0 >> 19) & 0x3u;
      currentImage = w1 & 0x03ffffffu;
      currentTexWidth = 0;
      currentTexHeight = 0;
      break;
    case 0x3f:
      colorImage = w1 & 0x03ffffffu;
      break;
    default:
      break;
    }

	    if(writeActive && mgb64RdpIsDrawOp(opCode)) {
	      mgb64RdpAddDrawSummary(summaries,
                             summaryCount,
                             summaryTruncated,
                             address,
                             opCode,
                             textureSerial,
                             currentImage,
                             currentFmt,
                             currentSiz,
                             currentTile,
                             currentTexWidth,
                             currentTexHeight,
                             combineHi,
                             combineLo,
                             otherHi,
	                             otherLo,
	                             env,
	                             colorImage);
	      if(traceDrawOps) {
	        mgb64RdpAddDrawOp(memory,
	                          drawOps,
	                          drawOpCount,
	                          drawOpTruncated,
	                          address,
	                          opCode,
	                          textureSerial,
	                          currentImage,
	                          currentFmt,
	                          currentSiz,
	                          currentTile,
	                          currentTexWidth,
	                          currentTexHeight,
	                          combineHi,
	                          combineLo,
	                          otherHi,
	                          otherLo,
	                          env,
	                          colorImage,
	                          scissorXlo,
	                          scissorYlo,
	                          scissorXhi,
	                          scissorYhi);
	      }
	    }

    cursor += qwords * 8u;
  }
  if(writeActive && commands >= maxCommands && cursor < end) truncated = 1;
  if(!writeActive) return;

  const char* path = mgb64RdpTracePath();
  FILE* fp = std::fopen(path, "a");
  if(fp == nullptr) return;
  static u64 renderSerial = 0;
  renderSerial++;
  std::fprintf(fp,
	               "{\"frame\":%llu,\"render_serial\":%llu,\"source\":%u,"
	               "\"start\":\"0x%06x\",\"end\":\"0x%06x\",\"commands\":%u,"
	               "\"truncated\":%u,\"summary_truncated\":%u,\"draw_op_truncated\":%u,"
	               "\"truncated_op\":\"0x%02x\",\"truncated_addr\":\"0x%06x\","
	               "\"truncated_remaining\":%u,\"truncated_needed\":%u,"
	               "\"scissor\":[%u,%u,%u,%u],\"op_counts\":[",
	               (unsigned long long)frame,
	               (unsigned long long)renderSerial,
	               source,
               start,
               end,
	               commands,
	               truncated,
	               summaryTruncated,
	               drawOpTruncated,
	               truncatedOp,
	               truncatedAddress,
	               truncatedRemaining,
	               truncatedNeeded,
	               scissorXlo,
	               scissorYlo,
	               scissorXhi,
	               scissorYhi);
  bool first = true;
  for(u32 opCode = 0; opCode < 64; opCode++) {
    if(opCounts[opCode] == 0) continue;
    std::fprintf(fp, "%s{\"op\":\"0x%02x\",\"count\":%u}", first ? "" : ",", opCode, opCounts[opCode]);
    first = false;
  }
  std::fprintf(fp, "],\"draw_state\":[");
	  for(u32 index = 0; index < summaryCount; index++) {
    const auto& summary = summaries[index];
    std::fprintf(fp,
                 "%s{\"texture_serial\":%u,\"image\":\"0x%06x\",\"image_kseg0\":\"0x%08x\","
                 "\"fmt\":%u,\"siz\":%u,\"tile\":%u,\"width\":%u,\"height\":%u,"
                 "\"draws\":%u,\"op_mask\":\"0x%016llx\","
                 "\"first_addr\":\"0x%06x\",\"last_addr\":\"0x%06x\","
                 "\"combine\":[\"0x%08x\",\"0x%08x\"],"
                 "\"other\":[\"0x%08x\",\"0x%08x\"],"
                 "\"env\":\"0x%08x\",\"color_image\":\"0x%06x\"}",
                 index == 0 ? "" : ",",
                 summary.textureSerial,
                 summary.image,
                 summary.imageKseg0,
                 summary.fmt,
                 summary.siz,
                 summary.tile,
                 summary.width,
                 summary.height,
                 summary.draws,
                 (unsigned long long)summary.opMask,
                 summary.firstAddress,
                 summary.lastAddress,
                 summary.combineHi,
                 summary.combineLo,
                 summary.otherHi,
                 summary.otherLo,
	                 summary.env,
	                 summary.colorImage);
	  }
	  std::fprintf(fp, "],\"draw_ops\":[");
	  for(u32 index = 0; index < drawOpCount; index++) {
	    const auto& draw = drawOps[index];
	    std::fprintf(fp,
	                 "%s{\"addr\":\"0x%06x\",\"op\":\"0x%02x\","
	                 "\"texture_serial\":%u,\"image\":\"0x%06x\",\"image_kseg0\":\"0x%08x\","
	                 "\"fmt\":%u,\"siz\":%u,\"tile\":%u,\"width\":%u,\"height\":%u,"
	                 "\"combine\":[\"0x%08x\",\"0x%08x\"],"
	                 "\"other\":[\"0x%08x\",\"0x%08x\"],"
	                 "\"env\":\"0x%08x\",\"color_image\":\"0x%06x\","
	                 "\"bbox\":[%d,%d,%d,%d],\"valid\":%u,"
	                 "\"setup\":{\"xh\":%d,\"xm\":%d,\"xl\":%d,\"yh\":%d,\"ym\":%d,\"yl\":%d,"
	                 "\"dxhdy\":%d,\"dxmdy\":%d,\"dxldy\":%d,\"flags\":%u}}",
	                 index == 0 ? "" : ",",
	                 draw.address,
	                 draw.opCode,
	                 draw.textureSerial,
	                 draw.image,
	                 draw.imageKseg0,
	                 draw.fmt,
	                 draw.siz,
	                 draw.tile,
	                 draw.width,
	                 draw.height,
	                 draw.combineHi,
	                 draw.combineLo,
	                 draw.otherHi,
	                 draw.otherLo,
	                 draw.env,
	                 draw.colorImage,
	                 draw.bboxX0,
	                 draw.bboxY0,
	                 draw.bboxX1,
	                 draw.bboxY1,
	                 draw.valid,
	                 draw.rawXh,
	                 draw.rawXm,
	                 draw.rawXl,
	                 draw.rawYh,
	                 draw.rawYm,
	                 draw.rawYl,
	                 draw.rawDxhdy,
	                 draw.rawDxmdy,
	                 draw.rawDxldy,
	                 draw.flags);
	  }
	  std::fprintf(fp, "]}\n");
  std::fclose(fp);
}
'''
rdp_block_start = text.find("struct Mgb64RdpDrawSummary")
rdp_block_end = text.find("auto RDP::render() -> void", rdp_block_start)
if rdp_block_start >= 0 and rdp_block_end >= 0:
    text = text[:rdp_block_start] + rdp_trace_block.strip() + "\n\n" + text[rdp_block_end:]
elif "MGB64_ARES_TRACE_RDP_COMMANDS" not in text:
    insert_after = "static const std::vector<string> commandNames = {\n"
    marker_end = "};\n\n"
    start = text.find(insert_after)
    end = text.find(marker_end, start)
    if start >= 0 and end >= 0:
        end += len(marker_end)
        text = text[:end] + "\n" + rdp_trace_block.strip() + "\n\n" + text[end:]
if "mgb64RdpTraceCommandStream(memory, command.source, command.current, command.end);" not in text:
    text = text.replace(
        "  auto& memory = !command.source ? (Memory::Writable&)rdram.ram : (Memory::Writable&)rsp.dmem;\n\n",
        "  auto& memory = !command.source ? (Memory::Writable&)rdram.ram : (Memory::Writable&)rsp.dmem;\n"
        "  mgb64RdpTraceCommandStream(memory, command.source, command.current, command.end);\n\n",
        1,
    )
if "mgb64RdpTraceCommandStream(memory, command.source, command.current, command.end);\n  #if defined(VULKAN)" not in text:
    text = text.replace(
        "auto RDP::render() -> void {\n  #if defined(VULKAN)\n",
        "auto RDP::render() -> void {\n"
        "  auto& memory = !command.source ? (Memory::Writable&)rdram.ram : (Memory::Writable&)rsp.dmem;\n"
        "  mgb64RdpTraceCommandStream(memory, command.source, command.current, command.end);\n"
        "  #if defined(VULKAN)\n",
        1,
    )
    text = text.replace(
        "\n  auto& memory = !command.source ? (Memory::Writable&)rdram.ram : (Memory::Writable&)rsp.dmem;\n"
        "  mgb64RdpTraceCommandStream(memory, command.source, command.current, command.end);\n\n"
        "  auto fetch = [&]() -> u64 {\n",
        "\n  auto fetch = [&]() -> u64 {\n",
        1,
    )
rdp_render_cpp.write_text(text, encoding="utf-8")

rdp_device_cpp = src / "ares/n64/vulkan/parallel-rdp/parallel-rdp/rdp_device.cpp"
text = rdp_device_cpp.read_text(encoding="utf-8")
if "#include <cstdint>\n" not in text:
    text = text.replace(
        "#include <chrono>\n",
        "#include <chrono>\n#include <cstdint>\n",
        1,
    )
if "#include <cstdio>\n" not in text:
    text = text.replace(
        "#include <cstdint>\n",
        "#include <cstdint>\n#include <cstdio>\n",
        1,
    )
if "#include <cstdlib>\n" not in text:
    text = text.replace(
        "#include <cstdio>\n",
        "#include <cstdio>\n#include <cstdlib>\n",
        1,
    )

rdp_pixel_probe_block = r'''struct Mgb64RdpPixelProbeState
{
	bool configured = false;
	bool enabled = false;
	FILE *fp = nullptr;
	uint32_t x = 0;
	uint32_t y = 0;
	uint64_t frame_context = 0;
	uint64_t after_frame_context = 0;
	uint64_t before_frame_context = UINT64_MAX;
	uint64_t command_sequence = 0;
	uint64_t draw_sequence = 0;
	uint64_t frame_command_sequence = 0;
	uint64_t frame_draw_sequence = 0;
	uint32_t max_records = 4096;
	uint32_t records = 0;
	uint32_t max_stats_records = 4096;
	uint32_t stats_records = 0;
	bool changed_only = false;
	bool diagnostics = false;
	bool sample_all_draws = false;
	bool have_last = false;
	uint32_t last_raw = 0;
	uint32_t last_hidden = 0;
	uint32_t fb_addr = 0;
	uint32_t fb_width = 0;
	uint32_t fb_fmt = 0;
	uint32_t fb_size = 0;
	uint64_t commands_seen = 0;
	uint64_t draw_ops_seen = 0;
	uint64_t bbox_ok = 0;
	uint64_t bbox_fail = 0;
	uint64_t bbox_target_miss = 0;
	uint64_t target_hits = 0;
	uint64_t forced_samples = 0;
	uint64_t sample_attempts = 0;
	uint64_t read_ok = 0;
	uint64_t read_fail_no_fb = 0;
	uint64_t read_fail_x_oob = 0;
	uint64_t read_fail_no_maps = 0;
	uint64_t read_fail_no_size = 0;
	uint64_t read_fail_oob = 0;
	uint64_t suppressed_unchanged = 0;
	uint32_t texture_serial = 0;
	uint32_t other_mode_h = 0;
	uint32_t other_mode_l = 0;
	uint32_t combine_hi = 0;
	uint32_t combine_lo = 0;
	uint32_t env_color = 0;
	struct TileState
	{
		bool valid = false;
		uint32_t fmt = 0;
		uint32_t size = 0;
		uint32_t line = 0;
		uint32_t tmem = 0;
		uint32_t palette = 0;
		uint32_t cms = 0;
		uint32_t cmt = 0;
		uint32_t masks = 0;
		uint32_t maskt = 0;
		uint32_t shifts = 0;
		uint32_t shiftt = 0;
		uint32_t uls = 0;
		uint32_t ult = 0;
		uint32_t lrs = 0;
		uint32_t lrt = 0;
	} tiles[8];
};

struct Mgb64RdpPixelProbeSample
{
	uint32_t raw = 0;
	uint32_t hidden = 0;
	uint32_t r = 0;
	uint32_t g = 0;
	uint32_t b = 0;
	uint32_t a = 0;
};

static uint64_t mgb64RdpPixelProbeU64Env(const char *name, uint64_t fallback)
{
	const char *text = getenv(name);
	if (!text || !*text)
		return fallback;
	char *end = nullptr;
	unsigned long long value = strtoull(text, &end, 0);
	return end == text ? fallback : uint64_t(value);
}

static uint32_t mgb64RdpPixelProbeU32Env(const char *name, uint32_t fallback)
{
	return uint32_t(mgb64RdpPixelProbeU64Env(name, fallback));
}

static bool mgb64RdpPixelProbeBoolEnv(const char *name, bool fallback)
{
	const char *text = getenv(name);
	if (!text || !*text)
		return fallback;
	return !(text[0] == '0' || text[0] == 'n' || text[0] == 'N' ||
	         text[0] == 'f' || text[0] == 'F');
}

static Mgb64RdpPixelProbeState &mgb64RdpPixelProbe()
{
	static Mgb64RdpPixelProbeState state;
	if (!state.configured)
	{
		state.configured = true;
		const char *path = getenv("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_PATH");
		state.enabled = mgb64RdpPixelProbeBoolEnv("MGB64_ARES_TRACE_RDP_PIXEL_PROBE", false) &&
		                path && *path;
		state.x = mgb64RdpPixelProbeU32Env("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_X", 0);
		state.y = mgb64RdpPixelProbeU32Env("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_Y", 0);
		state.after_frame_context = mgb64RdpPixelProbeU64Env("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_AFTER_FRAME_CONTEXT", 0);
		state.before_frame_context = mgb64RdpPixelProbeU64Env("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_BEFORE_FRAME_CONTEXT", UINT64_MAX);
		state.max_records = mgb64RdpPixelProbeU32Env("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_MAX_RECORDS", 4096);
		state.max_stats_records = mgb64RdpPixelProbeU32Env("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_MAX_STATS_RECORDS", 4096);
		state.changed_only = mgb64RdpPixelProbeBoolEnv("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_CHANGED_ONLY", false);
		state.diagnostics = mgb64RdpPixelProbeBoolEnv("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_DIAGNOSTICS", false);
		state.sample_all_draws = mgb64RdpPixelProbeBoolEnv("MGB64_ARES_TRACE_RDP_PIXEL_PROBE_ALL_DRAWS", false);
		if (state.enabled)
		{
			state.fp = fopen(path, "w");
			if (!state.fp)
			{
				LOGE("Failed to open MGB64 RDP pixel probe: %s.\n", path);
				state.enabled = false;
			}
			else
				LOGI("MGB64 RDP pixel probe: %s x=%u y=%u.\n", path, state.x, state.y);
		}
	}
	return state;
}

static void mgb64RdpPixelProbeEmitStats(Mgb64RdpPixelProbeState &state,
                                        const char *event)
{
	if (!state.enabled || !state.fp || !state.diagnostics || state.stats_records >= state.max_stats_records)
		return;
	fprintf(state.fp,
	        "{\"type\":\"stats\",\"event\":\"%s\",\"frame_context\":%llu,"
	        "\"command_sequence\":%llu,\"draw_sequence\":%llu,"
	        "\"x\":%u,\"y\":%u,\"fb_addr\":\"0x%06x\","
	        "\"fb_width\":%u,\"fb_fmt\":%u,\"fb_size\":%u,"
	        "\"commands_seen\":%llu,\"draw_ops_seen\":%llu,"
	        "\"bbox_ok\":%llu,\"bbox_fail\":%llu,\"bbox_target_miss\":%llu,"
	        "\"target_hits\":%llu,\"forced_samples\":%llu,"
	        "\"sample_attempts\":%llu,\"read_ok\":%llu,"
	        "\"read_fail_no_fb\":%llu,\"read_fail_x_oob\":%llu,"
	        "\"read_fail_no_maps\":%llu,\"read_fail_no_size\":%llu,"
	        "\"read_fail_oob\":%llu,\"suppressed_unchanged\":%llu,"
	        "\"records\":%u,\"max_records\":%u}\n",
	        event,
	        (unsigned long long)state.frame_context,
	        (unsigned long long)state.command_sequence,
	        (unsigned long long)state.draw_sequence,
	        state.x,
	        state.y,
	        state.fb_addr,
	        state.fb_width,
	        state.fb_fmt,
	        state.fb_size,
	        (unsigned long long)state.commands_seen,
	        (unsigned long long)state.draw_ops_seen,
	        (unsigned long long)state.bbox_ok,
	        (unsigned long long)state.bbox_fail,
	        (unsigned long long)state.bbox_target_miss,
	        (unsigned long long)state.target_hits,
	        (unsigned long long)state.forced_samples,
	        (unsigned long long)state.sample_attempts,
	        (unsigned long long)state.read_ok,
	        (unsigned long long)state.read_fail_no_fb,
	        (unsigned long long)state.read_fail_x_oob,
	        (unsigned long long)state.read_fail_no_maps,
	        (unsigned long long)state.read_fail_no_size,
	        (unsigned long long)state.read_fail_oob,
	        (unsigned long long)state.suppressed_unchanged,
	        state.records,
	        state.max_records);
	fflush(state.fp);
	state.stats_records++;
}

static void mgb64RdpPixelProbeBeginFrameContext()
{
	auto &state = mgb64RdpPixelProbe();
	mgb64RdpPixelProbeEmitStats(state, "begin_frame_context");
	state.frame_context++;
	state.frame_command_sequence = 0;
	state.frame_draw_sequence = 0;
	state.have_last = false;
}

static bool mgb64RdpPixelProbeDrawOp(uint32_t op)
{
	return (op >= 0x08 && op <= 0x0f) || op == 0x24 || op == 0x25 || op == 0x36;
}

static int32_t mgb64RdpPixelProbeSignExtend(uint32_t value, uint32_t bits)
{
	uint32_t mask = 1u << (bits - 1u);
	return int32_t((value ^ mask) - mask);
}

static void mgb64RdpPixelProbeInterpolateX(int32_t xh, int32_t xm, int32_t xl,
                                           int32_t dxhdy, int32_t dxmdy, int32_t dxldy,
                                           int32_t yh, int32_t ym, int32_t y,
                                           bool flip, int32_t *left, int32_t *right)
{
	int64_t yh_base = int64_t(yh & ~3);
	int64_t ym_base = int64_t(ym);
	int64_t right_edge = int64_t(xh) + int64_t(y - yh_base) * int64_t(dxhdy);
	int64_t mid_edge = int64_t(xm) + int64_t(y - yh_base) * int64_t(dxmdy);
	int64_t left_edge = int64_t(xl) + int64_t(y - ym_base) * int64_t(dxldy);
	if (y < ym)
		left_edge = mid_edge;
	int32_t right_shifted = int32_t(right_edge >> 15);
	int32_t left_shifted = int32_t(left_edge >> 15);
	if (flip)
	{
		*left = right_shifted;
		*right = left_shifted;
	}
	else
	{
		*left = left_shifted;
		*right = right_shifted;
	}
}

static bool mgb64RdpPixelProbeCommandBbox(uint32_t op, const uint32_t *words,
                                          const ScissorState &scissor,
                                          int32_t *bbox_x0, int32_t *bbox_y0,
                                          int32_t *bbox_x1, int32_t *bbox_y1)
{
	if (op >= 0x08 && op <= 0x0f)
	{
		bool flip = (words[0] & 0x00800000u) != 0;
		int32_t yl = mgb64RdpPixelProbeSignExtend(words[0] & 0x3fffu, 14);
		int32_t ym = mgb64RdpPixelProbeSignExtend((words[1] >> 16) & 0x3fffu, 14);
		int32_t yh = mgb64RdpPixelProbeSignExtend(words[1] & 0x3fffu, 14);
		int32_t xl = mgb64RdpPixelProbeSignExtend(words[2] & 0x0fffffffu, 28) >> 1;
		int32_t xh = mgb64RdpPixelProbeSignExtend(words[4] & 0x0fffffffu, 28) >> 1;
		int32_t xm = mgb64RdpPixelProbeSignExtend(words[6] & 0x0fffffffu, 28) >> 1;
		int32_t dxldy = mgb64RdpPixelProbeSignExtend((words[3] >> 2) & 0x0fffffffu, 28) >> 1;
		int32_t dxhdy = mgb64RdpPixelProbeSignExtend((words[5] >> 2) & 0x0fffffffu, 28) >> 1;
		int32_t dxmdy = mgb64RdpPixelProbeSignExtend((words[7] >> 2) & 0x0fffffffu, 28) >> 1;
		if (yl <= yh)
			return false;

		int32_t start_y = yh & ~3;
		int32_t end_y = (yl - 1) | 3;
		if (start_y < int32_t(scissor.ylo))
			start_y = int32_t(scissor.ylo);
		if (end_y > int32_t(scissor.yhi) - 1)
			end_y = int32_t(scissor.yhi) - 1;
		if (end_y < start_y)
			return false;

		int32_t upper_left = 0, upper_right = 0;
		int32_t lower_left = 0, lower_right = 0;
		int32_t mid_left = 0, mid_right = 0;
		int32_t mid1_left = 0, mid1_right = 0;
		mgb64RdpPixelProbeInterpolateX(xh, xm, xl, dxhdy, dxmdy, dxldy, yh, ym, start_y, flip, &upper_left, &upper_right);
		mgb64RdpPixelProbeInterpolateX(xh, xm, xl, dxhdy, dxmdy, dxldy, yh, ym, end_y, flip, &lower_left, &lower_right);
		mid_left = mid1_left = upper_left;
		mid_right = mid1_right = upper_right;
		if (ym > start_y && ym < end_y)
		{
			mgb64RdpPixelProbeInterpolateX(xh, xm, xl, dxhdy, dxmdy, dxldy, yh, ym, ym, flip, &mid_left, &mid_right);
			mgb64RdpPixelProbeInterpolateX(xh, xm, xl, dxhdy, dxmdy, dxldy, yh, ym, ym - 1, flip, &mid1_left, &mid1_right);
		}

		int32_t start_x = upper_left;
		if (lower_left < start_x) start_x = lower_left;
		if (mid_left < start_x) start_x = mid_left;
		if (mid1_left < start_x) start_x = mid1_left;
		int32_t end_x = upper_right;
		if (lower_right > end_x) end_x = lower_right;
		if (mid_right > end_x) end_x = mid_right;
		if (mid1_right > end_x) end_x = mid1_right;

		int32_t clip_x0 = int32_t(scissor.xlo >> 2);
		int32_t clip_x1 = int32_t((scissor.xhi + 3u) >> 2) - 1;
		if (start_x < clip_x0) start_x = clip_x0;
		if (end_x > clip_x1) end_x = clip_x1;
		if (end_x < start_x)
			return false;

		*bbox_x0 = start_x;
		*bbox_y0 = start_y / 4;
		*bbox_x1 = end_x + 1;
		*bbox_y1 = end_y / 4 + 1;
		return true;
	}
	else if (op == 0x24 || op == 0x25 || op == 0x36)
	{
		int32_t x0 = int32_t((words[1] >> 12) & 0xfffu) / 4;
		int32_t y0 = int32_t(words[1] & 0xfffu) / 4;
		int32_t x1 = int32_t((words[0] >> 12) & 0xfffu) / 4 + 1;
		int32_t y1 = int32_t(words[0] & 0xfffu) / 4 + 1;
		if (x1 < x0) { int32_t tmp = x0; x0 = x1; x1 = tmp; }
		if (y1 < y0) { int32_t tmp = y0; y0 = y1; y1 = tmp; }
		*bbox_x0 = x0;
		*bbox_y0 = y0;
		*bbox_x1 = x1;
		*bbox_y1 = y1;
		return true;
	}
	return false;
}

static bool mgb64RdpPixelProbeReadSample(CommandProcessor &processor,
                                         Mgb64RdpPixelProbeState &state,
                                         Mgb64RdpPixelProbeSample &sample)
{
	if (!state.fb_width)
	{
		state.read_fail_no_fb++;
		return false;
	}
	if (state.x >= state.fb_width)
	{
		state.read_fail_x_oob++;
		return false;
	}

	auto *dram = static_cast<const uint8_t *>(processor.begin_read_rdram());
	auto *hidden = static_cast<const uint8_t *>(processor.begin_read_hidden_rdram());
	if (!dram || !hidden)
	{
		state.read_fail_no_maps++;
		return false;
	}

	size_t rdram_size = processor.get_rdram_size();
	size_t hidden_size = processor.get_hidden_rdram_size();
	if (!rdram_size || !hidden_size)
	{
		state.read_fail_no_size++;
		return false;
	}

	if (state.fb_size == 2)
	{
		uint32_t index = (state.fb_addr >> 1u) + state.fb_width * state.y + state.x;
		if (2u * index + 1u >= rdram_size || index >= hidden_size)
		{
			state.read_fail_oob++;
			return false;
		}
		const auto *v16 = reinterpret_cast<const uint16_t *>(dram);
		uint32_t word = uint32_t(v16[index ^ 1u]);
		uint32_t hidden_word = uint32_t(hidden[index]);
		sample.raw = word;
		sample.hidden = hidden_word;
		if (state.fb_fmt == 0)
		{
			sample.r = (word >> 8u) & 0xf8u;
			sample.g = (word >> 3u) & 0xf8u;
			sample.b = (word << 2u) & 0xf8u;
			sample.a = ((hidden_word & 3u) << 5u) | ((word & 1u) << 7u);
		}
		else
		{
			sample.r = (word >> 8u) & 0xffu;
			sample.g = sample.r;
			sample.b = sample.r;
			sample.a = word & 0xffu;
		}
		state.read_ok++;
		return true;
	}
	else if (state.fb_size == 3)
	{
		uint32_t index = (state.fb_addr >> 2u) + state.fb_width * state.y + state.x;
		if (4u * index + 3u >= rdram_size || 2u * index + 1u >= hidden_size)
		{
			state.read_fail_oob++;
			return false;
		}
		const auto *v32 = reinterpret_cast<const uint32_t *>(dram);
		uint32_t word = v32[index];
		sample.raw = word;
		sample.hidden = (uint32_t(hidden[2u * index]) << 8u) | uint32_t(hidden[2u * index + 1u]);
		sample.r = (word >> 24u) & 0xffu;
		sample.g = (word >> 16u) & 0xffu;
		sample.b = (word >> 8u) & 0xffu;
		sample.a = word & 0xffu;
		state.read_ok++;
		return true;
	}
	else
	{
		uint32_t index = state.fb_addr + state.fb_width * state.y + state.x;
		if (index >= rdram_size)
		{
			state.read_fail_oob++;
			return false;
		}
		uint32_t word = uint32_t(dram[index ^ 3u]);
		sample.raw = word;
		sample.hidden = index / 2u < hidden_size ? uint32_t(hidden[index / 2u]) : 0;
		sample.r = word;
		sample.g = word;
		sample.b = word;
		sample.a = sample.hidden;
		state.read_ok++;
		return true;
	}
}

static void mgb64RdpPixelProbeWriteDrawWords(FILE *fp,
                                             const uint32_t *words,
                                             unsigned word_count)
{
	unsigned limit = word_count <= 64u ? word_count : 64u;
	fputs("\"draw_words\":[", fp);
	for (unsigned index = 0; index < limit; index++)
		fprintf(fp, "%s\"0x%08x\"", index == 0 ? "" : ",", words[index]);
	fputs("]", fp);
}

static uint32_t mgb64RdpPixelProbeDrawTile(uint32_t op, const uint32_t *words)
{
	if (op >= 0x08 && op <= 0x0f)
		return (words[0] >> 16) & 7u;
	if (op == 0x24 || op == 0x25)
		return (words[1] >> 24) & 7u;
	return 0;
}

static void mgb64RdpPixelProbeUpdateState(Mgb64RdpPixelProbeState &state,
                                          uint32_t op,
                                          const uint32_t *words)
{
	switch (op)
	{
	case 0x2f:
		state.other_mode_h = words[0];
		state.other_mode_l = words[1];
		break;
	case 0x3b:
		state.env_color = words[1];
		break;
	case 0x3c:
		state.combine_hi = words[0];
		state.combine_lo = words[1];
		break;
	case 0x3d:
		state.texture_serial++;
		break;
	case 0x35:
	{
		uint32_t tile = (words[1] >> 24) & 7u;
		auto &tile_state = state.tiles[tile];
		tile_state.valid = true;
		tile_state.fmt = (words[0] >> 21) & 7u;
		tile_state.size = (words[0] >> 19) & 3u;
		tile_state.line = (words[0] >> 9) & 0x1ffu;
		tile_state.tmem = words[0] & 0x1ffu;
		tile_state.palette = (words[1] >> 20) & 0xfu;
		tile_state.cmt = (words[1] >> 18) & 3u;
		tile_state.maskt = (words[1] >> 14) & 0xfu;
		tile_state.shiftt = (words[1] >> 10) & 0xfu;
		tile_state.cms = (words[1] >> 8) & 3u;
		tile_state.masks = (words[1] >> 4) & 0xfu;
		tile_state.shifts = words[1] & 0xfu;
		break;
	}
	case 0x32:
	{
		uint32_t tile = (words[1] >> 24) & 7u;
		auto &tile_state = state.tiles[tile];
		tile_state.valid = true;
		tile_state.uls = (words[0] >> 12) & 0xfffu;
		tile_state.ult = words[0] & 0xfffu;
		tile_state.lrs = (words[1] >> 12) & 0xfffu;
		tile_state.lrt = words[1] & 0xfffu;
		break;
	}
	default:
		break;
	}
}

static void mgb64RdpPixelProbeAfterCommand(CommandProcessor &processor,
                                           uint32_t op,
                                           uint64_t command_sequence,
                                           uint64_t draw_sequence,
                                           uint64_t frame_command_sequence,
                                           uint64_t frame_draw_sequence,
                                           uint32_t texture_image,
                                           uint32_t texture_width,
                                           uint32_t texture_fmt,
                                           uint32_t texture_size,
                                           const StaticRasterizationState &static_state,
                                           const DepthBlendState &depth_blend,
                                           const ScissorState &scissor_state,
                                           const uint32_t *draw_words,
                                           unsigned draw_word_count,
                                           uint32_t draw_tile,
                                           bool bbox_valid,
                                           int32_t bbox_x0,
                                           int32_t bbox_y0,
                                           int32_t bbox_x1,
                                           int32_t bbox_y1)
{
	auto &state = mgb64RdpPixelProbe();
	if (!state.enabled || !state.fp || state.records >= state.max_records)
		return;
	if (state.frame_context < state.after_frame_context || state.frame_context > state.before_frame_context)
		return;

	state.sample_attempts++;
	processor.wait_for_timeline(processor.signal_timeline());
	Mgb64RdpPixelProbeSample sample;
	if (!mgb64RdpPixelProbeReadSample(processor, state, sample))
		return;

	bool changed = !state.have_last || sample.raw != state.last_raw || sample.hidden != state.last_hidden;
	const auto &tile_state = state.tiles[draw_tile & 7u];
	state.have_last = true;
	state.last_raw = sample.raw;
	state.last_hidden = sample.hidden;
	if (state.changed_only && !changed)
	{
		state.suppressed_unchanged++;
		return;
	}

	fprintf(state.fp,
	        "{\"type\":\"sample\",\"frame_context\":%llu,\"command_sequence\":%llu,\"draw_sequence\":%llu,"
	        "\"frame_command_sequence\":%llu,\"frame_draw_sequence\":%llu,"
	        "\"op\":\"0x%02x\",\"x\":%u,\"y\":%u,\"fb_addr\":\"0x%06x\","
	        "\"fb_width\":%u,\"fb_fmt\":%u,\"fb_size\":%u,"
	        "\"texture_image\":\"0x%06x\",\"texture_width\":%u,"
	        "\"texture_fmt\":%u,\"texture_size\":%u,\"texture_serial\":%u,"
	        "\"other\":[\"0x%08x\",\"0x%08x\"],"
	        "\"combine\":[\"0x%08x\",\"0x%08x\"],\"env\":\"0x%08x\","
	        "\"scissor\":[%u,%u,%u,%u],"
	        "\"raster_flags\":\"0x%08x\",\"raster_dither\":%u,"
	        "\"static_texture_fmt\":%u,\"static_texture_size\":%u,"
	        "\"depth_flags\":\"0x%08x\",\"coverage_mode\":%u,\"z_mode\":%u,"
	        "\"blend_cycles\":[[%u,%u,%u,%u],[%u,%u,%u,%u]],"
	        "\"combiner\":{\"rgb\":[[%u,%u,%u,%u],[%u,%u,%u,%u]],"
	        "\"alpha\":[[%u,%u,%u,%u],[%u,%u,%u,%u]]},"
	        "\"draw_tile\":%u,"
	        "\"tile_state\":{\"valid\":%u,\"fmt\":%u,\"size\":%u,\"line\":%u,"
	        "\"tmem\":%u,\"palette\":%u,\"cms\":%u,\"cmt\":%u,\"masks\":%u,"
	        "\"maskt\":%u,\"shifts\":%u,\"shiftt\":%u,"
	        "\"uls\":%u,\"ult\":%u,\"lrs\":%u,\"lrt\":%u},"
	        "\"bbox_valid\":%u,\"bbox\":[%d,%d,%d,%d],"
	        "\"draw_word_count\":%u,\"draw_words_truncated\":%u,",
	        (unsigned long long)state.frame_context,
	        (unsigned long long)command_sequence,
	        (unsigned long long)draw_sequence,
	        (unsigned long long)frame_command_sequence,
	        (unsigned long long)frame_draw_sequence,
	        op,
	        state.x,
	        state.y,
	        state.fb_addr,
	        state.fb_width,
	        state.fb_fmt,
	        state.fb_size,
	        texture_image,
	        texture_width,
	        texture_fmt,
	        texture_size,
	        state.texture_serial,
	        state.other_mode_h,
	        state.other_mode_l,
	        state.combine_hi,
	        state.combine_lo,
	        state.env_color,
	        scissor_state.xlo,
	        scissor_state.ylo,
	        scissor_state.xhi,
	        scissor_state.yhi,
	        uint32_t(static_state.flags),
	        static_state.dither,
	        static_state.texture_fmt,
	        static_state.texture_size,
	        uint32_t(depth_blend.flags),
	        uint32_t(depth_blend.coverage_mode),
	        uint32_t(depth_blend.z_mode),
	        uint32_t(depth_blend.blend_cycles[0].blend_1a),
	        uint32_t(depth_blend.blend_cycles[0].blend_1b),
	        uint32_t(depth_blend.blend_cycles[0].blend_2a),
	        uint32_t(depth_blend.blend_cycles[0].blend_2b),
	        uint32_t(depth_blend.blend_cycles[1].blend_1a),
	        uint32_t(depth_blend.blend_cycles[1].blend_1b),
	        uint32_t(depth_blend.blend_cycles[1].blend_2a),
	        uint32_t(depth_blend.blend_cycles[1].blend_2b),
	        uint32_t(static_state.combiner[0].rgb.muladd),
	        uint32_t(static_state.combiner[0].rgb.mulsub),
	        uint32_t(static_state.combiner[0].rgb.mul),
	        uint32_t(static_state.combiner[0].rgb.add),
	        uint32_t(static_state.combiner[1].rgb.muladd),
	        uint32_t(static_state.combiner[1].rgb.mulsub),
	        uint32_t(static_state.combiner[1].rgb.mul),
	        uint32_t(static_state.combiner[1].rgb.add),
	        uint32_t(static_state.combiner[0].alpha.muladd),
	        uint32_t(static_state.combiner[0].alpha.mulsub),
	        uint32_t(static_state.combiner[0].alpha.mul),
	        uint32_t(static_state.combiner[0].alpha.add),
	        uint32_t(static_state.combiner[1].alpha.muladd),
	        uint32_t(static_state.combiner[1].alpha.mulsub),
	        uint32_t(static_state.combiner[1].alpha.mul),
	        uint32_t(static_state.combiner[1].alpha.add),
	        draw_tile,
	        tile_state.valid ? 1u : 0u,
	        tile_state.fmt,
	        tile_state.size,
	        tile_state.line,
	        tile_state.tmem,
	        tile_state.palette,
	        tile_state.cms,
	        tile_state.cmt,
	        tile_state.masks,
	        tile_state.maskt,
	        tile_state.shifts,
	        tile_state.shiftt,
	        tile_state.uls,
	        tile_state.ult,
	        tile_state.lrs,
	        tile_state.lrt,
	        bbox_valid ? 1u : 0u,
	        bbox_x0,
	        bbox_y0,
	        bbox_x1,
	        bbox_y1,
	        draw_word_count,
	        draw_word_count > 64u ? 1u : 0u);
	mgb64RdpPixelProbeWriteDrawWords(state.fp, draw_words, draw_word_count);
	fprintf(state.fp,
	        ",\"raw\":\"0x%08x\",\"hidden\":\"0x%08x\","
	        "\"rgba\":[%u,%u,%u,%u],\"changed\":%u}\n",
	        sample.raw,
	        sample.hidden,
	        sample.r,
	        sample.g,
	        sample.b,
	        sample.a,
	        changed ? 1u : 0u);
	fflush(state.fp);
	state.records++;
}
'''

rdp_pixel_probe_start = text.find("struct Mgb64RdpPixelProbeState")
rdp_pixel_probe_end = text.find("CommandProcessor::CommandProcessor", rdp_pixel_probe_start)
if rdp_pixel_probe_start >= 0 and rdp_pixel_probe_end >= 0:
    text = text[:rdp_pixel_probe_start] + rdp_pixel_probe_block + "\n\n" + text[rdp_pixel_probe_end:]
elif "struct Mgb64RdpPixelProbeState" not in text:
    text = text.replace(
        "namespace RDP\n{\n",
        "namespace RDP\n{\n" + rdp_pixel_probe_block + "\n",
        1,
    )
if "mgb64RdpPixelProbeBeginFrameContext();" not in text:
    text = text.replace(
        "void CommandProcessor::begin_frame_context()\n{\n\tflush();\n\tdrain_command_ring();\n\tdevice.next_frame_context();\n}",
        "void CommandProcessor::begin_frame_context()\n{\n\tflush();\n\tdrain_command_ring();\n\tdevice.next_frame_context();\n\tmgb64RdpPixelProbeBeginFrameContext();\n}",
        1,
    )
rdp_enqueue_command_probe = r'''void CommandProcessor::enqueue_command(unsigned num_words, const uint32_t *words)
{
	auto &mgb64_pixel_probe = mgb64RdpPixelProbe();
	uint32_t mgb64_pixel_probe_op = (words[0] >> 24) & 63;
	uint64_t mgb64_pixel_probe_command_sequence = ++mgb64_pixel_probe.command_sequence;
	uint64_t mgb64_pixel_probe_frame_command_sequence = ++mgb64_pixel_probe.frame_command_sequence;
	uint64_t mgb64_pixel_probe_draw_sequence = mgb64_pixel_probe.draw_sequence;
	uint64_t mgb64_pixel_probe_frame_draw_sequence = mgb64_pixel_probe.frame_draw_sequence;
	int32_t mgb64_pixel_probe_bbox_x0 = 0;
	int32_t mgb64_pixel_probe_bbox_y0 = 0;
	int32_t mgb64_pixel_probe_bbox_x1 = 0;
	int32_t mgb64_pixel_probe_bbox_y1 = 0;
	bool mgb64_pixel_probe_bbox_valid = false;
	bool mgb64_pixel_probe_sample = false;
	uint32_t mgb64_pixel_probe_draw_tile = 0;
	if (mgb64_pixel_probe.enabled)
		mgb64_pixel_probe.commands_seen++;
	mgb64RdpPixelProbeUpdateState(mgb64_pixel_probe, mgb64_pixel_probe_op, words);
	if (mgb64_pixel_probe_op == 0x3f)
	{
		mgb64_pixel_probe.fb_fmt = (words[0] >> 21) & 7;
		mgb64_pixel_probe.fb_size = (words[0] >> 19) & 3;
		mgb64_pixel_probe.fb_width = (words[0] & 1023) + 1;
		mgb64_pixel_probe.fb_addr = words[1] & 0x00ffffffu;
	}
	if (mgb64RdpPixelProbeDrawOp(mgb64_pixel_probe_op))
	{
		mgb64_pixel_probe_draw_tile = mgb64RdpPixelProbeDrawTile(mgb64_pixel_probe_op, words);
		mgb64_pixel_probe_draw_sequence = ++mgb64_pixel_probe.draw_sequence;
		mgb64_pixel_probe_frame_draw_sequence = ++mgb64_pixel_probe.frame_draw_sequence;
		if (mgb64_pixel_probe.enabled)
		{
			mgb64_pixel_probe.draw_ops_seen++;
			mgb64_pixel_probe_bbox_valid = mgb64RdpPixelProbeCommandBbox(mgb64_pixel_probe_op, words, scissor_state,
			                                                             &mgb64_pixel_probe_bbox_x0,
			                                                             &mgb64_pixel_probe_bbox_y0,
			                                                             &mgb64_pixel_probe_bbox_x1,
			                                                             &mgb64_pixel_probe_bbox_y1);
			if (mgb64_pixel_probe_bbox_valid)
			{
				mgb64_pixel_probe.bbox_ok++;
				mgb64_pixel_probe_sample = int32_t(mgb64_pixel_probe.x) >= mgb64_pixel_probe_bbox_x0 &&
				                         int32_t(mgb64_pixel_probe.x) < mgb64_pixel_probe_bbox_x1 &&
				                         int32_t(mgb64_pixel_probe.y) >= mgb64_pixel_probe_bbox_y0 &&
				                         int32_t(mgb64_pixel_probe.y) < mgb64_pixel_probe_bbox_y1;
				if (mgb64_pixel_probe_sample)
					mgb64_pixel_probe.target_hits++;
				else
					mgb64_pixel_probe.bbox_target_miss++;
			}
			else
				mgb64_pixel_probe.bbox_fail++;
			if (!mgb64_pixel_probe_sample && mgb64_pixel_probe.sample_all_draws)
			{
				mgb64_pixel_probe_sample = true;
				mgb64_pixel_probe.forced_samples++;
			}
		}
	}

	if (dump_writer && !dump_in_command_list)
	{
		wait_for_timeline(signal_timeline());
		dump_writer->flush_dram(begin_read_rdram(), rdram_size);
		dump_writer->flush_hidden_dram(begin_read_hidden_rdram(), hidden_rdram->get_create_info().size);
		dump_in_command_list = true;
	}

	enqueue_command_inner(num_words, words);

	if (dump_writer)
	{
		uint32_t cmd_id = (words[0] >> 24) & 63;
		if (Op(cmd_id) == Op::SyncFull)
		{
			dump_writer->signal_complete();
			dump_in_command_list = false;
		}
		else
			dump_writer->emit_command(cmd_id, words, num_words);
	}

	if (mgb64_pixel_probe_sample)
	{
		mgb64RdpPixelProbeAfterCommand(*this,
		                               mgb64_pixel_probe_op,
		                               mgb64_pixel_probe_command_sequence,
		                               mgb64_pixel_probe_draw_sequence,
		                               mgb64_pixel_probe_frame_command_sequence,
		                               mgb64_pixel_probe_frame_draw_sequence,
		                               texture_image.addr,
		                               texture_image.width,
		                               uint32_t(texture_image.fmt),
		                               uint32_t(texture_image.size),
		                               static_state,
		                               depth_blend,
		                               scissor_state,
		                               words,
		                               num_words,
		                               mgb64_pixel_probe_draw_tile,
		                               mgb64_pixel_probe_bbox_valid,
		                               mgb64_pixel_probe_bbox_x0,
		                               mgb64_pixel_probe_bbox_y0,
		                               mgb64_pixel_probe_bbox_x1,
		                               mgb64_pixel_probe_bbox_y1);
	}
}
'''
rdp_enqueue_start = text.find("void CommandProcessor::enqueue_command(unsigned num_words, const uint32_t *words)")
rdp_enqueue_end = text.find("void CommandProcessor::enqueue_command_direct", rdp_enqueue_start)
if rdp_enqueue_start >= 0 and rdp_enqueue_end >= 0:
    text = text[:rdp_enqueue_start] + rdp_enqueue_command_probe + "\n\n" + text[rdp_enqueue_end:]
elif "mgb64RdpPixelProbeAfterCommand(*this," not in text:
    text = text.replace(
        "void CommandProcessor::enqueue_command(unsigned num_words, const uint32_t *words)\n{\n\tif (dump_writer && !dump_in_command_list)",
        "void CommandProcessor::enqueue_command(unsigned num_words, const uint32_t *words)\n{\n\tauto &mgb64_pixel_probe = mgb64RdpPixelProbe();\n\tuint32_t mgb64_pixel_probe_op = (words[0] >> 24) & 63;\n\tuint64_t mgb64_pixel_probe_command_sequence = ++mgb64_pixel_probe.command_sequence;\n\tuint64_t mgb64_pixel_probe_frame_command_sequence = ++mgb64_pixel_probe.frame_command_sequence;\n\tuint64_t mgb64_pixel_probe_draw_sequence = mgb64_pixel_probe.draw_sequence;\n\tuint64_t mgb64_pixel_probe_frame_draw_sequence = mgb64_pixel_probe.frame_draw_sequence;\n\tint32_t mgb64_pixel_probe_bbox_x0 = 0;\n\tint32_t mgb64_pixel_probe_bbox_y0 = 0;\n\tint32_t mgb64_pixel_probe_bbox_x1 = 0;\n\tint32_t mgb64_pixel_probe_bbox_y1 = 0;\n\tbool mgb64_pixel_probe_sample = false;\n\tuint32_t mgb64_pixel_probe_draw_tile = 0;\n\tmgb64RdpPixelProbeUpdateState(mgb64_pixel_probe, mgb64_pixel_probe_op, words);\n\tif (mgb64_pixel_probe_op == 0x3f)\n\t{\n\t\tmgb64_pixel_probe.fb_fmt = (words[0] >> 21) & 7;\n\t\tmgb64_pixel_probe.fb_size = (words[0] >> 19) & 3;\n\t\tmgb64_pixel_probe.fb_width = (words[0] & 1023) + 1;\n\t\tmgb64_pixel_probe.fb_addr = words[1] & 0x00ffffffu;\n\t}\n\tif (mgb64RdpPixelProbeDrawOp(mgb64_pixel_probe_op))\n\t{\n\t\tmgb64_pixel_probe_draw_tile = mgb64RdpPixelProbeDrawTile(mgb64_pixel_probe_op, words);\n\t\tmgb64_pixel_probe_draw_sequence = ++mgb64_pixel_probe.draw_sequence;\n\t\tmgb64_pixel_probe_frame_draw_sequence = ++mgb64_pixel_probe.frame_draw_sequence;\n\t\tif (mgb64_pixel_probe.enabled && mgb64RdpPixelProbeCommandBbox(mgb64_pixel_probe_op, words, scissor_state,\n\t\t                                                               &mgb64_pixel_probe_bbox_x0,\n\t\t                                                               &mgb64_pixel_probe_bbox_y0,\n\t\t                                                               &mgb64_pixel_probe_bbox_x1,\n\t\t                                                               &mgb64_pixel_probe_bbox_y1))\n\t\t{\n\t\t\tmgb64_pixel_probe_sample = int32_t(mgb64_pixel_probe.x) >= mgb64_pixel_probe_bbox_x0 &&\n\t\t\t                         int32_t(mgb64_pixel_probe.x) < mgb64_pixel_probe_bbox_x1 &&\n\t\t\t                         int32_t(mgb64_pixel_probe.y) >= mgb64_pixel_probe_bbox_y0 &&\n\t\t\t                         int32_t(mgb64_pixel_probe.y) < mgb64_pixel_probe_bbox_y1;\n\t\t}\n\t}\n\n\tif (dump_writer && !dump_in_command_list)",
        1,
    )
    text = text.replace(
        "\tif (dump_writer)\n\t{\n\t\tuint32_t cmd_id = (words[0] >> 24) & 63;\n\t\tif (Op(cmd_id) == Op::SyncFull)\n\t\t{",
        "\tif (dump_writer)\n\t{\n\t\tuint32_t cmd_id = (words[0] >> 24) & 63;\n\t\tif (Op(cmd_id) == Op::SyncFull)\n\t\t{",
        1,
    )
    text = text.replace(
        "\t\telse\n\t\t\tdump_writer->emit_command(cmd_id, words, num_words);\n\t}\n}",
        "\t\telse\n\t\t\tdump_writer->emit_command(cmd_id, words, num_words);\n\t}\n\n\tif (mgb64_pixel_probe_sample)\n\t{\n\t\tmgb64RdpPixelProbeAfterCommand(*this,\n\t\t                               mgb64_pixel_probe_op,\n\t\t                               mgb64_pixel_probe_command_sequence,\n\t\t                               mgb64_pixel_probe_draw_sequence,\n\t\t                               mgb64_pixel_probe_frame_command_sequence,\n\t\t                               mgb64_pixel_probe_frame_draw_sequence,\n\t\t                               texture_image.addr,\n\t\t                               texture_image.width,\n\t\t                               uint32_t(texture_image.fmt),\n\t\t                               uint32_t(texture_image.size),\n\t\t                               static_state,\n\t\t                               depth_blend,\n\t\t                               scissor_state,\n\t\t                               words,\n\t\t                               num_words,\n\t\t                               mgb64_pixel_probe_draw_tile,\n\t\t                               true,\n\t\t                               mgb64_pixel_probe_bbox_x0,\n\t\t                               mgb64_pixel_probe_bbox_y0,\n\t\t                               mgb64_pixel_probe_bbox_x1,\n\t\t                               mgb64_pixel_probe_bbox_y1);\n\t}\n}",
        1,
    )
text = text.replace(
    "\tif (state.fb_size == 1 || state.fb_size == 2)\n",
    "\tif (state.fb_size == 2)\n",
)
if "mgb64RdpPixelProbe().enabled" not in text:
    text = text.replace(
        "\tif (rdram_ptr)\n",
        "\tif (mgb64RdpPixelProbe().enabled)\n"
        "\t\tflags |= COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT;\n"
        "\n"
        "\tif (rdram_ptr)\n",
        1,
    )
rdp_device_cpp.write_text(text, encoding="utf-8")

screen_cpp = src / "ares/ares/node/video/screen.cpp"
text = screen_cpp.read_text(encoding="utf-8")
if "mgb64OraclePresentedVideoDump(std::shared_ptr<Screen>" not in text:
    text = text.replace(
        "#include <memory>\n",
        "#include <memory>\n"
        "\n"
        "auto mgb64OraclePresentedVideoDump(std::shared_ptr<Screen> screen, const u32* pixels, u32 pitch, u32 width, u32 height) -> void;\n",
        1,
    )
if "mgb64OraclePresentedVideoDump(presentedScreen" not in text:
    text = text.replace(
        "  platform->video(std::static_pointer_cast<Core::Video::Screen>(shared_from_this()), output + viewX + viewY * width, width * sizeof(u32), viewWidth, viewHeight);\n",
        "  auto presentedScreen = std::static_pointer_cast<Core::Video::Screen>(shared_from_this());\n"
        "  mgb64OraclePresentedVideoDump(presentedScreen, output + viewX + viewY * width, width, viewWidth, viewHeight);\n"
        "  platform->video(presentedScreen, output + viewX + viewY * width, width * sizeof(u32), viewWidth, viewHeight);\n",
        1,
    )
screen_cpp.write_text(text, encoding="utf-8")

compilerconfig = src / "cmake/macos/compilerconfig.cmake"
if compilerconfig.exists():
    text = compilerconfig.read_text(encoding="utf-8")
    if "--show-sdk-version" not in text:
        needle = """  execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-platform-version
    OUTPUT_VARIABLE ares_macos_current_sdk
    RESULT_VARIABLE result
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
"""
        replacement = needle + """  if(NOT result EQUAL 0)
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-version
      OUTPUT_VARIABLE ares_macos_current_sdk
      RESULT_VARIABLE result
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()
"""
        text = text.replace(needle, replacement, 1)
        compilerconfig.write_text(text, encoding="utf-8")

macos_ui = src / "desktop-ui/cmake/os-macos.cmake"
if macos_ui.exists():
    text = macos_ui.read_text(encoding="utf-8")
    text = text.replace(
        "if(ACTOOL_PROGRAM)\n",
        'if(ACTOOL_PROGRAM AND NOT "$ENV{ARES_CLT_BUILD}" STREQUAL "1")\n',
        1,
    )
    macos_ui.write_text(text, encoding="utf-8")
PY

if [[ "$NO_BUILD" -eq 1 ]]; then
    echo "Prepared instrumented ares checkout: $SRC_DIR"
    exit 0
fi

ARES_CLT_BUILD=1 cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target desktop-ui --parallel "$JOBS"

if [[ -x "$BUILD_DIR/desktop-ui/ares.app/Contents/MacOS/ares" ]]; then
    ARES_BIN="$BUILD_DIR/desktop-ui/ares.app/Contents/MacOS/ares"
elif [[ -x "$BUILD_DIR/desktop-ui/ares" ]]; then
    ARES_BIN="$BUILD_DIR/desktop-ui/ares"
else
    echo "FAIL: built desktop-ui target, but could not find the ares executable" >&2
    exit 1
fi

echo ""
echo "PASS: instrumented ares binary:"
echo "  $ARES_BIN"
echo ""
echo "Run a ROM-vs-native movement capture with:"
echo "  tools/movement_oracle_capture.sh --ares-bin \"$ARES_BIN\" --rom baserom.u.z64"
