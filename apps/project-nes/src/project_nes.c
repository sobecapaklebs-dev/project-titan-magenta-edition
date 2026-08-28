#include "titan_app_sdk.h"
#include "project_nes_core.h"

#include <stdint.h>
#include <stddef.h>

#define PN_TEST_ROM_PATH "data/roms/ProjectNesTest.nes"
#define PN_USER_ROM_PATH "data/roms/game.nes"
#define PN_ROM_CAPACITY (512u * 1024u)
#define PN_MEDIA_W 512u
#define PN_MEDIA_H 256u
#define PN_NES_W 256u
#define PN_NES_H 240u
#define PN_SCALED_W 320u
#define PN_SCALED_H 240u
#define PN_SCALED_X 96u
#define PN_SCALED_Y 8u
#define PN_CPU_CYCLES_PER_FRAME 29780
#define PN_NES_FRAME_BYTES (PN_NES_W * PN_NES_H * (uint32_t)sizeof(uint16_t))
#define PN_MEDIA_FRAME_BYTES (PN_MEDIA_W * PN_MEDIA_H * (uint32_t)sizeof(uint16_t))
#define PN_AUDIO_FRAMES 1024u
#define PN_SAVE_CAPACITY 8192u
#define PN_SAVE_FLUSH_FRAMES 120u
#define PN_AUDIO_BUFFER_BYTES (PN_AUDIO_FRAMES * 2u * (uint32_t)sizeof(int16_t))
#define PN_BOOT_WARMUP_FRAMES 3u
#define PN_TIMING_UNITS_PER_SECOND 1000u
#define PN_NES_FRAMES_PER_SECOND 60u
#define PN_MAX_CATCHUP_FRAMES 3u

#define PN_FLAG_C 0x01u
#define PN_FLAG_Z 0x02u
#define PN_FLAG_I 0x04u
#define PN_FLAG_D 0x08u
#define PN_FLAG_B 0x10u
#define PN_FLAG_U 0x20u
#define PN_FLAG_V 0x40u
#define PN_FLAG_N 0x80u

typedef enum ProjectNesScreen
{
    PN_SCREEN_LIBRARY = 0,
    PN_SCREEN_BOOTING,
    PN_SCREEN_RUNNING,
    PN_SCREEN_PAUSED,
    PN_SCREEN_ERROR
} ProjectNesScreen;

typedef struct PnCpu
{
    uint16_t pc;
    uint8_t sp;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t p;
    uint32_t unsupported_opcode;
} PnCpu;

typedef struct PnPpu
{
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t oam_addr;
    uint8_t oam[256];
    uint8_t vram[2048];
    uint8_t palette[32];
    uint8_t data_buffer;
    uint16_t vram_addr;
    uint16_t temp_addr;
    uint8_t scroll_x;
    uint8_t scroll_y;
    uint8_t write_toggle;
} PnPpu;

typedef struct PnNes
{
    PnCpu cpu;
    PnPpu ppu;
    uint8_t ram[2048];
    uint8_t prg_ram[8192];
    uint8_t chr_ram[8192];
    const uint8_t *prg;
    const uint8_t *chr;
    uint32_t prg_size;
    uint32_t chr_size;
    uint8_t chr_is_ram;
    uint8_t vertical_mirror;
    uint8_t controller_live;
    uint8_t controller_shift;
    uint8_t controller_strobe;
    uint8_t frame_ready;
    uint8_t halted;
    uint8_t apu[0x18];
    uint16_t pulse_phase[2];
    uint16_t triangle_phase;
    uint8_t audio_slot;
    uint8_t battery_backed;
    uint8_t prg_ram_dirty;
} PnNes;

typedef struct ProjectNesState
{
    const TitanAppApiV1 *api;
    ProjectNesScreen screen;
    PnNes nes;
    const ProjectNesCoreBackend *backend;
    void *backend_context;
    const ProjectNesProviderDescriptor *provider;
    ProjectNesCoreInput core_input;
    uint8_t *rom_data;
    uint32_t rom_size;
    uint16_t *nes_frame;
    uint16_t *media_frame;
    int16_t *audio_buffer;
    char error[96];
    uint32_t frame_counter;
    uint32_t timing_credit;
    uint8_t user_rom_present;
    uint8_t rom_selection;
    uint8_t library_probe_pending;
    uint8_t boot_frames_remaining;
    uint8_t audio_started;
    uint8_t provider_frame_ready;
    uint8_t save_loaded;
    uint32_t last_save_flush_frame;
    char save_path[48];
} ProjectNesState;

static ProjectNesState g_pn;
static uint16_t g_pn_nes_frame[PN_NES_W * PN_NES_H] __attribute__((aligned(64)));
static uint16_t g_pn_media_frame[PN_MEDIA_W * PN_MEDIA_H] __attribute__((aligned(64)));
static int16_t g_pn_audio_buffer[PN_AUDIO_FRAMES * 2u] __attribute__((aligned(64)));
static uint8_t g_pn_save_buffer[PN_SAVE_CAPACITY] __attribute__((aligned(64)));

static void *pn_backend_ctx(void)
{
    return g_pn.backend_context != 0 ? g_pn.backend_context : (void *)&g_pn.nes;
}

static void pn_zero(void *ptr, uint32_t size)
{
    uint8_t *p = (uint8_t *)ptr;
    uint32_t i;
    for (i = 0u; i < size; i++) p[i] = 0u;
}

static void pn_copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0u;
    if (dst == 0 || cap == 0u) return;
    while (src != 0 && src[i] != '\0' && i + 1u < cap)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void pn_set_error(const char *message)
{
    /* Release the Titan external audio stream before returning to the app UI.
     * This prevents a failed/unsupported ROM from leaving the shell music path
     * muted behind an empty emulator stream. */
    if (g_pn.api != 0 && g_pn.audio_started) g_pn.api->media_end_audio();
    g_pn.audio_started = 0u;
    pn_copy_text(g_pn.error, sizeof(g_pn.error), message);
    g_pn.screen = PN_SCREEN_ERROR;
}

static uint16_t pn_palette_rgb565(uint8_t index)
{
    static const uint16_t p[64] =
    {
        0x632c,0x0151,0x1094,0x3814,0x5810,0x6808,0x6820,0x50e0,
        0x3180,0x0a40,0x0280,0x0261,0x0209,0x0000,0x0000,0x0000,
        0xad75,0x12fb,0x421f,0x793f,0xa0d9,0xb0ef,0xb1a4,0x9a60,
        0x6b60,0x3c20,0x0c80,0x0486,0x03f1,0x0000,0x0000,0x0000,
        0xffff,0x659f,0x949f,0xc3bf,0xf35f,0xfb79,0xfc0e,0xece4,
        0xbde0,0x8ec0,0x5f06,0x470f,0x4e7b,0x4a69,0x0000,0x0000,
        0xffff,0xc6ff,0xd69f,0xee5f,0xfe1f,0xfe3d,0xfe78,0xfec8,
        0xe72a,0xd78b,0xb7b5,0xb79a,0xb75e,0xb5d6,0x0000,0x0000
    };
    return p[index & 63u];
}

static uint16_t pn_ppu_mirror_nametable(PnNes *n, uint16_t address)
{
    uint16_t v = (uint16_t)((address - 0x2000u) & 0x0fffu);
    uint16_t table = (uint16_t)(v >> 10);
    uint16_t offset = (uint16_t)(v & 0x03ffu);
    uint16_t physical;
    if (n->vertical_mirror)
        physical = (uint16_t)(table & 1u);
    else
        physical = (uint16_t)((table >> 1) & 1u);
    return (uint16_t)((physical << 10) | offset);
}

static uint8_t pn_ppu_read_mem(PnNes *n, uint16_t address)
{
    address &= 0x3fffu;
    if (address < 0x2000u)
    {
        if (n->chr_size == 0u) return n->chr_ram[address & 0x1fffu];
        return n->chr[address & 0x1fffu];
    }
    if (address < 0x3f00u)
        return n->ppu.vram[pn_ppu_mirror_nametable(n, address) & 0x07ffu];
    address = (uint16_t)((address - 0x3f00u) & 0x001fu);
    if (address == 0x10u) address = 0x00u;
    else if (address == 0x14u) address = 0x04u;
    else if (address == 0x18u) address = 0x08u;
    else if (address == 0x1cu) address = 0x0cu;
    return n->ppu.palette[address];
}

static void pn_ppu_write_mem(PnNes *n, uint16_t address, uint8_t value)
{
    address &= 0x3fffu;
    if (address < 0x2000u)
    {
        if (n->chr_size == 0u) n->chr_ram[address & 0x1fffu] = value;
        return;
    }
    if (address < 0x3f00u)
    {
        n->ppu.vram[pn_ppu_mirror_nametable(n, address) & 0x07ffu] = value;
        return;
    }
    address = (uint16_t)((address - 0x3f00u) & 0x001fu);
    if (address == 0x10u) address = 0x00u;
    else if (address == 0x14u) address = 0x04u;
    else if (address == 0x18u) address = 0x08u;
    else if (address == 0x1cu) address = 0x0cu;
    n->ppu.palette[address] = (uint8_t)(value & 0x3fu);
}

static uint32_t pn_udiv32(uint32_t numerator, uint32_t denominator)
{
    uint32_t quotient = 0u;
    uint32_t remainder = 0u;
    int bit;
    if (denominator == 0u) return 0u;
    for (bit = 31; bit >= 0; bit--)
    {
        remainder = (remainder << 1) | ((numerator >> bit) & 1u);
        if (remainder >= denominator)
        {
            remainder -= denominator;
            quotient |= (1u << bit);
        }
    }
    return quotient;
}

static uint8_t pn_cpu_read(PnNes *n, uint16_t address);
static void pn_cpu_write(PnNes *n, uint16_t address, uint8_t value);

