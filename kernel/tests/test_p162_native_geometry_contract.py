#!/usr/bin/env python3
from __future__ import annotations
import hashlib, math, random, re, struct, sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]).resolve()

def text(rel: str) -> str:
    p=ROOT/rel
    if not p.is_file(): raise SystemExit(f"P162_CONTRACT=FAIL missing {rel}")
    return p.read_text(errors='replace')

def sha(rel: str) -> str:
    return hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()

def need(cond: bool, msg: str):
    if not cond: raise SystemExit("P162_CONTRACT=FAIL " + msg)

def f32(x: float) -> float:
    return struct.unpack('<f', struct.pack('<f', float(x)))[0]

def add(a,b): return f32(f32(a)+f32(b))
def sub(a,b): return f32(f32(a)-f32(b))
def mul(a,b): return f32(f32(a)*f32(b))
def div(a,b): return f32(f32(a)/f32(b))

def old_xy_fixed(coord: float) -> int:
    coord=f32(coord)
    if not math.isfinite(coord): coord=0.0
    fixed=f32(mul(add(coord,2048.0),16.0))
    if fixed <= 0.0: return 0
    if fixed >= 65535.0: return 65535
    return int(f32(add(fixed,0.5)))

def ee_map(vx,vy,vw,vh,x,y,z,w):
    x,y,z,w=map(f32,(x,y,z,w))
    if not all(map(math.isfinite,(x,y,z,w))) or w < f32(0.0001) or vw==0 or vh==0: return None
    q=div(1.0,w)
    nx,ny=mul(x,q),mul(y,q)
    if not all(map(math.isfinite,(nx,ny))) or any((a < -2.0 or a > 2.0) for a in (nx,ny)): return None
    clamped_z=f32(max(-w,min(w,z)))
    nz=mul(clamped_z,q)
    if not math.isfinite(nz): return None
    sx=add(float(vx), mul(add(mul(nx,0.5),0.5),float(vw)))
    sy=add(float(vy), mul(sub(0.5,mul(ny,0.5)),float(vh)))
    depth=mul(sub(1.0,nz),0.5)
    depth=f32(max(0.0,min(1.0,depth)))
    zi=int(f32(add(mul(depth,65535.0),0.5)))
    return old_xy_fixed(sx),old_xy_fixed(sy),zi,q

def vu_map(vx,vy,vw,vh,x,y,z,w):
    # Model the actual VU sequence: DIV Q; MULq; z clamp; MUL viewport scale; ADD offset;
    # add half-LSB; FTOI4/FTOI0 (truncation for the nonnegative eligible domain).
    x,y,z,w=map(f32,(x,y,z,w))
    if not all(map(math.isfinite,(x,y,z,w))) or w < f32(0.0001): return None
    q=div(1.0,w)
    nx,ny=mul(x,q),mul(y,q)
    if not all(map(math.isfinite,(nx,ny))) or any((a < -2.0 or a > 2.0) for a in (nx,ny)): return None
    clamped_z=f32(max(-w,min(w,z)))
    nz=mul(clamped_z,q)
    if not math.isfinite(nz): return None
    halfw=f32(float(vw)*0.5); halfh=f32(float(vh)*0.5)
    gx=add(mul(nx,halfw), add(float(vx)+halfw,2048.0))
    gy=add(mul(ny,f32(-halfh)), add(float(vy)+halfh,2048.0))
    gz=add(mul(nz,-32767.5),32767.5)
    gx=add(gx,0.03125); gy=add(gy,0.03125); gz=add(gz,0.5)
    # ftoi4 = *16 then truncate; ftoi0 = truncate. StateEligible guarantees range.
    return int(f32(mul(gx,16.0))),int(f32(mul(gy,16.0))),int(gz),q

# Frozen hardware-PASS room lifecycle must not be touched by this pass.
need(sha('ports/goldeneye/mgb64/src/game/bg.c') == '4c1aecd9ac124f4549aa4a79fcd4e0966db2a02a955009bfdfc528f4770b7f69', 'P161-C bg.c changed')
need(sha('ports/goldeneye/mgb64/src/game/lvl.c') == 'c0aef76255460a5e8f57a06cfa5c02be326a1b78473677754db19bdfcc93b0e5', 'P161-C lvl.c changed')

km=text('kernel/Makefile'); pm=text('ports/goldeneye/Makefile.titan')
vu0=text('ports/goldeneye/mgb64/src/platform/fast3d/gfx_ps2_vu0.c')
pc=text('ports/goldeneye/mgb64/src/platform/fast3d/gfx_pc.c')
gt=text('ports/goldeneye/mgb64/src/platform/fast3d/gfx_titan.c')
vc=text('kernel/src/graphics/titan_game_vu1.c')
vh=text('kernel/src/graphics/titan_game_vu1.h')
vsm=text('kernel/src/graphics/vu1/titan_game_vu1.vsm')
video=text('kernel/src/graphics/titan_video.c')
mathh=text('kernel/src/graphics/titan_video_game_math.h')
extract=text('kernel/tools/extract_vu_micro.py')
texpool=text('kernel/src/graphics/titan_video_game_texture.c')
runtime=text('kernel/src/services/titan_game_runtime.c')
app_runtime=text('kernel/src/services/titan_app_runtime.c')

