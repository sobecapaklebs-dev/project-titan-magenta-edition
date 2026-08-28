#!/usr/bin/env python3
from pathlib import Path
import hashlib
import struct
import sys
import wave

kernel = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = kernel.parent

def require(condition, message):
    if not condition:
        raise SystemExit(f"audio-contract: FAIL: {message}")

makefile = (kernel / "Makefile").read_text(encoding="utf-8")
audio_c = (kernel / "src/services/titan_audio.c").read_text(encoding="utf-8")
kernel_c = (kernel / "src/core/titan_kernel.c").read_text(encoding="utf-8")
scene_c = (kernel / "src/ui/titan_shell_scene.c").read_text(encoding="utf-8")
notifications_c = (kernel / "src/ui/titan_notifications.c").read_text(encoding="utf-8")
avatar_c = (kernel / "src/ui/titan_avatar_picker.c").read_text(encoding="utf-8")
text_input_c = (kernel / "src/ui/titan_text_input.c").read_text(encoding="utf-8")

for token in (
    "src/services/titan_audio.o",
    "$(PS2SDK)/iop/irx/audsrv.irx",
    "-laudsrv",
    "titan_audsrv_irx",
):
    require(token in makefile, f"Makefile missing {token}")

for token in (
    "TITAN_AUDIO_SAMPLE_RATE 32000u",
    "TITAN_AUDIO_MUSIC_RING_BYTES",
    "TITAN_AUDIO_MUSIC_READ_BUDGET 8192u",
    "TITAN_AUDIO_MAX_TOTAL_SFX_BYTES",
    "TITAN_AUDIO_WAV_FORMAT_IMA_ADPCM",
    "TITAN_AUDIO_MASTER_VOLUME MAX_VOLUME",
    "TitanAudio_ParseWav",
    "TitanAudio_DecodeImaBlock",
    "TitanAudio_DrainPending",
    "TitanAudio_StepClipLoad",
    "TITAN_AUDIO_SFX_LOAD_BUDGET",
    "TitanAudio_Shutdown",
    "audsrv_quit",
):
    require(token in audio_c, f"audio service missing {token}")

require("(void)TitanAudio_Initialize();" in kernel_c, "kernel does not initialize audio")
require("TitanAudio_Update();" in kernel_c, "main loop does not pump audio")
require("TITAN_AUDIO_SFX_NOTIFICATION" in notifications_c, "notification event not wired")
require("TITAN_AUDIO_SFX_PROFILE_ENTRY" in scene_c, "profile entry event not wired")
require("TITAN_AUDIO_SFX_HOME_GRID_NAV" in scene_c, "Home navigation event not wired")
require("TITAN_AUDIO_SFX_TOP_MENU_NAV" in scene_c, "top menu event not wired")
require("TITAN_AUDIO_SFX_THEME_TOP_ENTER" in scene_c, "theme top-enter event not wired")
require("TITAN_AUDIO_SFX_INTERFACE_MENU_NAV" in scene_c, "interface navigation missing")
require("TitanAudio_PlayShutdownAndWait" in scene_c, "shutdown event not wired")
require("TitanAudio_PrepareHandoff" in scene_c, "external handoff mute missing")
require("TITAN_AUDIO_SFX_INTERFACE_MENU_NAV" in avatar_c, "avatar navigation missing")
require("TITAN_AUDIO_SFX_INTERFACE_MENU_NAV" in text_input_c, "text input navigation missing")
require(scene_c.count("TitanAudio_StartMusic();") >= 2,
        "music rebind paths must remain wired after lifecycle gating")
require("TitanThemeLifecycle_MusicAllowed" in audio_c,
        "theme music must be gated by the BF30 lifecycle")
health_transition = """g_scene.startup_phase = TITAN_STARTUP_PHASE_HEALTH;
                g_scene.startup_phase_frames = 0u;
                TitanAudio_StartMusic();"""
require(health_transition in scene_c,
        "startup audio request path must remain wired; lifecycle gate decides whether HOME-only music may play")
require("TitanShellScene_LeavePackageInstallerExclusiveMode" in scene_c and
        "package_music_suspended" in scene_c,
        "package installer does not restore menu music after exclusive mode")
for token in (
    "g_scene.folder_ui_mode != TITAN_FOLDER_UI_CLOSED",
    "g_scene.folder_open",
    "g_scene.shell_move_active",
):
    require(token in scene_c, f"navigation audio routing missing {token}")
require("text_x = x + ((width - text_width) * 0.5f);" in scene_c,
        "DEX Title ID text is not centered in its tile")

assets = sorted((project / "runtime/TITAN/audio/sfx").glob("*.wav"))
require(len(assets) == 6, "expected six system SFX assets")
total = 0
for path in assets:
    with wave.open(str(path), "rb") as wav:
        require(wav.getnchannels() == 2, f"{path.name}: expected stereo")
        require(wav.getsampwidth() == 2, f"{path.name}: expected 16-bit PCM")
        require(wav.getframerate() == 32000, f"{path.name}: expected 32000 Hz")
        require(wav.getnframes() > 0, f"{path.name}: empty audio")
        total += wav.getnframes() * wav.getnchannels() * wav.getsampwidth()
require(total <= 1024 * 1024, "bundled SFX exceed runtime memory budget")

music_path = project / "runtime/TITAN/audio/magenta_melody.wav"
if music_path.is_file():
    music = music_path.read_bytes()
    require(len(music) == 113029180, "background music size changed")
    require(hashlib.sha256(music).hexdigest() ==
            "047f1bf49c4d3355185bb6579b9eb029f545997a08f9f3648a68d988e6967ea0",
            "background music hash changed")
    require(music[:4] == b"RIFF" and music[8:12] == b"WAVE",
            "background music is not RIFF/WAVE")

    chunks = {}
    position = 12
    riff_end = struct.unpack_from("<I", music, 4)[0] + 8
    require(riff_end == len(music), "background music RIFF size mismatch")
    while position + 8 <= riff_end:
        tag = music[position:position + 4]
        size = struct.unpack_from("<I", music, position + 4)[0]
        chunk_start = position + 8
        chunk_end = chunk_start + size
        require(chunk_end <= riff_end, "background music chunk exceeds RIFF")
        chunks.setdefault(tag, music[chunk_start:chunk_end])
        position = chunk_end + (size & 1)

    fmt = chunks.get(b"fmt ", b"")
    fact = chunks.get(b"fact", b"")
    data = chunks.get(b"data", b"")
    require(len(fmt) >= 20 and len(fact) >= 4 and data,
            "background music chunks are incomplete")
    format_tag, channels, sample_rate, byte_rate, block_align, bits = \
        struct.unpack_from("<HHIIHH", fmt, 0)
    extension_size, samples_per_block = struct.unpack_from("<HH", fmt, 16)
    sample_frames = struct.unpack_from("<I", fact, 0)[0]
    require(format_tag == 0x0011, "background music is not IMA-ADPCM")
    require(channels == 2 and sample_rate == 32000 and bits == 4,
            "background music format changed")
    require(byte_rate > 0 and block_align == 4096 and extension_size >= 2 and
            samples_per_block == 4089, "background music block geometry changed")
    require(sample_frames == 112834137,
            "background music duration/start offset changed")
    require(len(data) % block_align == 0,
            "background music ends on a partial ADPCM block")
else:
    require((project / "runtime/TITAN/audio/MAGENTA_MELODY_NOT_REDISTRIBUTED.md").is_file(),
            "background music absent without public-source redistribution notice")

require((project / "docs/ASSET_PROVENANCE.md").is_file(), "asset provenance missing")

print("magenta-audio-contract: OK")