static uint8_t pn_ppu_reg_read(PnNes *n, uint16_t reg)
{
    uint8_t value;
    reg &= 7u;
    if (reg == 2u)
    {
        value = n->ppu.status;
        n->ppu.status &= (uint8_t)~0x80u;
        n->ppu.write_toggle = 0u;
        return value;
    }
    if (reg == 4u)
        return n->ppu.oam[n->ppu.oam_addr];
    if (reg == 7u)
    {
        uint16_t address = n->ppu.vram_addr;
        uint8_t read = pn_ppu_read_mem(n, address);
        n->ppu.vram_addr = (uint16_t)(n->ppu.vram_addr + ((n->ppu.ctrl & 4u) ? 32u : 1u));
        if ((address & 0x3fffu) >= 0x3f00u)
        {
            n->ppu.data_buffer = pn_ppu_read_mem(n, (uint16_t)(address - 0x1000u));
            return read;
        }
        value = n->ppu.data_buffer;
        n->ppu.data_buffer = read;
        return value;
    }
    return 0u;
}

static void pn_ppu_reg_write(PnNes *n, uint16_t reg, uint8_t value)
{
    reg &= 7u;
    if (reg == 0u)
    {
        n->ppu.ctrl = value;
        n->ppu.temp_addr = (uint16_t)((n->ppu.temp_addr & 0xf3ffu) | ((uint16_t)(value & 3u) << 10));
    }
    else if (reg == 1u) n->ppu.mask = value;
    else if (reg == 3u) n->ppu.oam_addr = value;
    else if (reg == 4u) n->ppu.oam[n->ppu.oam_addr++] = value;
    else if (reg == 5u)
    {
        if (!n->ppu.write_toggle)
        {
            n->ppu.scroll_x = value;
            n->ppu.write_toggle = 1u;
        }
        else
        {
            n->ppu.scroll_y = value;
            n->ppu.write_toggle = 0u;
        }
    }
    else if (reg == 6u)
    {
        if (!n->ppu.write_toggle)
        {
            n->ppu.temp_addr = (uint16_t)(((uint16_t)(value & 0x3fu) << 8) | (n->ppu.temp_addr & 0x00ffu));
            n->ppu.write_toggle = 1u;
        }
        else
        {
            n->ppu.temp_addr = (uint16_t)((n->ppu.temp_addr & 0xff00u) | value);
            n->ppu.vram_addr = n->ppu.temp_addr;
            n->ppu.write_toggle = 0u;
        }
    }
    else if (reg == 7u)
    {
        pn_ppu_write_mem(n, n->ppu.vram_addr, value);
        n->ppu.vram_addr = (uint16_t)(n->ppu.vram_addr + ((n->ppu.ctrl & 4u) ? 32u : 1u));
    }
}

static uint8_t pn_cpu_read(PnNes *n, uint16_t address)
{
    if (address < 0x2000u) return n->ram[address & 0x07ffu];
    if (address < 0x4000u) return pn_ppu_reg_read(n, (uint16_t)(address & 7u));
    if (address == 0x4015u) return n->apu[0x15u] & 0x0fu;
    if (address == 0x4016u)
    {
        uint8_t value = (uint8_t)(n->controller_shift & 1u);
        if (!n->controller_strobe) n->controller_shift = (uint8_t)((n->controller_shift >> 1) | 0x80u);
        return (uint8_t)(value | 0x40u);
    }
    if (address >= 0x6000u && address < 0x8000u) return n->prg_ram[address - 0x6000u];
    if (address >= 0x8000u)
    {
        uint32_t offset = (uint32_t)(address - 0x8000u);
        if (n->prg_size == 16384u) offset &= 0x3fffu;
        else offset &= 0x7fffu;
        return n->prg[offset];
    }
    return 0u;
}

static void pn_cpu_write(PnNes *n, uint16_t address, uint8_t value)
{
    if (address < 0x2000u)
    {
        n->ram[address & 0x07ffu] = value;
        return;
    }
    if (address < 0x4000u)
    {
        pn_ppu_reg_write(n, (uint16_t)(address & 7u), value);
        return;
    }
    if (address >= 0x4000u && address <= 0x4015u && address != 0x4014u)
    {
        n->apu[address - 0x4000u] = value;
        if (address != 0x4016u) return;
    }
    if (address == 0x4014u)
    {
        uint16_t base = (uint16_t)((uint16_t)value << 8);
        uint16_t i;
        for (i = 0u; i < 256u; i++) n->ppu.oam[n->ppu.oam_addr++] = pn_cpu_read(n, (uint16_t)(base + i));
        return;
    }
    if (address == 0x4016u)
    {
        n->controller_strobe = (uint8_t)(value & 1u);
        if (n->controller_strobe) n->controller_shift = n->controller_live;
        else n->controller_shift = n->controller_live;
        return;
    }
    if (address >= 0x6000u && address < 0x8000u)
    {
        uint32_t index = (uint32_t)(address - 0x6000u);
        if (n->prg_ram[index] != value)
        {
            n->prg_ram[index] = value;
            if (n->battery_backed) n->prg_ram_dirty = 1u;
        }
    }
}