# Build architecture / optimization policy.
need('gfx_ps2_vu0.c' in pm, 'VU0 source absent from TitanEye build')
need('TITANEYE_FAST3D_NATIVE_CFLAGS' in pm and '-O2' in pm, 'localized O2 missing')
active_make_lines = [line.split('#',1)[0] for line in pm.splitlines()]
active_make = '\n'.join(active_make_lines)
need('-ffast-math' not in active_make and '-Ofast' not in active_make, 'unsafe fast math enabled')
need(re.search(r'gfx_pc\.o[^\n]*: CFLAGS := \$\(TITANEYE_FAST3D_NATIVE_CFLAGS\)', pm) is not None, 'gfx_pc.o is not localized O2')
need(re.search(r'gfx_ps2_vu0\.o[^\n]*: CFLAGS := \$\(TITANEYE_FAST3D_NATIVE_CFLAGS\)', pm) is not None, 'gfx_ps2_vu0.o is not localized O2')
need('src/graphics/titan_game_vu1.o' in km, 'VU1 C object absent from kernel link')
need('src/graphics/titan_game_vu1.o: $(TITAN_GAME_VU1_HEADER)' in km, 'VU1 object lacks generated-header dependency')
need('--max-size 14336' in km, 'VU1 micro build does not reserve address-256 program memory bound')
need('extract_vu_micro.py' in km and 'titan_game_vu1_micro_start titan_game_vu1_micro_end' in km, 'symbol-delimited DVP extraction missing')
# Game micro must not be produced by blind objcopy.
game_rule=km[km.index('$(TITAN_GAME_VU1_BINARY):'):km.index('$(TITAN_GAME_VU1_HEADER):')]
need('DVP_OBJCOPY' not in game_rule, 'game micro uses blind DVP_OBJCOPY')
need('bad=[r for r in elf.relocs()' in extract and 'if len(code)%8' in extract and 'atomic_write' in extract and 'pending relocation inside microprogram' in extract, 'fail-closed VU extractor incomplete')

# VU0 work really exists and host fallback remains.
for tok in ('lqc2','vmulaw','vmaddax','vmadday','vmaddz','sqc2','gfx_ps2_vu0_transform_i16_batch','gfx_ps2_vu0_dot3_i8_batch','gfx_ps2_vu0_lerp4'):
    need(tok in vu0, f'VU0 token missing: {tok}')
need('#if defined(__EE__) || defined(_EE)' in vu0, 'VU0 target branch guard missing')
# Preserve historical Fast3D float operation order: raw dot3 first, one 1/127
# multiply only after the completed dot.  Pre-dividing coefficients changes
# cancellation/rounding and is therefore not an equivalent implementation.
dot=vu0[vu0.index('int gfx_ps2_vu0_dot3_i8_batch'):vu0.index('void gfx_ps2_vu0_lerp4')]
for axis in range(3):
    need(f's_vu0_matrix[{axis}].v[lane] = coeff[lane][{axis}];' in dot, f'VU0 dot raw coefficient row {axis} missing')
need('coeff[lane][0] * (1.0f / 127.0f)' not in dot and
     'coeff[lane][1] * (1.0f / 127.0f)' not in dot and
     'coeff[lane][2] * (1.0f / 127.0f)' not in dot,
     'VU0 dot pre-divides coefficients instead of scaling completed dot')
asm_dot=dot[dot.index('#if GFX_PS2_EE'):dot.index('#else')]
need(asm_dot.index('vmaddz') < asm_dot.index('vmul.xyzw') < asm_dot.index('sqc2'),
     'VU0 target dot must complete raw dot3 before 1/127 vector multiply/store')
host_dot=dot[dot.index('#else'):dot.index('#endif', dot.index('#else'))]
need(host_dot.index('gfx_ps2_scalar_transform') < host_dot.index('*= (1.0f / 127.0f)'),
     'VU0 host fallback does not mirror raw-dot then 1/127 order')
inv=vu0.index('s_vu0_inv127')
need(vu0.rfind('#if GFX_PS2_EE', 0, inv) > vu0.rfind('#endif', 0, inv),
     's_vu0_inv127 must remain target-only')
transform=vu0[vu0.index('int gfx_ps2_vu0_transform_i16_batch'):vu0.index('int gfx_ps2_vu0_dot3_i8_batch')]
need('(uintptr_t)src' in transform and 'xyz_stride &' in transform and 'sizeof(int16_t)' in transform,
     'VU0 i16 source alignment guard missing')

