/**
 * test_stubs.c -- Minimal stubs for symbols referenced by GameBridge.c
 * that are NOT part of the ROM validation code path.
 *
 * This allows us to compile and link GameBridge.c for testing
 * game_validate_rom() without pulling in the entire game engine.
 *
 * Build: cc -DMACOS_APP_BUNDLE -DNATIVE_PORT -DNONMATCHING <includes> \
 *        test_stubs.c ...
 */

#include <stdint.h>
#include <stddef.h>

/* --- Types matching ultra64 conventions --- */
typedef unsigned char u8;
typedef unsigned int  u32;
typedef int           s32;
typedef float         f32;

/* ========================================================================
 * External globals referenced by GameBridge.c
 * ======================================================================== */

int g_pcStartLevel = 0;
int g_frame_count_diag = 0;

/* rom_io.h */
u8  *g_romData = NULL;
u32  g_romSize = 0;

/* Crash handler / GFX recovery globals (from main_pc.c and gfx_pc.c) */
#include <setjmp.h>
sigjmp_buf g_gfxRecoveryJmp;
volatile int g_gfxRecoveryActive = 0;
int g_crashRecoveryCount = 0;
volatile uintptr_t g_lastDlCmd = 0;
volatile u32 g_lastDlOpcode = 0, g_lastDlW0 = 0;
volatile uintptr_t g_lastDlW1 = 0;
volatile uintptr_t g_diag_current_cmd_addr = 0;
volatile uintptr_t g_diag_tex_addr = 0;
volatile u32 g_diag_tex_size_bytes = 0, g_diag_tex_needed = 0;
volatile u8 g_diag_tex_fmt = 0, g_diag_tex_siz = 0, g_diag_tex_slot = 0, g_diag_tex_tile = 0;

/* ========================================================================
 * Lifecycle stubs (game_init / game_run path -- never called in tests)
 * ======================================================================== */

void bossEntry(void) {}
void schedulerInitThread(void) {}

int  platformInitSDL(void)  { return 0; }
void platformShutdownSDL(void) {}

void gfx_init(void) {}

void portAudioInit(void) {}
void portAudioRegisterConfig(void) {}

void platformRegisterConfig(void) {}

int savedirInit(const char *savedir_override) { (void)savedir_override; return 0; }
const char *savedirPath(const char *filename) { (void)filename; return "/dev/null"; }
const char *savedirGet(void) { return "/tmp/"; }

void configInit(void) {}
s32  configSave(void) { return 1; }

/* Config bridge lookup stub (added by config bridge implementation) */
void *configFindEntry(const char *key, int *type_out) {
    (void)key;
    if (type_out) *type_out = -1;
    return NULL;
}

s32 configSetValue(const char *key, const char *value) {
    (void)key; (void)value; return 0;
}

void configRegisterInt(const char *key, s32 *var, s32 min, s32 max) {
    (void)key; (void)var; (void)min; (void)max;
}
void configRegisterFloat(const char *key, f32 *var, f32 min, f32 max) {
    (void)key; (void)var; (void)min; (void)max;
}
void configRegisterUInt(const char *key, u32 *var, u32 min, u32 max) {
    (void)key; (void)var; (void)min; (void)max;
}

int platformInitRom(const char *path) { (void)path; return 0; }
void platformPatchFileTable(u8 *romData) { (void)romData; }
void platformInitSegmentOffsets(void) {}

/* platform_stdio.h: ge007_sprintf stub */
#include <stdio.h>
#include <stdarg.h>
int ge007_sprintf(char *dst, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(dst, fmt, ap);
    va_end(ap);
    return r;
}

/* ========================================================================
 * RSP ucode symbol stubs (referenced by platform_gbi.h)
 * ======================================================================== */

long long int rspbootTextStart[1] = {0};
long long int rspbootTextEnd[1]   = {0};
long long int gspF3DEX2_NoN_fifoTextStart[1] = {0};
long long int gspF3DEX2_NoN_fifoTextEnd[1]   = {0};
long long int gspF3DEX2_NoN_fifoDataStart[1] = {0};
long long int gspF3DEX2_NoN_fifoDataEnd[1]   = {0};