static uint16_t pn_read16(PnNes *n, uint16_t address)
{
    uint8_t lo = pn_cpu_read(n, address);
    uint8_t hi = pn_cpu_read(n, (uint16_t)(address + 1u));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint16_t pn_read16_bug(PnNes *n, uint16_t address)
{
    uint8_t lo = pn_cpu_read(n, address);
    uint16_t next = (uint16_t)((address & 0xff00u) | ((address + 1u) & 0x00ffu));
    uint8_t hi = pn_cpu_read(n, next);
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static void pn_cpu_set_zn(PnCpu *c, uint8_t v)
{
    if (v == 0u) c->p |= PN_FLAG_Z; else c->p &= (uint8_t)~PN_FLAG_Z;
    if (v & 0x80u) c->p |= PN_FLAG_N; else c->p &= (uint8_t)~PN_FLAG_N;
}

static void pn_push(PnNes *n, uint8_t value)
{
    pn_cpu_write(n, (uint16_t)(0x0100u | n->cpu.sp), value);
    n->cpu.sp--;
}

static uint8_t pn_pop(PnNes *n)
{
    n->cpu.sp++;
    return pn_cpu_read(n, (uint16_t)(0x0100u | n->cpu.sp));
}

static uint8_t pn_fetch8(PnNes *n)
{
    return pn_cpu_read(n, n->cpu.pc++);
}

static uint16_t pn_fetch16(PnNes *n)
{
    uint8_t lo = pn_fetch8(n);
    uint8_t hi = pn_fetch8(n);
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint16_t pn_addr_zp(PnNes *n) { return pn_fetch8(n); }
static uint16_t pn_addr_zpx(PnNes *n) { return (uint8_t)(pn_fetch8(n) + n->cpu.x); }
static uint16_t pn_addr_zpy(PnNes *n) { return (uint8_t)(pn_fetch8(n) + n->cpu.y); }
static uint16_t pn_addr_abs(PnNes *n) { return pn_fetch16(n); }
static uint16_t pn_addr_absx(PnNes *n) { return (uint16_t)(pn_fetch16(n) + n->cpu.x); }
static uint16_t pn_addr_absy(PnNes *n) { return (uint16_t)(pn_fetch16(n) + n->cpu.y); }
static uint16_t pn_addr_indx(PnNes *n)
{
    uint8_t zp = (uint8_t)(pn_fetch8(n) + n->cpu.x);
    uint8_t lo = pn_cpu_read(n, zp);
    uint8_t hi = pn_cpu_read(n, (uint8_t)(zp + 1u));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}
static uint16_t pn_addr_indy(PnNes *n)
{
    uint8_t zp = pn_fetch8(n);
    uint8_t lo = pn_cpu_read(n, zp);
    uint8_t hi = pn_cpu_read(n, (uint8_t)(zp + 1u));
    return (uint16_t)((lo | ((uint16_t)hi << 8)) + n->cpu.y);
}

static void pn_adc(PnCpu *c, uint8_t value)
{
    uint16_t sum = (uint16_t)c->a + value + ((c->p & PN_FLAG_C) ? 1u : 0u);
    uint8_t result = (uint8_t)sum;
    if (sum > 0xffu) c->p |= PN_FLAG_C; else c->p &= (uint8_t)~PN_FLAG_C;
    if (((~(c->a ^ value)) & (c->a ^ result) & 0x80u) != 0u) c->p |= PN_FLAG_V; else c->p &= (uint8_t)~PN_FLAG_V;
    c->a = result;
    pn_cpu_set_zn(c, c->a);
}

static void pn_sbc(PnCpu *c, uint8_t value) { pn_adc(c, (uint8_t)(value ^ 0xffu)); }

static void pn_cmp(PnCpu *c, uint8_t reg, uint8_t value)
{
    uint8_t result = (uint8_t)(reg - value);
    if (reg >= value) c->p |= PN_FLAG_C; else c->p &= (uint8_t)~PN_FLAG_C;
    pn_cpu_set_zn(c, result);
}

static void pn_branch(PnNes *n, int condition)
{
    int8_t rel = (int8_t)pn_fetch8(n);
    if (condition) n->cpu.pc = (uint16_t)(n->cpu.pc + rel);
}

static void pn_nmi(PnNes *n)
{
    pn_push(n, (uint8_t)(n->cpu.pc >> 8));
    pn_push(n, (uint8_t)n->cpu.pc);
    pn_push(n, (uint8_t)((n->cpu.p & ~PN_FLAG_B) | PN_FLAG_U));
    n->cpu.p |= PN_FLAG_I;
    n->cpu.pc = pn_read16(n, 0xfffau);
}

static int pn_cpu_step(PnNes *n)
{
    PnCpu *c = &n->cpu;
    uint8_t op = pn_fetch8(n);
    uint16_t a;
    uint8_t v;
    switch (op)
    {
        case 0x00: c->pc++; pn_push(n,(uint8_t)(c->pc>>8)); pn_push(n,(uint8_t)c->pc); pn_push(n,(uint8_t)(c->p|PN_FLAG_B|PN_FLAG_U)); c->p|=PN_FLAG_I; c->pc=pn_read16(n,0xfffeu); return 7;
        case 0x08: pn_push(n,(uint8_t)(c->p|PN_FLAG_B|PN_FLAG_U)); return 3;
        case 0x10: pn_branch(n,(c->p&PN_FLAG_N)==0u); return 2;
        case 0x18: c->p&=(uint8_t)~PN_FLAG_C; return 2;
        case 0x20: a=pn_fetch16(n); {uint16_t r=(uint16_t)(c->pc-1u);pn_push(n,(uint8_t)(r>>8));pn_push(n,(uint8_t)r);} c->pc=a; return 6;
        case 0x24: v=pn_cpu_read(n,pn_addr_zp(n)); if((c->a&v)==0)c->p|=PN_FLAG_Z;else c->p&=(uint8_t)~PN_FLAG_Z; c->p=(uint8_t)((c->p&~(PN_FLAG_N|PN_FLAG_V))|(v&(PN_FLAG_N|PN_FLAG_V))); return 3;
        case 0x28: c->p=(uint8_t)((pn_pop(n)&~PN_FLAG_B)|PN_FLAG_U); return 4;
        case 0x29: c->a&=pn_fetch8(n); pn_cpu_set_zn(c,c->a); return 2;
        case 0x2c: v=pn_cpu_read(n,pn_addr_abs(n)); if((c->a&v)==0)c->p|=PN_FLAG_Z;else c->p&=(uint8_t)~PN_FLAG_Z; c->p=(uint8_t)((c->p&~(PN_FLAG_N|PN_FLAG_V))|(v&(PN_FLAG_N|PN_FLAG_V))); return 4;
        case 0x30: pn_branch(n,(c->p&PN_FLAG_N)!=0u); return 2;
        case 0x38: c->p|=PN_FLAG_C; return 2;
        case 0x40: c->p=(uint8_t)((pn_pop(n)&~PN_FLAG_B)|PN_FLAG_U); {uint8_t lo=pn_pop(n),hi=pn_pop(n);c->pc=(uint16_t)(lo|((uint16_t)hi<<8));} return 6;
        case 0x48: pn_push(n,c->a); return 3;
        case 0x49: c->a^=pn_fetch8(n); pn_cpu_set_zn(c,c->a); return 2;
        case 0x4c: c->pc=pn_fetch16(n); return 3;
        case 0x4d: c->a^=pn_cpu_read(n,pn_addr_abs(n)); pn_cpu_set_zn(c,c->a); return 4;
        case 0x50: pn_branch(n,(c->p&PN_FLAG_V)==0u); return 2;
        case 0x58: c->p&=(uint8_t)~PN_FLAG_I; return 2;
        case 0x60: {uint8_t lo=pn_pop(n),hi=pn_pop(n);c->pc=(uint16_t)((lo|((uint16_t)hi<<8))+1u);} return 6;
        case 0x68: c->a=pn_pop(n); pn_cpu_set_zn(c,c->a); return 4;
        case 0x69: pn_adc(c,pn_fetch8(n)); return 2;
        case 0x6c: c->pc=pn_read16_bug(n,pn_fetch16(n)); return 5;
        case 0x70: pn_branch(n,(c->p&PN_FLAG_V)!=0u); return 2;
        case 0x78: c->p|=PN_FLAG_I; return 2;
        case 0x84: pn_cpu_write(n,pn_addr_zp(n),c->y); return 3;
        case 0x85: pn_cpu_write(n,pn_addr_zp(n),c->a); return 3;
        case 0x86: pn_cpu_write(n,pn_addr_zp(n),c->x); return 3;
        case 0x88: c->y--; pn_cpu_set_zn(c,c->y); return 2;
        case 0x8a: c->a=c->x; pn_cpu_set_zn(c,c->a); return 2;
        case 0x8c: pn_cpu_write(n,pn_addr_abs(n),c->y); return 4;
        case 0x8d: pn_cpu_write(n,pn_addr_abs(n),c->a); return 4;
        case 0x8e: pn_cpu_write(n,pn_addr_abs(n),c->x); return 4;
        case 0x90: pn_branch(n,(c->p&PN_FLAG_C)==0u); return 2;
        case 0x94: pn_cpu_write(n,pn_addr_zpx(n),c->y); return 4;
        case 0x95: pn_cpu_write(n,pn_addr_zpx(n),c->a); return 4;
        case 0x96: pn_cpu_write(n,pn_addr_zpy(n),c->x); return 4;
        case 0x98: c->a=c->y; pn_cpu_set_zn(c,c->a); return 2;
        case 0x99: pn_cpu_write(n,pn_addr_absy(n),c->a); return 5;
        case 0x9a: c->sp=c->x; return 2;
        case 0x9d: pn_cpu_write(n,pn_addr_absx(n),c->a); return 5;
        case 0xa0: c->y=pn_fetch8(n); pn_cpu_set_zn(c,c->y); return 2;
        case 0xa1: c->a=pn_cpu_read(n,pn_addr_indx(n)); pn_cpu_set_zn(c,c->a); return 6;
        case 0xa2: c->x=pn_fetch8(n); pn_cpu_set_zn(c,c->x); return 2;
        case 0xa4: c->y=pn_cpu_read(n,pn_addr_zp(n)); pn_cpu_set_zn(c,c->y); return 3;
        case 0xa5: c->a=pn_cpu_read(n,pn_addr_zp(n)); pn_cpu_set_zn(c,c->a); return 3;
        case 0xa6: c->x=pn_cpu_read(n,pn_addr_zp(n)); pn_cpu_set_zn(c,c->x); return 3;
        case 0xa8: c->y=c->a; pn_cpu_set_zn(c,c->y); return 2;
        case 0xa9: c->a=pn_fetch8(n); pn_cpu_set_zn(c,c->a); return 2;
        case 0xaa: c->x=c->a; pn_cpu_set_zn(c,c->x); return 2;
        case 0xac: c->y=pn_cpu_read(n,pn_addr_abs(n)); pn_cpu_set_zn(c,c->y); return 4;
        case 0xad: c->a=pn_cpu_read(n,pn_addr_abs(n)); pn_cpu_set_zn(c,c->a); return 4;
        case 0xae: c->x=pn_cpu_read(n,pn_addr_abs(n)); pn_cpu_set_zn(c,c->x); return 4;
        case 0xb0: pn_branch(n,(c->p&PN_FLAG_C)!=0u); return 2;
        case 0xb1: c->a=pn_cpu_read(n,pn_addr_indy(n)); pn_cpu_set_zn(c,c->a); return 5;
        case 0xb4: c->y=pn_cpu_read(n,pn_addr_zpx(n)); pn_cpu_set_zn(c,c->y); return 4;
        case 0xb5: c->a=pn_cpu_read(n,pn_addr_zpx(n)); pn_cpu_set_zn(c,c->a); return 4;
        case 0xb6: c->x=pn_cpu_read(n,pn_addr_zpy(n)); pn_cpu_set_zn(c,c->x); return 4;
        case 0xb8: c->p&=(uint8_t)~PN_FLAG_V; return 2;
        case 0xb9: c->a=pn_cpu_read(n,pn_addr_absy(n)); pn_cpu_set_zn(c,c->a); return 4;
        case 0xba: c->x=c->sp; pn_cpu_set_zn(c,c->x); return 2;
        case 0xbc: c->y=pn_cpu_read(n,pn_addr_absx(n)); pn_cpu_set_zn(c,c->y); return 4;
        case 0xbd: c->a=pn_cpu_read(n,pn_addr_absx(n)); pn_cpu_set_zn(c,c->a); return 4;
        case 0xbe: c->x=pn_cpu_read(n,pn_addr_absy(n)); pn_cpu_set_zn(c,c->x); return 4;
        case 0xc0: pn_cmp(c,c->y,pn_fetch8(n)); return 2;
        case 0xc4: pn_cmp(c,c->y,pn_cpu_read(n,pn_addr_zp(n))); return 3;
        case 0xc5: pn_cmp(c,c->a,pn_cpu_read(n,pn_addr_zp(n))); return 3;
        case 0xc6: a=pn_addr_zp(n);v=(uint8_t)(pn_cpu_read(n,a)-1u);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 5;
        case 0xc8: c->y++;pn_cpu_set_zn(c,c->y);return 2;
        case 0xc9: pn_cmp(c,c->a,pn_fetch8(n));return 2;
        case 0xca: c->x--;pn_cpu_set_zn(c,c->x);return 2;
        case 0xcc: pn_cmp(c,c->y,pn_cpu_read(n,pn_addr_abs(n)));return 4;
        case 0xcd: pn_cmp(c,c->a,pn_cpu_read(n,pn_addr_abs(n)));return 4;
        case 0xce: a=pn_addr_abs(n);v=(uint8_t)(pn_cpu_read(n,a)-1u);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0xd0: pn_branch(n,(c->p&PN_FLAG_Z)==0u); return 2;
        case 0xd8: c->p&=(uint8_t)~PN_FLAG_D; return 2;
        case 0xe0: pn_cmp(c,c->x,pn_fetch8(n)); return 2;
        case 0xe4: pn_cmp(c,c->x,pn_cpu_read(n,pn_addr_zp(n))); return 3;
        case 0xe5: pn_sbc(c,pn_cpu_read(n,pn_addr_zp(n))); return 3;
        case 0xe6: a=pn_addr_zp(n);v=(uint8_t)(pn_cpu_read(n,a)+1u);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 5;
        case 0xe8: c->x++;pn_cpu_set_zn(c,c->x);return 2;
        case 0xe9: pn_sbc(c,pn_fetch8(n)); return 2;
        case 0xea: return 2;
        case 0xec: pn_cmp(c,c->x,pn_cpu_read(n,pn_addr_abs(n))); return 4;
        case 0xed: pn_sbc(c,pn_cpu_read(n,pn_addr_abs(n))); return 4;
        case 0xee: a=pn_addr_abs(n);v=(uint8_t)(pn_cpu_read(n,a)+1u);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0xf0: pn_branch(n,(c->p&PN_FLAG_Z)!=0u); return 2;
        case 0xf8: c->p|=PN_FLAG_D; return 2;

        /* ORA */
        case 0x01:c->a|=pn_cpu_read(n,pn_addr_indx(n));pn_cpu_set_zn(c,c->a);return 6;
        case 0x05:c->a|=pn_cpu_read(n,pn_addr_zp(n));pn_cpu_set_zn(c,c->a);return 3;
        case 0x09:c->a|=pn_fetch8(n);pn_cpu_set_zn(c,c->a);return 2;
        case 0x0d:c->a|=pn_cpu_read(n,pn_addr_abs(n));pn_cpu_set_zn(c,c->a);return 4;
        case 0x11:c->a|=pn_cpu_read(n,pn_addr_indy(n));pn_cpu_set_zn(c,c->a);return 5;
        case 0x15:c->a|=pn_cpu_read(n,pn_addr_zpx(n));pn_cpu_set_zn(c,c->a);return 4;
        case 0x19:c->a|=pn_cpu_read(n,pn_addr_absy(n));pn_cpu_set_zn(c,c->a);return 4;
        case 0x1d:c->a|=pn_cpu_read(n,pn_addr_absx(n));pn_cpu_set_zn(c,c->a);return 4;
        /* AND */
        case 0x21:c->a&=pn_cpu_read(n,pn_addr_indx(n));pn_cpu_set_zn(c,c->a);return 6;
        case 0x25:c->a&=pn_cpu_read(n,pn_addr_zp(n));pn_cpu_set_zn(c,c->a);return 3;
        case 0x2d:c->a&=pn_cpu_read(n,pn_addr_abs(n));pn_cpu_set_zn(c,c->a);return 4;
        case 0x31:c->a&=pn_cpu_read(n,pn_addr_indy(n));pn_cpu_set_zn(c,c->a);return 5;
        case 0x35:c->a&=pn_cpu_read(n,pn_addr_zpx(n));pn_cpu_set_zn(c,c->a);return 4;
        case 0x39:c->a&=pn_cpu_read(n,pn_addr_absy(n));pn_cpu_set_zn(c,c->a);return 4;
        case 0x3d:c->a&=pn_cpu_read(n,pn_addr_absx(n));pn_cpu_set_zn(c,c->a);return 4;
        /* EOR */
        case 0x41:c->a^=pn_cpu_read(n,pn_addr_indx(n));pn_cpu_set_zn(c,c->a);return 6;
        case 0x45:c->a^=pn_cpu_read(n,pn_addr_zp(n));pn_cpu_set_zn(c,c->a);return 3;
        case 0x51:c->a^=pn_cpu_read(n,pn_addr_indy(n));pn_cpu_set_zn(c,c->a);return 5;
        case 0x55:c->a^=pn_cpu_read(n,pn_addr_zpx(n));pn_cpu_set_zn(c,c->a);return 4;
        case 0x59:c->a^=pn_cpu_read(n,pn_addr_absy(n));pn_cpu_set_zn(c,c->a);return 4;
        case 0x5d:c->a^=pn_cpu_read(n,pn_addr_absx(n));pn_cpu_set_zn(c,c->a);return 4;
        /* ADC */
        case 0x61:pn_adc(c,pn_cpu_read(n,pn_addr_indx(n)));return 6;
        case 0x65:pn_adc(c,pn_cpu_read(n,pn_addr_zp(n)));return 3;
        case 0x6d:pn_adc(c,pn_cpu_read(n,pn_addr_abs(n)));return 4;
        case 0x71:pn_adc(c,pn_cpu_read(n,pn_addr_indy(n)));return 5;
        case 0x75:pn_adc(c,pn_cpu_read(n,pn_addr_zpx(n)));return 4;
        case 0x79:pn_adc(c,pn_cpu_read(n,pn_addr_absy(n)));return 4;
        case 0x7d:pn_adc(c,pn_cpu_read(n,pn_addr_absx(n)));return 4;
        /* Stores */
        case 0x81:pn_cpu_write(n,pn_addr_indx(n),c->a);return 6;
        case 0x91:pn_cpu_write(n,pn_addr_indy(n),c->a);return 6;
        /* CMP */
        case 0xc1:pn_cmp(c,c->a,pn_cpu_read(n,pn_addr_indx(n)));return 6;
        case 0xd1:pn_cmp(c,c->a,pn_cpu_read(n,pn_addr_indy(n)));return 5;
        case 0xd5:pn_cmp(c,c->a,pn_cpu_read(n,pn_addr_zpx(n)));return 4;
        case 0xd9:pn_cmp(c,c->a,pn_cpu_read(n,pn_addr_absy(n)));return 4;
        case 0xdd:pn_cmp(c,c->a,pn_cpu_read(n,pn_addr_absx(n)));return 4;
        /* SBC */
        case 0xe1:pn_sbc(c,pn_cpu_read(n,pn_addr_indx(n)));return 6;
        case 0xf1:pn_sbc(c,pn_cpu_read(n,pn_addr_indy(n)));return 5;
        case 0xf5:pn_sbc(c,pn_cpu_read(n,pn_addr_zpx(n)));return 4;
        case 0xf9:pn_sbc(c,pn_cpu_read(n,pn_addr_absy(n)));return 4;
        case 0xfd:pn_sbc(c,pn_cpu_read(n,pn_addr_absx(n)));return 4;

        /* ASL */
        case 0x0a:v=c->a;if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;c->a=(uint8_t)(v<<1);pn_cpu_set_zn(c,c->a);return 2;
        case 0x06:a=pn_addr_zp(n);v=pn_cpu_read(n,a);if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)(v<<1);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 5;
        case 0x0e:a=pn_addr_abs(n);v=pn_cpu_read(n,a);if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)(v<<1);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0x16:a=pn_addr_zpx(n);v=pn_cpu_read(n,a);if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)(v<<1);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0x1e:a=pn_addr_absx(n);v=pn_cpu_read(n,a);if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)(v<<1);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 7;
        /* LSR */
        case 0x4a:v=c->a;if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;c->a=(uint8_t)(v>>1);pn_cpu_set_zn(c,c->a);return 2;
        case 0x46:a=pn_addr_zp(n);v=pn_cpu_read(n,a);if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)(v>>1);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 5;
        case 0x4e:a=pn_addr_abs(n);v=pn_cpu_read(n,a);if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)(v>>1);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0x56:a=pn_addr_zpx(n);v=pn_cpu_read(n,a);if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)(v>>1);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0x5e:a=pn_addr_absx(n);v=pn_cpu_read(n,a);if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)(v>>1);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 7;
        /* ROL */
        case 0x2a:{uint8_t oldc=(c->p&PN_FLAG_C)?1u:0u;v=c->a;if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;c->a=(uint8_t)((v<<1)|oldc);pn_cpu_set_zn(c,c->a);return 2;}
        case 0x26:a=pn_addr_zp(n);v=pn_cpu_read(n,a);{uint8_t oldc=(c->p&PN_FLAG_C)?1u:0u;if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)((v<<1)|oldc);}pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 5;
        case 0x2e:a=pn_addr_abs(n);v=pn_cpu_read(n,a);{uint8_t oldc=(c->p&PN_FLAG_C)?1u:0u;if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)((v<<1)|oldc);}pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0x36:a=pn_addr_zpx(n);v=pn_cpu_read(n,a);{uint8_t oldc=(c->p&PN_FLAG_C)?1u:0u;if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)((v<<1)|oldc);}pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0x3e:a=pn_addr_absx(n);v=pn_cpu_read(n,a);{uint8_t oldc=(c->p&PN_FLAG_C)?1u:0u;if(v&0x80)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)((v<<1)|oldc);}pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 7;
        /* ROR */
        case 0x6a:{uint8_t oldc=(c->p&PN_FLAG_C)?0x80u:0u;v=c->a;if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;c->a=(uint8_t)((v>>1)|oldc);pn_cpu_set_zn(c,c->a);return 2;}
        case 0x66:a=pn_addr_zp(n);v=pn_cpu_read(n,a);{uint8_t oldc=(c->p&PN_FLAG_C)?0x80u:0u;if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)((v>>1)|oldc);}pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 5;
        case 0x6e:a=pn_addr_abs(n);v=pn_cpu_read(n,a);{uint8_t oldc=(c->p&PN_FLAG_C)?0x80u:0u;if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)((v>>1)|oldc);}pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0x76:a=pn_addr_zpx(n);v=pn_cpu_read(n,a);{uint8_t oldc=(c->p&PN_FLAG_C)?0x80u:0u;if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)((v>>1)|oldc);}pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0x7e:a=pn_addr_absx(n);v=pn_cpu_read(n,a);{uint8_t oldc=(c->p&PN_FLAG_C)?0x80u:0u;if(v&1)c->p|=PN_FLAG_C;else c->p&=(uint8_t)~PN_FLAG_C;v=(uint8_t)((v>>1)|oldc);}pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 7;
        /* Extra INC/DEC indexed forms */
        case 0xd6:a=pn_addr_zpx(n);v=(uint8_t)(pn_cpu_read(n,a)-1u);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0xde:a=pn_addr_absx(n);v=(uint8_t)(pn_cpu_read(n,a)-1u);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 7;
        case 0xf6:a=pn_addr_zpx(n);v=(uint8_t)(pn_cpu_read(n,a)+1u);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 6;
        case 0xfe:a=pn_addr_absx(n);v=(uint8_t)(pn_cpu_read(n,a)+1u);pn_cpu_write(n,a,v);pn_cpu_set_zn(c,v);return 7;
        default:
            c->unsupported_opcode = op;
            n->halted = 1u;
            return 2;
    }
    return 2;
}