# Fast3D hardware use and holes/perf instrumentation.
need('gfx_ps2_vu0_transform_i16_batch' in pc, 'G_VTX does not call VU0 batch transform')
need('gfx_ps2_vu0_dot3_i8_batch' in pc, 'G_VTX lighting/texgen does not use VU0 dot batch')
need('gfx_ps2_vu0_lerp4' in pc, 'clip intersection does not call VU0')
need('g_depth_clamp_enabled = true;' in pc and 'g_gpu_can_clamp_depth = true;' in pc, 'Titan depth clamp capability missing')
need('gfx_loaded_vertex_snap_to_clip_plane' in pc, 'canonical clip-plane snapping missing')
need('gfx_tri_projected_cross_homogeneous' in pc, 'homogeneous cull fast path missing')
need('float n = q - p;' in pc, 'P162.4 homogeneous fast-cull lost GoldenEye historical winding sign')
for tok in ('GFX_CLIP_W_SAFE','GFX_CLIP_GB_LEFT','GFX_CLIP_GB_RIGHT','GFX_CLIP_GB_BOTTOM','GFX_CLIP_GB_TOP','gfx_clip_triangle_to_wsafe_and_guardband','gfx_clip_triangle_to_guardband'):
    need(tok in pc, f'Titan W-safe/guard-band clip policy missing: {tok}')
need(pc.count('common_clip_rej &= (uint8_t)~0x30u;') >= 1 and pc.count('p162_common_rej &= (uint8_t)~0x30u;') >= 1, 'depth-clamp still allows near/far trivial reject')
need('#ifndef PROJECT_TITAN_TGR\n        return;\n#endif' in pc, 'Titan shard workaround reject guard missing')
need('s_room_range_index_generation = UINT32_MAX;' in pc and 's_room_range_index_count = 0;' in pc, 'room-range clear invalidation missing')
for tok in ('P162_HW','vu0_batches=','vu0_light_vtx=','gs_side=','gs_depth=','ndc_div_saved=','tri_commonrej=','tri_cullfast_rej=','tri_cullexact_rej=','tri_clipaway=','tri_emit=','tex_dynhash=','tex_hashbytes=','tex_live=','tex_dynlive=','chr_clipaway=','chr_cullrej=','chr_emit='):
    need(tok in pc, f'P162 telemetry missing: {tok}')

# Producer must no longer precompute reciprocal W for the native path.
need('const float reciprocal_w = 0.0f;' in gt, 'TitanEye still precomputes reciprocal W')

# VU1 semantics and safety.
for tok in ('div Q','mulq.xyzw','ftoi4.xy','ftoi0.z','xgkick'):
    need(tok in vsm, f'VU1 micro op missing: {tok}')
for tok in ('TITAN_GAME_VU1_ADDR_CLIP 16u','TITAN_GAME_VU1_ADDR_ST 112u','TITAN_GAME_VU1_ADDR_RGBA 208u','TITAN_GAME_VU1_ADDR_PATH1 320u','TITAN_GAME_VU1_ADDR_OUTPUT 329u'):
    need(tok in vc, f'VU1 data map changed: {tok}')
need('TITAN_GAME_VU1_RESULT_NOT_ELIGIBLE (-2)' in vh, 'NOT_ELIGIBLE result missing')
need('TITAN_GAME_VU1_RESULT_SUBMIT_FAILED (-3)' in vh, 'SUBMIT_FAILED result missing')
need('TITAN_GAME_VU1_MICRO_ADDRESS 256u' in vc, 'VU1 micro base address changed')
need('TITAN_GAME_VU1_MICRO_INSTRUCTION_CAPACITY 2048u' in vc, 'VU1 micro capacity gate missing')
need('titan_game_vu1_micro_size == 0u' in vc and '(titan_game_vu1_micro_size & 7u) != 0u' in vc, 'VU1 micro alignment gate missing')
need('TitanGameVu1_StateEligible' in vc and 'TitanGameVu1_TextureEligible' in vc and 'TitanGameVu1_BatchEligible' in vc, 'VU1 eligibility gates missing')
need('texture->buffer_width != expected_buffer_width' in vc and 'TITAN_VIDEO_GAME_TEXTURE_CLUT_BYTES' in vc, 'VU1 malformed-texture fallback missing')
need('state->combine_mode == TITAN_GAME_COMBINE_TEX0_TEX1_LOD' in vc, 'LOD defense-in-depth missing')
need('state->combine_mode != TITAN_GAME_COMBINE_TEX0_TEX1_LOD' in video, 'caller can route LOD to VU1')
need('native_drawn == TITAN_GAME_VU1_RESULT_SUBMIT_FAILED' in video, 'partial VIF failure replay protection missing')
need('int faulted;' in vc and 'g_game_vu1.faulted = 1;' in vc and 'TitanGameVu1_HasFault' in vc and 'TitanGameVu1_HasFault' in vh,
     'persistent VIF1 fault latch/API missing')
