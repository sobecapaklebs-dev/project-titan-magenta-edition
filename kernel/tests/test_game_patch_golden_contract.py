#!/usr/bin/env python3
from pathlib import Path
import hashlib, sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]).resolve()

def text(rel): return (root / rel).read_text(encoding='utf-8', errors='strict')
def need(s, n, msg):
    if n not in s: raise AssertionError(msg)
def forbid(s, n, msg):
    if n in s: raise AssertionError(msg)

def tree_hash(rel):
    base = root / rel
    h = hashlib.sha256(); count = 0
    for p in sorted(base.rglob('*')):
        if not p.is_file(): continue
        rp = p.relative_to(root).as_posix().encode()
        data = p.read_bytes()
        h.update(len(rp).to_bytes(4,'little')); h.update(rp)
        h.update(len(data).to_bytes(8,'little')); h.update(data)
        count += 1
    return count, h.hexdigest()

EXPECTED = {
    'modules/titan-neutrino-engine': (397, '3fd982294cde8f68479820f4f94a36d9f02a8514e55f34a789327a6f5cd6e72e'),
    'modules/titan-ul-engine': (553, 'bf87637ee11dab4ab4b82ec6afa6ad92e5f2703bcf3ecaec8f7104ebab8e4453'),
}
for rel, expected in EXPECTED.items():
    got = tree_hash(rel)
    if got != expected:
        raise AssertionError(f'production engine source tree changed: {rel}: {got} != {expected}')

patch = text('kernel/src/services/titan_game_patches.c')
ph = text('kernel/src/services/titan_game_patches.h')
neu = text('kernel/src/services/titan_neutrino.c')
neh = text('kernel/src/services/titan_neutrino.h')
hand = text('kernel/src/services/titan_exec_handoff.c')
st2 = text('bootloader/stage2/src/main.c')
link = text('bootloader/stage2/linkfile')
mk = text('bootloader/stage2/Makefile')
shell = text('kernel/src/ui/titan_shell_scene.c')
loc = text('kernel/src/services/titan_localization.c')

need(patch, '#define TITAN_GAME_PATCH_CHEAT_ROOT "mass:/CHT"', 'CHT root missing')
need(patch, '#define TITAN_GAME_PATCH_ENHANCEMENT_ROOT "mass:/ENH"', 'ENH root missing')
need(patch, 'TitanGamePatches_PrepareUlGoldenRuntime', 'Golden UL staging missing')
need(patch, '$EnableCheat=1', 'UL native PS2RD config missing')
need(patch, 'TITAN_GAME_PATCH_PS2RD_MAX_HOOKS 5u', '5-hook bound missing')
need(patch, 'TITAN_GAME_PATCH_PS2RD_MAX_CODES 250u', '250-code bound missing')

for old in ('titan_cheat_bridge', 'neutrino-titan-patches', '-titan-patch='):
    forbid(neu, old, f'obsolete engine/bridge path remains: {old}')
need(neu, 'plan->arguments[plan->argument_count++] = g_neutrino_elf;', 'Golden Neutrino argv[0] missing')
need(neu, 'plan->arguments[plan->argument_count++] = "-bsd=usb";', 'Golden Neutrino USB contract missing')
need(neu, 'plan->arguments[plan->argument_count++] = "-logo";', 'Golden Neutrino logo contract missing')
need(neu, 'plan->arguments[plan->argument_count++] = g_opl_ul_loader;', 'Golden UL argv0 missing')
need(neu, 'plan->arguments[plan->argument_count++] = (char *)game->startup;', 'Golden UL argv1 missing')
need(neu, 'plan->arguments[plan->argument_count++] = "titan-ul";', 'Golden UL argv2 missing')
need(neu, 'TitanExecHandoff_RunWithPatch(', 'Neutrino target launch does not use stage2 patch sideband')
need(neh, 'TITAN_NEUTRINO_PATCH_PREPARE_FAILED', 'patch preparation result missing')

need(hand, 'TITAN_HANDOFF_PS2RD_MINIMUM_ADDRESS 0x00080000u', 'handoff PS2RD lower bound missing')
need(hand, 'TITAN_HANDOFF_PS2RD_MAXIMUM_ADDRESS 0x00084000u', 'handoff PS2RD upper bound missing')
need(hand, 'if (!copy_ps2rd)', 'normal handoff does not skip optional PS2RD segment')
need(hand, 'TitanExecHandoff_RunWithPatch', 'patch sideband handoff missing')
need(st2, 'TITAN_STAGE2_PATCH_PREFIX "--titan-internal-patch="', 'private stage2 marker missing')
need(st2, 'target_index = 1;', 'stage2 does not strip private marker')
need(st2, 'SetSyscall(__NR_SetupThread', 'PS2RD SetupThread install missing')
need(link, 'ps2rd : ORIGIN = 0x00080000, LENGTH = 16K', 'PS2RD linker region wrong')
need(link, 'ASSERT(__titan_ps2rd_end <= 0x00084000', 'PS2RD/ee_core collision assertion missing')
need(mk, 'src/titan_ps2rd_engine.o', 'resident PS2RD object not embedded in stage2')

need(shell, 'const char *label = "Titan Enhanced";', 'Titan Enhanced badge missing')
need(shell, 'g_scene.game_launch_enhanced = patch_state.enhancement_active ? 1 : 0;', 'badge not gated by applied enhancements')
need(shell, 'taunt = TitanLocalization_Compat(g_scene.game_launch_cheat_taunt);', 'taunt localization missing')
for phrase in (
    'Tá bom, campeão. Você venceu antes de começar.',
    'O Titan viu isso. O jogo também.',
    'Você quer jogar ou assistir o Titan jogar por você?',
    'Está bien, campeón. Ganaste antes de empezar.',
    'Titan vio eso. El juego también.',
    'Do you want to play, or should Titan play for you?',
): need(loc, phrase, f'localized roast missing: {phrase}')

print('game-patch-golden-contract: OK engines immutable, Golden argv preserved, kernel stage2 PS2RD, UL native CHT/CFG, UI+i18n')