static void pn_cpu_reset(PnNes *n)
{
    n->cpu.a = 0u;
    n->cpu.x = 0u;
    n->cpu.y = 0u;
    n->cpu.sp = 0xfdu;
    n->cpu.p = PN_FLAG_I | PN_FLAG_U;
    n->cpu.unsupported_opcode = 0xffffffffu;
    n->cpu.pc = pn_read16(n, 0xfffcu);
    n->halted = 0u;
}

static void pn_render_background(PnNes *n, uint16_t *out)
{
    uint32_t y;
    uint16_t base_nt = (uint16_t)(0x2000u + ((uint16_t)(n->ppu.ctrl & 3u) << 10));
    uint16_t pattern_base = (n->ppu.ctrl & 0x10u) ? 0x1000u : 0x0000u;
    uint16_t universal = pn_palette_rgb565(pn_ppu_read_mem(n,0x3f00u));

    for (y = 0u; y < PN_NES_H; y++)
    {
        uint32_t x;
        for (x = 0u; x < PN_NES_W; x++) out[y * PN_NES_W + x] = universal;
    }
    if ((n->ppu.mask & 0x08u) == 0u) return;

    for (y = 0u; y < PN_NES_H; y++)
    {
        uint32_t x;
        uint32_t sy = (y + n->ppu.scroll_y) % 240u;
        uint32_t tile_y = sy >> 3;
        uint32_t fine_y = sy & 7u;
        for (x = 0u; x < PN_NES_W; x++)
        {
            uint32_t sx = (x + n->ppu.scroll_x) & 255u;
            uint32_t tile_x = sx >> 3;
            uint32_t fine_x = sx & 7u;
            uint16_t nt_addr = (uint16_t)(base_nt + tile_y * 32u + tile_x);
            uint8_t tile = pn_ppu_read_mem(n, nt_addr);
            uint16_t attr_addr = (uint16_t)(base_nt + 0x03c0u + ((tile_y >> 2) * 8u) + (tile_x >> 2));
            uint8_t attr = pn_ppu_read_mem(n, attr_addr);
            uint8_t shift = (uint8_t)(((tile_y & 2u) ? 4u : 0u) + ((tile_x & 2u) ? 2u : 0u));
            uint8_t palette = (uint8_t)((attr >> shift) & 3u);
            uint16_t paddr = (uint16_t)(pattern_base + (uint16_t)tile * 16u + fine_y);
            uint8_t lo = pn_ppu_read_mem(n, paddr);
            uint8_t hi = pn_ppu_read_mem(n, (uint16_t)(paddr + 8u));
            uint8_t bit = (uint8_t)(7u - fine_x);
            uint8_t color = (uint8_t)(((lo >> bit) & 1u) | (((hi >> bit) & 1u) << 1));
            uint8_t nes_color = pn_ppu_read_mem(n, (uint16_t)(0x3f00u + (uint16_t)palette * 4u + color));
            if (color == 0u) nes_color = pn_ppu_read_mem(n,0x3f00u);
            out[y * PN_NES_W + x] = pn_palette_rgb565(nes_color);
        }
    }
}