need('TITAN_GAME_VU1_VIF_IDLE_MASK 0x1F00000Fu' in vc,
     'VIF1 idle proof must include VPS/VEW/VGW and FQC')
need('TITAN_GAME_VU1_GIF_PATH_IDLE_MASK 0x00000F00u' in vc and
     '0x10003020u' in vc and '0x10003C10u' in vc,
     'GIF/PATH1 idle proof or VIF1 FBRST register missing')
recover=vc[vc.index('static int TitanGameVu1_FaultHardwareIdle'):vc.index('int TitanGameVu1_Initialize')]
need(recover.index('dma_channel_wait') < recover.index('VIF1_STAT') <
     recover.index('TITAN_GAME_VU1_GIF_STAT_REG') < recover.index('TITAN_GAME_VU1_VIF1_FBRST_REG = 1u'),
     'VIF1 recovery must prove DMAC -> VIF -> GIF/PATH1 idle before FBRST')
mark=vc[vc.index('static void TitanGameVu1_MarkFailure'):vc.index('static int TitanGameVu1_FaultHardwareIdle')]
need(mark.index('g_game_vu1.available = 0;') < mark.index('g_game_vu1.faulted = 1;'),
     'VU1 failure does not latch fault after disabling native backend')
video_native=video[video.index('native_drawn == TITAN_GAME_VU1_RESULT_SUBMIT_FAILED'):video.index('/* NOT_ELIGIBLE', video.index('native_drawn == TITAN_GAME_VU1_RESULT_SUBMIT_FAILED'))]
need('g_video.packet_error = 1;' in video_native,
     'VIF1 submit fault is not frame-fatal before later PATH3 work')
begin_frame=video[video.index('TitanResult TitanVideo_BeginFrame(void)'):video.index('TitanResult TitanVideo_EndFrame(void)')]
need(begin_frame.index('TitanGameVu1_HasFault()') < begin_frame.index('g_video.packet_error = 0;'),
     'BeginFrame can erase packet state before checking persistent VIF1 fault')
end_game=video[video.index('TitanResult TitanVideo_EndGameMode(void)'):video.index('int TitanVideo_IsGameMode(void)')]
need('if (!TitanGameVu1_Reset())' in end_game and
     end_game.index('if (!TitanGameVu1_Reset())') < end_game.index('TitanWaveVu1_RebindVideoResources'),
     'waves can reclaim VIF1 before safe game-owner release')
end_frame=video[video.index('static TitanResult TitanVideo_AbortOpenFrame'):]
need('g_video.frame_open = 0;' in end_frame and
     'return TitanVideo_AbortOpenFrame(TITAN_ERROR_FRAME_END);' in end_frame and
     end_frame.count('return TitanVideo_AbortOpenFrame(result);') >= 2,
     'EndFrame error paths do not coherently close logical frame')
abort_frame=end_frame[:end_frame.index('TitanResult TitanVideo_EndFrame(void)')]
need('TitanVideoGameTexturePool_ReclaimRetiredCount' not in abort_frame,
     'aborted/unknown PATH1 frame reclaims retired textures')
need(re.search(r'u64\s+tex0\s*=',vc) is not None and re.search(r'u64\s+clamp\s*;',vc) is not None, 'GS 64-bit register state narrowed')
need('Silently filtering one triangle' in vc, 'whole-batch fallback invariant missing')
need('TitanGameVu1Path1Fits' in vc, 'PATH1/output VU-memory non-overlap proof missing')
# packet2 has no implicit write bound: prove the exact local-chain budget at
# compile time for both draw and MPG upload packets.
for tok in ('TITAN_GAME_VU1_DRAW_UNPACK_QWORDS 5u',
            'TITAN_GAME_VU1_DRAW_LAUNCH_QWORDS 2u',
            'TITAN_GAME_VU1_DRAW_END_QWORDS 1u',
            'TitanGameVu1DrawPacketFits', 'TitanGameVu1UploadPacketFits',
            'TITAN_GAME_VU1_UPLOAD_MAX_MPG_BLOCKS'):
    need(tok in vc, f'VU1 packet capacity proof missing: {tok}')
need('TITAN_GAME_VU1_DRAW_REQUIRED_QWORDS <=' in vc and
     'TITAN_GAME_VU1_UPLOAD_REQUIRED_QWORDS <=' in vc,
     'VU1 packet capacity compile-time inequalities missing')
need('vertex->z >= -guard' not in vc and 'float clip_z = source->z;' in vc and 'if (clip_z < -source->w) clip_z = -source->w;' in vc, 'VU1 still rejects depth instead of clamping it')
need('nx < -2.0005f || nx > 2.0005f' in mathh and
     'ny < -2.0005f || ny > 2.0005f' in mathh,
     'EE XY guard-band tolerance changed unexpectedly')
for clamp in ('if (nx < -2.0f) nx = -2.0f;',
              'if (nx > 2.0f) nx = 2.0f;',
              'if (ny < -2.0f) ny = -2.0f;',
              'if (ny > 2.0f) ny = 2.0f;'):
    need(clamp in mathh, 'EE XY guard-band clamp missing: ' + clamp)
need('nz < -2.0f' not in mathh and 'clamped_z = z;' in mathh and 'if (clamped_z < -w) clamped_z = -w;' in mathh, 'EE depth-clamp still has +/-2 Z reject')
need('w < 0.0001f' in mathh and 'w <= 0.0001f' not in mathh, 'EE rejects the exact W_SAFE boundary')
need('vertex->w < 0.0001f' in vc and 'vertex->w <= 0.0001f' not in vc, 'VU1 rejects the exact W_SAFE boundary')
launch=vc[vc.index('static void TitanGameVu1_AddStartProgram'):vc.index('static int TitanGameVu1_SendPacket')]
need(launch.index('packet2_vif_flush') < launch.index('packet2_vif_mscal') < launch.index('packet2_vif_flushe'), 'VU1 PATH1 fence/launch order invalid')
path1_state=vc[vc.index('static void TitanGameVu1_PreparePath1State'):vc.index('static void TitanGameVu1_AddStartProgram')]
need(path1_state.index('GIF_REG_ST') < path1_state.index('GIF_REG_RGBAQ') < path1_state.index('GIF_REG_XYZ2'),
     'VU1 packed vertex register order must remain ST -> RGBAQ -> XYZ2 for Q capture')
draw=vc[vc.index('int TitanGameVu1_DrawMaterialPass('):vc.index('#else', vc.index('int TitanGameVu1_DrawMaterialPass('))]
need(draw.rfind('packet2_utils_vu_add_unpack_data') < draw.index('TitanGameVu1_AddStartProgram'), 'VU1 launch occurs before final UNPACK')
# The scaling constant is invariant for the complete dot batch; load it once
# before the loop, then perform exactly one vector scale per completed dot3.
need(asm_dot.index('lqc2      $vf7') < asm_dot.index('1:') < asm_dot.index('vmul.xyzw'),
     'VU0 1/127 vector constant is reloaded inside the dot loop')

# PATH1 must commit sampler accounting only after a successful native submit.
native_call=video[video.index('native_drawn = TitanGameVu1_DrawMaterialPass'):video.index('/* NOT_ELIGIBLE', video.index('native_drawn = TitanGameVu1_DrawMaterialPass'))]
need(native_call.index('TitanGameVu1_DrawMaterialPass') < native_call.index('if (native_drawn >= 0)') < native_call.index('TitanPikaVr_RecordSamplerState'),
     'native sampler tracking commits before successful PATH1 acceptance')
failed=native_call[native_call.index('native_drawn == TITAN_GAME_VU1_RESULT_SUBMIT_FAILED'):]
need('TitanPikaVr_RecordSamplerState' not in failed,
     'failed PATH1 submission mutates sampler tracking')

# Field-by-field GS material parity between native PATH1 and EE/PATH3.
ee_state=video[video.index('static int TitanVideo_GameAppendMaterialState'):video.index('static void TitanVideo_GameApplyFog')]
for reg in ('GS_REG_SCISSOR_1','GS_REG_TEST_1','GS_REG_ZBUF_1','GS_REG_ALPHA_1','GS_REG_CLAMP_1'):
    need(reg in path1_state and reg in ee_state, f'PATH1/PATH3 common GS state diverged: {reg}')
for expr in ('state->depth_compare == TITAN_GAME_DEPTH_GREATER ? 3u',
             'state->depth_compare == TITAN_GAME_DEPTH_GEQUAL ? 2u : 1u',
             'state->alpha_compare ? 5u : 1u',
             'GS_SET_ALPHA(0u, 1u, 0u, 1u, 0u)',
             'GS_SET_ALPHA(0u, 2u, 0u, 1u, 0u)',
             'GS_SET_ALPHA(0u, 2u, 1u, 2u, 0u)',
             '(!state->depth_write || second_pass) ? 1u : 0u'):
    need(expr in path1_state and expr in ee_state, f'PATH1/PATH3 state formula diverged: {expr}')
need('return TitanVideoGame_ToGsAlpha(alpha);' in video and
     'TitanVideoGame_ToGsAlpha(state->alpha_threshold)' in path1_state and
     'TitanVideo_ToGsAlpha(state->alpha_threshold)' in ee_state,
     'PATH1/PATH3 alpha conversion source differs')
need('mode == TITAN_GAME_WRAP_CLAMP ? WRAP_CLAMP : WRAP_REPEAT' in vc and
     'if (mode == TITAN_GAME_WRAP_CLAMP) return WRAP_CLAMP;' in video and
     'return WRAP_REPEAT;' in video,
     'PATH1/PATH3 sampler wrap normalization differs')