static void pn_render_sprites(PnNes *n, uint16_t *out)
{
    int sprite;
    uint16_t pattern_base;
    if ((n->ppu.mask & 0x10u) == 0u) return;
    pattern_base = (n->ppu.ctrl & 0x08u) ? 0x1000u : 0x0000u;
    for (sprite = 63; sprite >= 0; sprite--)
    {
        uint8_t sy0 = n->ppu.oam[sprite * 4 + 0];
        uint8_t tile = n->ppu.oam[sprite * 4 + 1];
        uint8_t attr = n->ppu.oam[sprite * 4 + 2];
        uint8_t sx0 = n->ppu.oam[sprite * 4 + 3];
        int py;
        for (py = 0; py < 8; py++)
        {
            int y = (int)sy0 + 1 + py;
            int row = (attr & 0x80u) ? (7 - py) : py;
            uint16_t paddr;
            uint8_t lo, hi;
            int px;
            if (y < 0 || y >= (int)PN_NES_H) continue;
            paddr = (uint16_t)(pattern_base + (uint16_t)tile * 16u + (uint16_t)row);
            lo = pn_ppu_read_mem(n,paddr);
            hi = pn_ppu_read_mem(n,(uint16_t)(paddr+8u));
            for (px = 0; px < 8; px++)
            {
                int x = (int)sx0 + px;
                int bit = (attr & 0x40u) ? px : (7 - px);
                uint8_t color;
                uint8_t nes_color;
                if (x < 0 || x >= (int)PN_NES_W) continue;
                color = (uint8_t)(((lo >> bit)&1u) | (((hi >> bit)&1u)<<1));
                if (color == 0u) continue;
                nes_color = pn_ppu_read_mem(n,(uint16_t)(0x3f10u + (uint16_t)(attr&3u)*4u + color));
                out[(uint32_t)y * PN_NES_W + (uint32_t)x] = pn_palette_rgb565(nes_color);
            }
        }
    }
}

static void pn_scale_to_media(const uint16_t *src, uint16_t *dst)
{
    uint32_t i;
    for (i = 0u; i < PN_MEDIA_W * PN_MEDIA_H; i++) dst[i] = 0u;
    for (i = 0u; i < PN_SCALED_H; i++)
    {
        uint32_t dx;
        uint32_t sx = 0u;
        uint32_t acc = 0u;
        const uint16_t *srow = &src[i * PN_NES_W];
        uint16_t *drow = &dst[(PN_SCALED_Y + i) * PN_MEDIA_W + PN_SCALED_X];
        for (dx = 0u; dx < PN_SCALED_W; dx++)
        {
            drow[dx] = srow[sx];
            acc += 4u;
            if (acc >= 5u)
            {
                acc -= 5u;
                if (sx + 1u < PN_NES_W) sx++;
            }
        }
    }
}

static int16_t pn_clamp16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static uint16_t pn_pulse_increment(uint16_t timer)
{
    uint32_t denominator = ((uint32_t)timer + 1u) * 32000u;
    /* (1789773 / 16) * 4096 ~= 458178560. */
    return (uint16_t)pn_udiv32(458178560u, denominator);
}

static uint16_t pn_triangle_increment(uint16_t timer)
{
    uint32_t denominator = ((uint32_t)timer + 1u) * 32000u;
    /* (1789773 / 32) * 4096 ~= 229089280. */
    return (uint16_t)pn_udiv32(229089280u, denominator);
}

static int32_t pn_pulse_sample(PnNes *n, unsigned int channel)
{
    uint8_t base = channel == 0u ? 0u : 4u;
    uint8_t enabled = (uint8_t)((n->apu[0x15u] >> channel) & 1u);
    uint16_t timer;
    uint16_t inc;
    uint16_t phase;
    uint16_t threshold;
    uint8_t duty;
    uint8_t volume;
    static const uint16_t duty_threshold[4] = {512u,1024u,2048u,3072u};
    if (!enabled) return 0;
    timer = (uint16_t)(n->apu[base + 2u] | ((uint16_t)(n->apu[base + 3u] & 7u) << 8));
    if (timer < 8u) return 0;
    inc = pn_pulse_increment(timer);
    n->pulse_phase[channel] = (uint16_t)((n->pulse_phase[channel] + inc) & 4095u);
    phase = n->pulse_phase[channel];
    duty = (uint8_t)((n->apu[base] >> 6) & 3u);
    volume = (uint8_t)(n->apu[base] & 0x0fu);
    threshold = duty_threshold[duty];
    return phase < threshold ? (int32_t)volume * 520 : -(int32_t)volume * 520;
}

static int32_t pn_triangle_sample(PnNes *n)
{
    uint16_t timer;
    uint16_t inc;
    uint16_t phase;
    int32_t tri;
    if ((n->apu[0x15u] & 0x04u) == 0u) return 0;
    timer = (uint16_t)(n->apu[0x0au] | ((uint16_t)(n->apu[0x0bu] & 7u) << 8));
    if (timer < 2u) return 0;
    inc = pn_triangle_increment(timer);
    n->triangle_phase = (uint16_t)((n->triangle_phase + inc) & 4095u);
    phase = n->triangle_phase;
    if (phase < 2048u) tri = (int32_t)(phase >> 6);
    else tri = (int32_t)((4095u - phase) >> 6);
    tri -= 16;
    return tri * 240;
}

static int pn_submit_audio(PnNes *n)
{
    uint32_t frames;
    uint32_t rendered;
    uint32_t submitted;
    (void)n;
    if (g_pn.api == 0 || !g_pn.audio_started || g_pn.backend == 0 ||
        g_pn.backend->next_audio_frames == 0 ||
        g_pn.backend->render_audio_s16_stereo == 0) return 0;
    frames = g_pn.backend->next_audio_frames(pn_backend_ctx());
    if (frames == 0u || frames > PN_AUDIO_FRAMES) return 0;
    /* Do not ask the provider to render until Titan has queue capacity. This
     * keeps provider audio cadence/state unchanged under backpressure. */
    if (g_pn.api->media_audio_free_frames() < frames) return 0;
    rendered = g_pn.backend->render_audio_s16_stereo(
        pn_backend_ctx(), g_pn.audio_buffer, PN_AUDIO_FRAMES);
    if (rendered != frames) return 0;
    submitted = g_pn.api->media_submit_pcm(g_pn.audio_buffer, frames);
    return submitted == frames;
}

static void pn_audio_stop(void)
{
    if (g_pn.api != 0 && g_pn.audio_started) g_pn.api->media_end_audio();
    g_pn.audio_started = 0u;
}

static int pn_audio_start(void)
{
    if (g_pn.api == 0) return 0;
    pn_audio_stop();
    g_pn.audio_started = g_pn.api->media_begin_audio() ? 1u : 0u;
    return g_pn.audio_started != 0u;
}

static void pn_run_frame_legacy(PnNes *n)
{
    int cycles = 0;
    while (cycles < PN_CPU_CYCLES_PER_FRAME && !n->halted)
        cycles += pn_cpu_step(n);

    pn_render_background(n, g_pn.nes_frame);
    pn_render_sprites(n, g_pn.nes_frame);
    n->frame_ready = 1u;
    n->ppu.status |= 0x80u;
    if ((n->ppu.ctrl & 0x80u) != 0u && !n->halted) pn_nmi(n);
}

int ProjectNesCore_ProbeRom(const uint8_t *data, uint32_t size, ProjectNesCoreRomInfo *info)
{
    uint8_t flags6;
    uint8_t flags7;
    uint32_t mapper;
    ProjectNesCoreRomInfo parsed;
    if (data == 0 || info == 0 || size < 16u ||
        data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1au)
        return 0;
    flags6 = data[6];
    flags7 = data[7];
    pn_zero(&parsed,(uint32_t)sizeof(parsed));
    parsed.format = ((flags7 & 0x0cu) == 0x08u) ?
        PROJECT_NES_CORE_ROM_NES2 : PROJECT_NES_CORE_ROM_INES1;
    mapper = (uint32_t)(flags6 >> 4) | (uint32_t)(flags7 & 0xf0u);
    if (parsed.format == PROJECT_NES_CORE_ROM_NES2)
    {
        mapper |= (uint32_t)(data[8] & 0x0fu) << 8;
        parsed.submapper = (uint8_t)(data[8] >> 4);
        parsed.prg_rom_bytes = 0u;
        parsed.chr_rom_bytes = 0u;
        if ((data[12] & 3u) == 0u) parsed.region_hint = PROJECT_NES_CORE_REGION_NTSC;
        else if ((data[12] & 3u) == 1u) parsed.region_hint = PROJECT_NES_CORE_REGION_PAL;
        else parsed.region_hint = PROJECT_NES_CORE_REGION_UNKNOWN;
    }
    else
    {
        parsed.prg_rom_bytes = (uint32_t)data[4] * 16384u;
        parsed.chr_rom_bytes = (uint32_t)data[5] * 8192u;
        parsed.region_hint = (data[9] & 1u) ?
            PROJECT_NES_CORE_REGION_PAL : PROJECT_NES_CORE_REGION_NTSC;
    }
    parsed.mapper = mapper;
    parsed.battery_backed = (flags6 & 2u) ? 1u : 0u;
    parsed.has_trainer = (flags6 & 4u) ? 1u : 0u;
    parsed.vertical_mirror = (flags6 & 1u) ? 1u : 0u;
    *info = parsed;
    return 1;
}