for expr in ('texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM32',
             'texture->format == TITAN_VIDEO_GAME_TEXTURE_FORMAT_PSM8_CLUT'):
    need(expr in vc and expr in video, f'PATH1/PATH3 texture PSM mapping differs: {expr}')
for expr in ('LOD_FORMULAIC','LOD_MAG_LINEAR','LOD_MAG_NEAREST','LOD_MIN_LINEAR','LOD_MIN_NEAREST','LOD_MIPMAP_REGISTER'):
    need(expr in path1_state and expr in ee_state, f'PATH1/PATH3 TEX1 policy differs: {expr}')
need('TEXTURE_FUNCTION_DECAL : TEXTURE_FUNCTION_MODULATE' in path1_state and
     'TEXTURE_FUNCTION_DECAL : TEXTURE_FUNCTION_MODULATE' in ee_state,
     'PATH1/PATH3 TEX0 combine function differs')
ee_batch=video[video.index('static int TitanVideo_GameAppendMaterialBatchPass'):video.index('static int TitanVideo_GameNativeQCompatible')]
need('GIF_REG_ST' in ee_batch and 'GIF_REG_RGBAQ' in ee_batch and 'GIF_REG_XYZ2' in ee_batch and
     ee_batch.index('GIF_REG_ST') < ee_batch.index('GIF_REG_RGBAQ') < ee_batch.index('GIF_REG_XYZ2'),
     'EE textured packet lost ST -> RGBAQ -> XYZ2 order')
need('3u, 1u, textured ? 1u : 0u, 0u, abe,' in ee_batch and
     'texture != 0 ? 1u : 0u,' in path1_state and
     'second_pass || state->blend_mode != TITAN_GAME_BLEND_OPAQUE ? 1u : 0u' in path1_state,
     'PATH1/PATH3 primitive topology/shading/texture/blend enable differs')

# Texture backing must remain non-reusable while a frame can still reference it.
need('texture->state != TITAN_VIDEO_GAME_TEXTURE_LIVE' in texpool and
     'texture->generation != generation' in texpool,
     'texture lookup can alias retired/stale generations')
release=texpool[texpool.index('int TitanVideoGameTexturePool_Release'):texpool.index('uint32_t TitanVideoGameTexturePool_ReclaimRetiredCount')]
need('if (defer_reuse)' in release and 'TITAN_VIDEO_GAME_TEXTURE_RETIRED' in release,
     'in-frame texture release does not retire backing')
video_release=video[video.index('int TitanVideo_GameReleaseTexture'):video.index('TitanResult TitanVideo_BeginFrame(void)')]
need('defer_reuse = g_video.frame_open != 0;' in video_release,
     'game texture release does not defer reuse for an open frame')
pressure=video[video.index('static TitanResult TitanVideo_GameAllocateTexture'):video.index('static TitanResult TitanVideo_GameSubmitTextureUpload')]
need(pressure.index('TitanVideo_SubmitPacket(1)') < pressure.index('TitanVideoGameTexturePool_ReclaimRetiredCount'),
     'texture pressure reclaims backing before a GS FINISH fence')
end_success=video[video.index('TitanResult TitanVideo_EndFrame(void)'):]
need(end_success.index('TitanVideo_SubmitPacket(1)') < end_success.index('TitanVideoGameTexturePool_ReclaimRetiredCount'),
     'successful EndFrame reclaims retired textures before FINISH')

# Runtime/app teardown must preserve ownership when safe VIF1 release fails.
rt_end=runtime[runtime.index('int TitanGameRuntime_End(void)'):runtime.index('int TitanGameRuntime_IsActive(void)')]
need(rt_end.index('TitanVideo_EndGameMode() != TITAN_RESULT_OK') <
     rt_end.index('TitanStorageScheduler_SetTitanGameExclusiveMode(0)') <
     rt_end.index('TitanAudio_StartMusic()'),
     'runtime exposes shell/storage/audio before video/VIF ownership release')
need('return 0;' in rt_end[rt_end.index('TitanVideo_EndGameMode() != TITAN_RESULT_OK'):rt_end.index('TitanGameApi_AudioEnd()')],
     'runtime cannot preserve session for a failed ownership-release retry')
app_close=app_runtime[app_runtime.index('void TitanAppRuntime_Close(void)'):app_runtime.index('void TitanAppRuntime_QuiesceForSystemShutdown(void)')]
need('TitanGameRuntime_IsActive() && !TitanGameRuntime_End()' in app_close and
     app_close.index('TitanGameRuntime_IsActive() && !TitanGameRuntime_End()') < app_close.index('TITAN_APP_LIFECYCLE_DESTROY'),
     'app runtime destroys session instead of retrying faulted game teardown')
need('g_app_runtime.lifecycle != TITAN_APP_LIFECYCLE_QUIESCE' in app_close,
     'faulted teardown retry can re-run module shutdown side effects')
need('teardown_pending' in runtime and '*api = 0;' in runtime and
     'g_game.teardown_pending = 1;' in runtime and
     'return g_game.active || g_game.teardown_pending;' in runtime,
     'failed Begin cannot preserve unpublished VIF/video ownership for retry')

# Numeric equivalence: established eligible viewport/state domain.
rng=random.Random(0x1622026)
checked=0
max_xy=0; max_z=0; max_q_rel=0.0
for _ in range(220000):
    vw=rng.choice((320,512,640,704,720,960,1280))
    vh=rng.choice((224,240,448,480,540,576,720))
    vx=rng.randint(0,max(0,2048-vw)) if vw <= 2048 else 0
    vy=rng.randint(0,max(0,2048-vh)) if vh <= 2048 else 0
    # Mirror StateEligible complete +/-2 guard constraint.
    if vx-0.5*vw < -2048 or vy-0.5*vh < -2048 or vx+1.5*vw > 2047.9375 or vy+1.5*vh > 2047.9375:
        continue
    w=f32(10**rng.uniform(-3.5,3.5))
    nx=f32(rng.uniform(-1.999,1.999)); ny=f32(rng.uniform(-1.999,1.999)); nz=f32(rng.uniform(-100000.0,100000.0))
    x=mul(nx,w); y=mul(ny,w); z=mul(nz,w)
    a=ee_map(vx,vy,vw,vh,x,y,z,w); b=vu_map(vx,vy,vw,vh,x,y,z,w)
    if a is None or b is None: continue
    dx=abs(a[0]-b[0]); dy=abs(a[1]-b[1]); dz=abs(a[2]-b[2])
    max_xy=max(max_xy,dx,dy); max_z=max(max_z,dz)
    if dx>1 or dy>1 or dz>1:
        raise SystemExit(f'P162_CONTRACT=FAIL EE/VU map mismatch {a} vs {b} state={(vx,vy,vw,vh)} clip={(x,y,z,w)}')
    if a[3] != 0:
        max_q_rel=max(max_q_rel,abs(a[3]-b[3])/abs(a[3]))
    checked+=1
need(checked > 50000, f'insufficient map equivalence samples {checked}')
# W_SAFE emits exactly epsilon; both backends must accept that boundary.
w_safe=f32(0.0001)
for zscale in (-1000.0,-1.0,0.0,1.0,1000.0):
    a=ee_map(0,0,640,480,0.0,0.0,mul(f32(zscale),w_safe),w_safe)
    b=vu_map(0,0,640,480,0.0,0.0,mul(f32(zscale),w_safe),w_safe)
    need(a is not None and b is not None and a[:3]==b[:3], f'W_SAFE boundary rejected/diverged zscale={zscale}: {a} {b}')

# STQ identity from producer through VU: normalized*(logical/POT)*Q.
for _ in range(50000):
    logical=rng.randint(1,2048); norm=f32(rng.uniform(-8,8)); w=f32(10**rng.uniform(-3,3)); q=div(1,w)
    pot=1
    while pot<logical: pot <<= 1
    ee=f32(mul(mul(norm,f32(logical/pot)),q))
    vu=f32(mul(mul(norm,f32(logical/pot)),q))
    if ee != vu: raise SystemExit('P162_CONTRACT=FAIL STQ model diverged')

# P162.4: homogeneous cull must match GoldenEye's HISTORICAL winding, not
# the conventional orientation. The legacy path uses (v0-v1) x (v2-v1);
# that is the opposite sign of (v1-v0) x (v2-v0). For positive W the
# q-p homogeneous numerator has the same sign as that historical cross.
cull_checked=0
for _ in range(320000):
    vs=[]
    for _j in range(3):
        w=f32(10**rng.uniform(-2,2)); x=mul(f32(rng.uniform(-2,2)),w); y=mul(f32(rng.uniform(-2,2)),w); vs.append((x,y,w))
    (x0,y0,w0),(x1,y1,w1),(x2,y2,w2)=vs
    hx0=div(x0,w0); hy0=div(y0,w0); hx1=div(x1,w1); hy1=div(y1,w1); hx2=div(x2,w2); hy2=div(y2,w2)
    historical=f32(sub(mul(sub(hx0,hx1),sub(hy2,hy1)), mul(sub(hy0,hy1),sub(hx2,hx1))))
    a=sub(mul(x1,w0),mul(x0,w1)); b=sub(mul(y2,w0),mul(y0,w2)); c=sub(mul(y1,w0),mul(y0,w1)); d=sub(mul(x2,w0),mul(x0,w2))
    p=mul(a,b); q=mul(c,d); n=sub(q,p); scale=f32(abs(p)+abs(q)+1.0)
    if abs(n) <= scale*1.0e-5 or abs(historical) < 1e-7: continue
    if (historical>0)!=(n>0):
        raise SystemExit(f'P162_CONTRACT=FAIL historical homogeneous cull sign mismatch hist={historical} n={n}')
    cull_checked+=1