static int pn_parse_rom(PnNes *n, uint8_t *data, uint32_t size)
{
    ProjectNesCoreRomInfo info;
    uint32_t offset;
    uint32_t prg_size;
    uint32_t chr_size;
    if (!ProjectNesCore_ProbeRom(data,size,&info))
        return PROJECT_NES_CORE_LOAD_INVALID;
    if (info.format != PROJECT_NES_CORE_ROM_INES1)
        return PROJECT_NES_CORE_LOAD_UNSUPPORTED_FORMAT;
    if (info.mapper != 0u) return PROJECT_NES_CORE_LOAD_UNSUPPORTED_MAPPER;
    if (data[4] != 1u && data[4] != 2u) return PROJECT_NES_CORE_LOAD_UNSUPPORTED_PRG;
    if (data[5] > 1u) return PROJECT_NES_CORE_LOAD_UNSUPPORTED_CHR;
    prg_size = info.prg_rom_bytes;
    chr_size = info.chr_rom_bytes;
    offset = 16u + (info.has_trainer ? 512u : 0u);
    if (offset > size || prg_size > size - offset) return PROJECT_NES_CORE_LOAD_TRUNCATED_PRG;
    n->prg = data + offset;
    n->prg_size = prg_size;
    offset += prg_size;
    if (chr_size != 0u)
    {
        if (chr_size > size - offset) return PROJECT_NES_CORE_LOAD_TRUNCATED_CHR;
        n->chr = data + offset;
        n->chr_size = chr_size;
        n->chr_is_ram = 0u;
    }
    else
    {
        n->chr = n->chr_ram;
        n->chr_size = 0u;
        n->chr_is_ram = 1u;
    }
    n->vertical_mirror = info.vertical_mirror;
    n->battery_backed = info.battery_backed;
    n->prg_ram_dirty = 0u;
    return 1;
}


static int pn_legacy_load_rom(void *context, uint8_t *data, uint32_t size)
{
    return pn_parse_rom((PnNes *)context, data, size);
}

static void pn_legacy_reset(void *context)
{
    pn_cpu_reset((PnNes *)context);
}

static void pn_legacy_set_input(void *context, const ProjectNesCoreInput *input)
{
    PnNes *n = (PnNes *)context;
    uint8_t value = input != 0 ? input->buttons : 0u;
    n->controller_live = value;
    if (n->controller_strobe) n->controller_shift = value;
}

static int pn_legacy_run_frame(void *context, const ProjectNesCoreVideoTarget *video)
{
    PnNes *n = (PnNes *)context;
    if (video == 0 || video->pixels == 0 ||
        video->width != PN_MEDIA_W || video->height != PN_MEDIA_H ||
        video->stride < PN_MEDIA_W) return 0;
    pn_run_frame_legacy(n);
    /* The legacy renderer writes the logical NES frame, then the provider
     * targets the frontend-owned RGB565 surface explicitly. */
    pn_scale_to_media(g_pn.nes_frame, video->pixels);
    n->frame_ready = 1u;
    return n->halted ? 0 : 1;
}

static uint32_t pn_legacy_frame_rate_millihz(void *context)
{
    (void)context;
    return 60000u;
}

static uint32_t pn_legacy_next_audio_frames(void *context)
{
    PnNes *n = (PnNes *)context;
    return n->audio_slot == 2u ? 534u : 533u;
}

static uint32_t pn_legacy_render_audio(void *context, int16_t *samples, uint32_t capacity_frames)
{
    PnNes *n = (PnNes *)context;
    uint32_t frames = pn_legacy_next_audio_frames(context);
    uint32_t i;
    if (samples == 0 || capacity_frames < frames) return 0u;
    for (i = 0u; i < frames; i++)
    {
        int32_t sample = pn_pulse_sample(n,0u) + pn_pulse_sample(n,1u) + pn_triangle_sample(n);
        int16_t out = pn_clamp16(sample);
        samples[i * 2u] = out;
        samples[i * 2u + 1u] = out;
    }
    n->audio_slot++;
    if (n->audio_slot >= 3u) n->audio_slot = 0u;
    return frames;
}

static uint32_t pn_legacy_save_ram_size(void *context)
{
    PnNes *n = (PnNes *)context;
    return n->battery_backed ? (uint32_t)sizeof(n->prg_ram) : 0u;
}

static int pn_legacy_save_ram_export(void *context, uint8_t *data, uint32_t capacity)
{
    PnNes *n = (PnNes *)context;
    uint32_t i, size = pn_legacy_save_ram_size(context);
    if (size == 0u || data == 0 || capacity < size) return 0;
    for (i = 0u; i < size; i++) data[i] = n->prg_ram[i];
    return 1;
}

static int pn_legacy_save_ram_import(void *context, const uint8_t *data, uint32_t size)
{
    PnNes *n = (PnNes *)context;
    uint32_t i, expected = pn_legacy_save_ram_size(context);
    if (expected == 0u || data == 0 || size != expected) return 0;
    for (i = 0u; i < expected; i++) n->prg_ram[i] = data[i];
    n->prg_ram_dirty = 0u;
    return 1;
}

static int pn_legacy_save_ram_dirty(void *context)
{
    return ((PnNes *)context)->prg_ram_dirty != 0u;
}

static void pn_legacy_save_ram_clear_dirty(void *context)
{
    ((PnNes *)context)->prg_ram_dirty = 0u;
}

static int pn_legacy_probe_rom(void *context, const uint8_t *data, uint32_t size, ProjectNesCoreRomInfo *info)
{
    (void)context;
    return ProjectNesCore_ProbeRom(data,size,info);
}

static const ProjectNesCoreBackend g_pn_legacy_backend =
{
    "Legacy NROM fallback",
    pn_legacy_load_rom,
    pn_legacy_reset,
    pn_legacy_set_input,
    pn_legacy_run_frame,
    pn_legacy_frame_rate_millihz,
    pn_legacy_next_audio_frames,
    pn_legacy_render_audio,
    pn_legacy_save_ram_size,
    pn_legacy_save_ram_export,
    pn_legacy_save_ram_import,
    pn_legacy_save_ram_dirty,
    pn_legacy_save_ram_clear_dirty,
    pn_legacy_probe_rom,
    0
};

#if defined(__GNUC__)
extern const ProjectNesProviderDescriptor *ProjectNesFceumm_QueryProvider(void) __attribute__((weak));
extern void ProjectNesFceummCompat_BindApi(const TitanAppApiV1 *api) __attribute__((weak));
#else
extern const ProjectNesProviderDescriptor *ProjectNesFceumm_QueryProvider(void);
extern void ProjectNesFceummCompat_BindApi(const TitanAppApiV1 *api);
#endif