need(cull_checked > 100000, f'insufficient cull samples {cull_checked}')

# Canonical Titan clip edge: W_SAFE first, then the +/-2W GS guard-band.
def interp(a,b,t): return [f32(add(a[i],mul(sub(b[i],a[i]),t))) for i in range(4)]
def dist(v,plane):
    x,y,z,w=v
    if plane==0:return f32(w)
    if plane==1:return f32(x+2.0*w)
    if plane==2:return f32(-x+2.0*w)
    if plane==3:return f32(y+2.0*w)
    return f32(-y+2.0*w)
def snap(v,plane,eps):
    x,y,z,w=v
    if plane==0:w=f32(eps)
    elif plane==1:x=f32(eps-2.0*w)
    elif plane==2:x=f32(2.0*w-eps)
    elif plane==3:y=f32(eps-2.0*w)
    else:y=f32(2.0*w-eps)
    return [x,y,z,w]
def pos_key(v): return (v[0],v[1],v[2],v[3])
def canonical_intersection(a,b,plane,eps):
    if pos_key(a) > pos_key(b): a,b=b,a
    da,db=dist(a,plane),dist(b,plane)
    t=f32(div(sub(da,eps),sub(da,db)))
    t=f32(max(0.0,min(1.0,t)))
    return snap(interp(a,b,t),plane,eps)
clip_checked=0
for plane in range(5):
    for _ in range(22000):
        # Include negative W for W_SAFE and broad x/y for guard-band crossings.
        a=[f32(rng.uniform(-6,6)) for _j in range(3)]+[f32(rng.uniform(-3,3))]
        b=[f32(rng.uniform(-6,6)) for _j in range(3)]+[f32(rng.uniform(-3,3))]
        da,db=dist(a,plane),dist(b,plane); eps=f32(0.0001 if plane==0 else 0.0)
        if not ((da>=eps) != (db>=eps)): continue
        r1=canonical_intersection(a,b,plane,eps)
        r2=canonical_intersection(b,a,plane,eps)
        if any(struct.pack('<f',r1[i]) != struct.pack('<f',r2[i]) for i in range(4)):
            raise SystemExit(f'P162_CONTRACT=FAIL canonical edge mismatch plane={plane} {r1} vs {r2}')
        clip_checked+=1
need(clip_checked > 20000, f'insufficient clip canonicalization samples {clip_checked}')

# Resident-room compact index must be semantically identical to the old scan.
room_checked=0
for _ in range(20000):
    rooms=[]; cursor=0x100000
    for room in range(1,rng.randint(2,140)):
        cursor += rng.randint(0,64)
        primary=rng.randint(0,2048) if rng.random()<0.28 else 0
        pstart=cursor if primary else 0
        cursor += primary
        cursor += rng.randint(0,64)
        secondary=rng.randint(0,1024) if rng.random()<0.12 else 0
        sstart=cursor if secondary else 0
        cursor += secondary
        rooms.append((room,pstart,primary,sstart,secondary))
    idx=[]
    for room,pstart,psz,sstart,ssz in rooms:
        if psz: idx.append((pstart,pstart+psz,room,'primary'))
        if ssz: idx.append((sstart,sstart+ssz,room,'secondary'))
    if idx and rng.random()<0.7:
        r=rng.choice(idx); addr=rng.randrange(r[0],r[1])
    else:
        addr=rng.randrange(0x80000,max(cursor+0x1000,0x90000))
    old=None
    for room,pstart,psz,sstart,ssz in rooms:
        if psz and pstart<=addr<pstart+psz: old=(room,'primary',pstart,addr-pstart); break
        if ssz and sstart<=addr<sstart+ssz: old=(room,'secondary',sstart,addr-sstart); break
    compact=None
    for st,en,room,which in idx:
        if st<=addr<en: compact=(room,which,st,addr-st); break
    if old!=compact: raise SystemExit(f'P162_CONTRACT=FAIL room index mismatch {old} vs {compact}')
    room_checked+=1
need(room_checked==20000,'room index model incomplete')

print(f'P162_MAP_EQUIVALENCE=PASS samples={checked} max_xy_lsb={max_xy} max_z_lsb={max_z} max_q_rel={max_q_rel:.3g}')
print(f'P162_HOMOGENEOUS_CULL=PASS samples={cull_checked}')
print(f'P162_CLIP_PLANE_SNAP=PASS samples={clip_checked}')
print(f'P162_ROOM_RANGE_INDEX=PASS samples={room_checked}')
print('P162_ROOM_LIFECYCLE_FROZEN=PASS')
print('P162_NATIVE_GEOMETRY_CONTRACT=PASS')