static int pn_text_equal(const char *a, const char *b)
{
    uint32_t i = 0u;
    if (a == 0 || b == 0) return 0;
    while (a[i] != '\0' && b[i] != '\0')
    {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int pn_fceumm_commit_allowed(const char *commit)
{
    return pn_text_equal(commit, "b5e3566515c27dc66c9c20572171673126532e06") ||
           pn_text_equal(commit, "dc77e3ca4baad08f0023f61dba9846550735b74d");
}

static int pn_fceumm_license_allowed(const char *spdx)
{
    return pn_text_equal(spdx, "GPL-2.0-only") ||
           pn_text_equal(spdx, "GPL-2.0-or-later");
}

static int pn_backend_complete(const ProjectNesCoreBackend *b)
{
    return b != 0 && b->name != 0 && b->load_rom != 0 && b->reset != 0 &&
           b->set_input != 0 && b->run_frame != 0 && b->frame_rate_millihz != 0 &&
           b->next_audio_frames != 0 && b->render_audio_s16_stereo != 0 &&
           b->probe_rom != 0;
}

static int pn_provider_valid(const ProjectNesProviderDescriptor *p)
{
    if (p == 0 || p->abi_version != PROJECT_NES_PROVIDER_ABI_VERSION ||
        p->struct_size < sizeof(ProjectNesProviderDescriptor) || p->provider_name == 0 ||
        !pn_fceumm_license_allowed(p->license_spdx) || !pn_fceumm_commit_allowed(p->source_commit) ||
        !pn_backend_complete(p->backend)) return 0;
    return 1;
}

static void pn_select_backend_candidate(const ProjectNesProviderDescriptor *candidate)
{
    g_pn.backend = &g_pn_legacy_backend;
    g_pn.backend_context = &g_pn.nes;
    g_pn.provider = 0;
    if (pn_provider_valid(candidate))
    {
        g_pn.backend = candidate->backend;
        g_pn.backend_context = candidate->context;
        g_pn.provider = candidate;
    }
}

static void pn_select_backend(void)
{
    const ProjectNesProviderDescriptor *candidate = 0;
#if defined(__GNUC__)
    if (ProjectNesFceumm_QueryProvider != 0) candidate = ProjectNesFceumm_QueryProvider();
#else
    candidate = ProjectNesFceumm_QueryProvider();
#endif
    pn_select_backend_candidate(candidate);
}

static uint32_t pn_core_rate_millihz(void)
{
    uint32_t rate = 60000u;
    if (g_pn.backend != 0 && g_pn.backend->frame_rate_millihz != 0)
        rate = g_pn.backend->frame_rate_millihz(pn_backend_ctx());
    /* Corrupt/unsupported provider metadata must not explode the scheduler. */
    if (rate < 40000u || rate > 70000u) rate = 60000u;
    return rate;
}

static int pn_core_run_frame(void)
{
    ProjectNesCoreVideoTarget target;
    if (g_pn.backend == 0 || g_pn.backend->run_frame == 0) return 0;
    target.pixels = g_pn.media_frame;
    target.width = PN_MEDIA_W;
    target.height = PN_MEDIA_H;
    target.stride = PN_MEDIA_W;
    if (g_pn.backend->run_frame(pn_backend_ctx(), &target))
    {
        g_pn.provider_frame_ready = 1u;
        return 1;
    }
    return 0;
}

static uint32_t pn_fnv1a32(const uint8_t *data, uint32_t size)
{
    uint32_t hash = 2166136261u, i;
    for (i = 0u; i < size; i++)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void pn_build_save_path(const uint8_t *rom, uint32_t size)
{
    static const char hex[] = "0123456789abcdef";
    const char prefix[] = "data/saves/";
    const char suffix[] = ".srm";
    uint32_t hash = pn_fnv1a32(rom, size), p = 0u, i;
    for (i = 0u; prefix[i] != '\0'; i++) g_pn.save_path[p++] = prefix[i];
    for (i = 0u; i < 8u; i++)
        g_pn.save_path[p++] = hex[(hash >> ((7u-i)*4u)) & 0x0fu];
    for (i = 0u; suffix[i] != '\0'; i++) g_pn.save_path[p++] = suffix[i];
    g_pn.save_path[p] = '\0';
}

static uint32_t pn_save_ram_size(void)
{
    if (g_pn.backend == 0 || g_pn.backend->save_ram_size == 0) return 0u;
    return g_pn.backend->save_ram_size(pn_backend_ctx());
}

static void pn_load_save(void)
{
    uint32_t size = pn_save_ram_size();
    int got;
    g_pn.save_loaded = 0u;
    if (size == 0u || size > PN_SAVE_CAPACITY || g_pn.api == 0 ||
        g_pn.backend->save_ram_import == 0) return;
    got = g_pn.api->file_read(g_pn.save_path, 0u, g_pn_save_buffer, size);
    if (got == (int)size && g_pn.backend->save_ram_import(pn_backend_ctx(), g_pn_save_buffer, size))
        g_pn.save_loaded = 1u;
    if (g_pn.backend->save_ram_clear_dirty != 0)
        g_pn.backend->save_ram_clear_dirty(pn_backend_ctx());
}

static int pn_flush_save(int notify_failure)
{
    uint32_t size = pn_save_ram_size();
    int written;
    if (size == 0u || size > PN_SAVE_CAPACITY || g_pn.api == 0 ||
        g_pn.backend == 0 || g_pn.backend->save_ram_export == 0 ||
        g_pn.backend->save_ram_dirty == 0 ||
        !g_pn.backend->save_ram_dirty(pn_backend_ctx())) return 1;
    if (!g_pn.backend->save_ram_export(pn_backend_ctx(), g_pn_save_buffer, PN_SAVE_CAPACITY)) return 0;
    written = g_pn.api->file_write_data(g_pn.save_path, g_pn_save_buffer, size);
    if (written != (int)size)
    {
        if (notify_failure)
            (void)g_pn.api->notify(TITAN_APP_NOTIFICATION_WARNING, "Project Nes",
                "SRAM nao foi salva; estado sujo preservado para nova tentativa");
        return 0;
    }
    if (g_pn.backend->save_ram_clear_dirty != 0)
        g_pn.backend->save_ram_clear_dirty(pn_backend_ctx());
    g_pn.last_save_flush_frame = g_pn.frame_counter;
    return 1;
}

static void pn_save_tick(void)
{
    if (g_pn.backend == 0 || g_pn.backend->save_ram_dirty == 0 ||
        !g_pn.backend->save_ram_dirty(pn_backend_ctx())) return;
    if (g_pn.frame_counter - g_pn.last_save_flush_frame >= PN_SAVE_FLUSH_FRAMES)
        (void)pn_flush_save(0);
}

static int pn_load_rom(const char *rom_path)
{
    uint32_t offset = 0u;
    int got;
    int parsed;
    if (g_pn.rom_data != 0)
    {
        g_pn.api->release(g_pn.rom_data);
        g_pn.rom_data = 0;
        g_pn.rom_size = 0u;
    }
    g_pn.rom_data = (uint8_t *)g_pn.api->alloc(PN_ROM_CAPACITY);
    if (g_pn.rom_data == 0) return 0;
    while (offset < PN_ROM_CAPACITY)
    {
        uint32_t cap = PN_ROM_CAPACITY - offset;
        if (cap > 4096u) cap = 4096u;
        got = g_pn.api->file_read(rom_path, offset, g_pn.rom_data + offset, cap);
        if (got < 0)
        {
            g_pn.api->release(g_pn.rom_data);
            g_pn.rom_data = 0;
            return 0;
        }
        if (got == 0) break;
        offset += (uint32_t)got;
        if ((uint32_t)got < cap) break;
    }
    g_pn.rom_size = offset;
    pn_zero(&g_pn.nes, sizeof(g_pn.nes));
    parsed = (g_pn.backend != 0 && g_pn.backend->load_rom != 0)
        ? g_pn.backend->load_rom(pn_backend_ctx(), g_pn.rom_data, g_pn.rom_size) : 0;
    if (parsed != 1)
    {
        if (parsed == -2) pn_set_error("Mapper nao suportado nesta preview. Use NROM/Mapper 0.");
        else pn_set_error("ROM iNES invalida ou nao suportada.");
        g_pn.api->release(g_pn.rom_data);
        g_pn.rom_data = 0;
        g_pn.rom_size = 0u;
        return 0;
    }
    pn_build_save_path(g_pn.rom_data, g_pn.rom_size);
    pn_load_save();
    if (g_pn.backend != 0 && g_pn.backend->reset != 0)
        g_pn.backend->reset(pn_backend_ctx());
    g_pn.frame_counter = 0u;
    g_pn.last_save_flush_frame = 0u;
    g_pn.timing_credit = 0u;
    g_pn.boot_frames_remaining = PN_BOOT_WARMUP_FRAMES;
    g_pn.nes.frame_ready = 0u;
    if (!pn_audio_start())
    {
        pn_set_error("Titan nao iniciou a fila PCM do emulador.");
        return 0;
    }
    g_pn.screen = PN_SCREEN_BOOTING;
    return 1;
}

static void pn_input_to_controller(const TitanAppInput *input)
{
    uint8_t v = 0u;
    if (input == 0) return;
    if ((input->held & TITAN_APP_BUTTON_CROSS) != 0u) v |= PROJECT_NES_CORE_INPUT_A;
    if ((input->held & TITAN_APP_BUTTON_CIRCLE) != 0u) v |= PROJECT_NES_CORE_INPUT_B;
    if ((input->held & TITAN_APP_BUTTON_SELECT) != 0u) v |= PROJECT_NES_CORE_INPUT_SELECT;
    if ((input->held & TITAN_APP_BUTTON_START) != 0u) v |= PROJECT_NES_CORE_INPUT_START;
    if ((input->held & TITAN_APP_BUTTON_UP) != 0u) v |= PROJECT_NES_CORE_INPUT_UP;
    if ((input->held & TITAN_APP_BUTTON_DOWN) != 0u) v |= PROJECT_NES_CORE_INPUT_DOWN;
    if ((input->held & TITAN_APP_BUTTON_LEFT) != 0u) v |= PROJECT_NES_CORE_INPUT_LEFT;
    if ((input->held & TITAN_APP_BUTTON_RIGHT) != 0u) v |= PROJECT_NES_CORE_INPUT_RIGHT;
    g_pn.core_input.buttons = v;
    if (g_pn.backend != 0 && g_pn.backend->set_input != 0)
        g_pn.backend->set_input(pn_backend_ctx(), &g_pn.core_input);
}

int titan_app_init(const TitanAppApiV1 *api, const char *app_root)
{
    (void)app_root;
    if (api == 0 || api->abi_version != TITAN_APP_API_VERSION || api->struct_size < sizeof(TitanAppApiV1))
        return -1;

    /* Titan App init is deliberately resource-free.  The real PS2 proved the
     * earlier static-state model can cross the in-process loader boundary,
     * while API heap calls from the protected init thread can deadlock the
     * shell before the watchdog/input path gets another turn.  Keep the hot
     * emulator surfaces inside the module and only use Titan-owned dynamic
     * memory after the session is ACTIVE (ROM payloads, etc.). */
    pn_zero(&g_pn, (uint32_t)sizeof(g_pn));
    pn_zero(g_pn_nes_frame, PN_NES_FRAME_BYTES);
    pn_zero(g_pn_media_frame, PN_MEDIA_FRAME_BYTES);
    pn_zero(g_pn_audio_buffer, PN_AUDIO_BUFFER_BYTES);
    pn_zero(g_pn_save_buffer, PN_SAVE_CAPACITY);
    g_pn.api = api;
#if defined(__GNUC__)
    if (ProjectNesFceummCompat_BindApi != 0) ProjectNesFceummCompat_BindApi(api);
#else
    ProjectNesFceummCompat_BindApi(api);
#endif
    pn_select_backend();
    g_pn.nes_frame = g_pn_nes_frame;
    g_pn.media_frame = g_pn_media_frame;
    g_pn.audio_buffer = g_pn_audio_buffer;
    g_pn.screen = PN_SCREEN_LIBRARY;
    g_pn.user_rom_present = 0u;
    g_pn.rom_selection = 0u;
    g_pn.library_probe_pending = 1u;
    return 0;
}

void titan_app_update(const TitanAppInput *input, uint32_t delta_ms)
{
    (void)delta_ms;
    if (input == 0 || g_pn.api == 0) return;

    if (g_pn.library_probe_pending)
    {
        g_pn.user_rom_present =
            g_pn.api->file_exists(PN_USER_ROM_PATH) ? 1u : 0u;
        g_pn.library_probe_pending = 0u;
    }

    if ((input->held & (TITAN_APP_BUTTON_SELECT | TITAN_APP_BUTTON_CIRCLE)) ==
        (TITAN_APP_BUTTON_SELECT | TITAN_APP_BUTTON_CIRCLE))
    {
        g_pn.api->request_exit();
        return;
    }

    if (g_pn.screen == PN_SCREEN_LIBRARY)
    {
        if (g_pn.user_rom_present &&
            ((input->pressed & TITAN_APP_BUTTON_UP) != 0u ||
             (input->pressed & TITAN_APP_BUTTON_DOWN) != 0u))
            g_pn.rom_selection ^= 1u;
        if ((input->pressed & TITAN_APP_BUTTON_CROSS) != 0u)
        {
            const char *path = (g_pn.rom_selection == 1u && g_pn.user_rom_present)
                ? PN_USER_ROM_PATH : PN_TEST_ROM_PATH;
            if (!pn_load_rom(path) && g_pn.screen != PN_SCREEN_ERROR)
                pn_set_error("Nao foi possivel carregar a ROM selecionada");
        }
        else if ((input->pressed & TITAN_APP_BUTTON_CIRCLE) != 0u)
            g_pn.api->request_exit();
        return;
    }

    if (g_pn.screen == PN_SCREEN_ERROR)
    {
        if ((input->pressed & TITAN_APP_BUTTON_CIRCLE) != 0u) g_pn.screen = PN_SCREEN_LIBRARY;
        return;
    }

    if (g_pn.screen == PN_SCREEN_BOOTING)
    {
        pn_input_to_controller(input);
        (void)pn_core_run_frame();
        (void)pn_submit_audio(&g_pn.nes);
        g_pn.frame_counter++;
        if (g_pn.nes.halted)
        {
            pn_set_error("A ROM executou uma instrucao ainda nao suportada pela preview.");
            return;
        }
        if (g_pn.boot_frames_remaining > 0u) g_pn.boot_frames_remaining--;
        if (g_pn.boot_frames_remaining == 0u)
        {
            if (!g_pn.api->media_present_rgb565(
                    g_pn.media_frame, PN_MEDIA_W, PN_MEDIA_H, PN_MEDIA_W))
            {
                pn_set_error("Titan recusou o framebuffer RGB565 do emulador.");
                return;
            }
            g_pn.nes.frame_ready = 0u;
            g_pn.timing_credit = 0u;
            g_pn.screen = PN_SCREEN_RUNNING;
        }
        return;
    }

    if ((input->pressed & TITAN_APP_BUTTON_TRIANGLE) != 0u)
    {
        if (g_pn.screen == PN_SCREEN_PAUSED)
        {
            if (!pn_audio_start())
            {
                pn_set_error("Titan nao reiniciou a fila PCM do emulador.");
                return;
            }
            g_pn.timing_credit = 0u;
            g_pn.screen = PN_SCREEN_RUNNING;
        }
        else
        {
            (void)pn_flush_save(1);
            pn_audio_stop();
            g_pn.screen = PN_SCREEN_PAUSED;
        }
        return;
    }
    if (g_pn.screen == PN_SCREEN_PAUSED) return;

    if ((input->pressed & TITAN_APP_BUTTON_SQUARE) != 0u)
    {
        (void)pn_flush_save(1);
        if (g_pn.backend != 0 && g_pn.backend->reset != 0)
            g_pn.backend->reset(pn_backend_ctx());
        g_pn.nes.frame_ready = 0u;
        g_pn.frame_counter = 0u;
        g_pn.timing_credit = 0u;
        g_pn.boot_frames_remaining = PN_BOOT_WARMUP_FRAMES;
        if (!pn_audio_start())
        {
            pn_set_error("Titan nao reiniciou a fila PCM do emulador.");
            return;
        }
        g_pn.screen = PN_SCREEN_BOOTING;
        g_pn.api->notify(TITAN_APP_NOTIFICATION_INFO,"Project Nes","ROM reiniciada");
        return;
    }
    if (g_pn.screen != PN_SCREEN_RUNNING) return;

    /* Provider-rate fixed-step emulation. The frontend owns scheduling while
     * allowing a mature core to report NTSC/PAL cadence without Titan API calls. */
    {
        uint32_t elapsed = delta_ms;
        unsigned int frames_run = 0u;

        if (elapsed == 0u) elapsed = 16u;
        if (elapsed > 100u) elapsed = 100u;
        {
            uint32_t rate_millihz = pn_core_rate_millihz();
            if (elapsed > (0xffffffffu - g_pn.timing_credit) / rate_millihz)
                g_pn.timing_credit = 1000000u;
            else
                g_pn.timing_credit += elapsed * rate_millihz;
        }

        pn_input_to_controller(input);
        while (g_pn.timing_credit >= 1000000u &&
               frames_run < PN_MAX_CATCHUP_FRAMES)
        {
            g_pn.timing_credit -= 1000000u;
            (void)pn_core_run_frame();
            (void)pn_submit_audio(&g_pn.nes);
            g_pn.frame_counter++;
            frames_run++;
            if (g_pn.nes.halted)
            {
                pn_set_error("A ROM executou uma instrucao ainda nao suportada pela preview.");
                return;
            }
        }
        if (g_pn.timing_credit >= 1000000u)
            g_pn.timing_credit = 999999u;
    }

    if (g_pn.provider_frame_ready)
    {
        if (!g_pn.api->media_present_rgb565(
                g_pn.media_frame, PN_MEDIA_W, PN_MEDIA_H, PN_MEDIA_W))
        {
            pn_set_error("Titan recusou o framebuffer RGB565 do emulador.");
            return;
        }
        g_pn.provider_frame_ready = 0u;
        g_pn.nes.frame_ready = 0u;
    }
    pn_save_tick();
}

static void pn_draw_header(void)
{
    g_pn.api->draw_text_shadow(82.0f, 56.0f, 1.00f, "Project Nes", 246u,248u,252u,255u);
    g_pn.api->draw_text(84.0f, 96.0f, 0.54f, "A powerfull NES emulator for Project Titan", 218u,222u,232u,255u);
    g_pn.api->draw_text(84.0f, 121.0f, 0.49f, "Powered by: VlombisCore(TM) Technology", 222u,43u,82u,255u);
}

void titan_app_draw(void)
{
    if (g_pn.api == 0) return;

    if (g_pn.screen == PN_SCREEN_LIBRARY)
    {
        pn_draw_header();
        g_pn.api->draw_panel(82.0f, 158.0f, 690.0f, 194.0f, 232u);
        g_pn.api->draw_text_shadow(116.0f, 191.0f, 0.67f,
            g_pn.rom_selection == 0u ? "> ProjectNesTest.nes" : "  ProjectNesTest.nes",
            g_pn.rom_selection == 0u ? 222u : 246u,
            g_pn.rom_selection == 0u ? 43u : 248u,
            g_pn.rom_selection == 0u ? 82u : 252u,255u);
        if (g_pn.user_rom_present)
            g_pn.api->draw_text_shadow(116.0f, 226.0f, 0.62f,
                g_pn.rom_selection == 1u ? "> game.nes" : "  game.nes",
                g_pn.rom_selection == 1u ? 222u : 230u,
                g_pn.rom_selection == 1u ? 43u : 232u,
                g_pn.rom_selection == 1u ? 82u : 240u,255u);
        else
            g_pn.api->draw_text(116.0f, 226.0f, 0.47f, "Opcional: data/roms/game.nes", 180u,186u,201u,255u);
        g_pn.api->draw_text(116.0f, 266.0f, 0.49f, "Preview: iNES Mapper 0 (NROM)", 202u,208u,222u,255u);
        g_pn.api->draw_text(116.0f, 302.0f, 0.55f, "X  Iniciar", 222u,43u,82u,255u);
        g_pn.api->draw_text(116.0f, 330.0f, 0.45f, "Circulo  Voltar", 202u,208u,222u,255u);
        return;
    }

    if (g_pn.screen == PN_SCREEN_BOOTING)
    {
        pn_draw_header();
        g_pn.api->draw_panel(118.0f, 172.0f, 618.0f, 142.0f, 238u);
        g_pn.api->draw_text_shadow(154.0f, 210.0f, 0.66f,
            "Inicializando ROM...", 246u,248u,252u,255u);
        g_pn.api->draw_text(154.0f, 250.0f, 0.48f,
            "Preparando PPU e audio", 202u,208u,222u,255u);
        return;
    }

    if (g_pn.screen == PN_SCREEN_ERROR)
    {
        pn_draw_header();
        g_pn.api->draw_panel(100.0f, 165.0f, 654.0f, 174.0f, 238u);
        g_pn.api->draw_text_shadow(132.0f, 202.0f, 0.66f, "Falha ao executar ROM", 246u,248u,252u,255u);
        g_pn.api->draw_text(132.0f, 241.0f, 0.49f, g_pn.error, 222u,90u,108u,255u);
        g_pn.api->draw_text(132.0f, 298.0f, 0.49f, "Circulo  Voltar", 202u,208u,222u,255u);
        return;
    }

    g_pn.api->media_draw(107.0f, 64.0f, 640.0f, 320.0f, 255u);
    g_pn.api->draw_text_shadow(110.0f, 405.0f, 0.42f,
        "X=A  Circulo=B  Start/Select=NES  Triangulo=Pausa  Quadrado=Reset  Select+Circulo=Sair",
        228u,232u,240u,230u);

    if (g_pn.screen == PN_SCREEN_PAUSED)
    {
        g_pn.api->draw_panel(277.0f, 160.0f, 300.0f, 116.0f, 240u);
        g_pn.api->draw_text_shadow(356.0f, 193.0f, 0.72f, "Pausado", 246u,248u,252u,255u);
        g_pn.api->draw_text(323.0f, 238.0f, 0.48f, "Triangulo para continuar", 210u,216u,230u,255u);
    }
}

void titan_app_shutdown(void)
{
    const TitanAppApiV1 *api = g_pn.api;

    if (api != 0) (void)pn_flush_save(1);
    if (api != 0 && g_pn.audio_started) api->media_end_audio();
    if (g_pn.backend != 0 && g_pn.backend->shutdown != 0)
        g_pn.backend->shutdown(pn_backend_ctx());
    if (api != 0 && g_pn.rom_data != 0) api->release(g_pn.rom_data);
    pn_zero(&g_pn, (uint32_t)sizeof(g_pn));
    pn_zero(g_pn_nes_frame, PN_NES_FRAME_BYTES);
    pn_zero(g_pn_media_frame, PN_MEDIA_FRAME_BYTES);
    pn_zero(g_pn_audio_buffer, PN_AUDIO_BUFFER_BYTES);
    pn_zero(g_pn_save_buffer, PN_SAVE_CAPACITY);
}
