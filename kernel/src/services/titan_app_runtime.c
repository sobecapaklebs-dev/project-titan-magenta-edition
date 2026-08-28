#include "titan_app_runtime.h"

#include "titan_app_sdk.h"
#include "titan_audio.h"
#include "titan_notifications.h"
#include "titan_system_error.h"
#include "titan_shell_background.h"
#include "titan_storage_io.h"
#include "titan_storage_scheduler.h"
#include "titan_storage_mount.h"
#include "titan_time.h"
#include "titan_video.h"
#include "titan_font.h"
#include "titan_input.h"
#include "titan_elf_validate.h"
#include "titan_exec_handoff.h"
#include "titan_game_runtime.h"
#include "titan_diagnostics.h"
#include "titan_text_input.h"
#include "titan_app_network.h"
#include "titan_asset_image.h"
#include "titan_ftp_service.h"
#include "titan_vmc.h"
#include "titan_user_profile.h"
#include "titan_ingame_overlay.h"
#include "titan_localization.h"
#include "titan_platform_overhead.h"
#include "titan_unified_input.h"
#include "titan_igr_compatibility.h"
#include "titan_session_service.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


/* Runtime chrome is system UI too. Route its own static/dynamic status strings
 * through the same compatibility catalog used by the shell. App-provided text
 * that has no catalog match passes through unchanged. */
#define TitanFont_MeasureText(scale, text) \
    TitanFont_MeasureText((scale), TitanLocalization_Compat((text)))
#define TitanFont_DrawTextAlpha(x, y, scale, text, r, g, b, a) \
    TitanFont_DrawTextAlpha((x), (y), (scale), TitanLocalization_Compat((text)), (r), (g), (b), (a))
#define TitanFont_DrawTextShadow(x, y, scale, text, r, g, b, a) \
    TitanFont_DrawTextShadow((x), (y), (scale), TitanLocalization_Compat((text)), (r), (g), (b), (a))
#define TitanNotifications_Push(type, title, message) \
    TitanNotifications_Push((type), TitanLocalization_Compat((title)), TitanLocalization_Compat((message)))

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <kernel.h>
extern void *_gp;
#endif

#define TITAN_TMOD_MAX_FILE_SIZE (8u * 1024u * 1024u)
#define TITAN_TMOD_MAX_IMAGE_SIZE (8u * 1024u * 1024u)
#define TITAN_TMOD_MAX_SECTIONS 64u
#define TITAN_TMOD_MAX_PENDING_HI16 128u
#define TITAN_TMOD_ELFCLASS32 1u
#define TITAN_TMOD_ELFDATA2LSB 1u
#define TITAN_TMOD_ET_REL 1u
#define TITAN_TMOD_EM_MIPS 8u
#define TITAN_TMOD_SHT_PROGBITS 1u
#define TITAN_TMOD_SHT_SYMTAB 2u
#define TITAN_TMOD_SHT_STRTAB 3u
#define TITAN_TMOD_SHT_NOBITS 8u
#define TITAN_TMOD_SHT_RELA 4u
#define TITAN_TMOD_SHT_REL 9u
#define TITAN_TMOD_SHT_MIPS_REGINFO 0x70000006u
#define TITAN_TMOD_SHT_MIPS_OPTIONS 0x7000000du
#define TITAN_TMOD_SHT_MIPS_ABIFLAGS 0x7000002au
#define TITAN_TMOD_SHF_WRITE 0x1u
#define TITAN_TMOD_SHF_ALLOC 0x2u
#define TITAN_TMOD_SHF_EXECINSTR 0x4u
#define TITAN_TMOD_SHN_UNDEF 0u
#define TITAN_TMOD_SHN_ABS 0xfff1u
#define TITAN_TMOD_STT_FUNC 2u
#define TITAN_TMOD_EF_MIPS_PIC 0x00000002u
#define TITAN_TMOD_EF_MIPS_CPIC 0x00000004u
#define TITAN_TMOD_EF_MIPS_ABI2 0x00000020u
#define TITAN_TMOD_EF_MIPS_ABI_MASK 0x0000f000u
#define TITAN_TMOD_EF_MIPS_ARCH_ASE 0x0f000000u
#define TITAN_TMOD_EF_MIPS_ARCH_MASK 0xf0000000u
#define TITAN_TMOD_EF_MIPS_ARCH_3 0x20000000u
#define TITAN_TMOD_EF_MIPS_ARCH_32 0x50000000u
#define TITAN_TMOD_EF_MIPS_MACH_MASK 0x00ff0000u
#define TITAN_TMOD_EF_MIPS_MACH_5900 0x00920000u
#define TITAN_TMOD_EF_MIPS_FP64 0x00000200u
#define TITAN_TMOD_EF_MIPS_NAN2008 0x00000400u
#define TITAN_TMOD_R_MIPS_NONE 0u
#define TITAN_TMOD_R_MIPS_32 2u
#define TITAN_TMOD_R_MIPS_26 4u
#define TITAN_TMOD_R_MIPS_HI16 5u
#define TITAN_TMOD_R_MIPS_LO16 6u
#define TITAN_APP_MEDIA_BYTES \
    (TITAN_VIDEO_MEDIA_SURFACE_WIDTH * TITAN_VIDEO_MEDIA_SURFACE_HEIGHT * 2u)
#define TITAN_APP_BACKGROUND_PRIMARY_BYTES \
    (TITAN_VIDEO_MEDIA_PRIMARY_WIDTH * TITAN_VIDEO_MEDIA_PRIMARY_HEIGHT * 2u)
#define TITAN_APP_BACKGROUND_LEGACY_BYTES \
    (TITAN_VIDEO_MEDIA_LEGACY_WIDTH * TITAN_VIDEO_MEDIA_LEGACY_HEIGHT * 2u)
#define TITAN_APP_INIT_THREAD_STACK_SIZE (64u * 1024u)
#define TITAN_APP_INIT_THREAD_PRIORITY 80
#define TITAN_APP_GAME_THREAD_STACK_SIZE (256u * 1024u)
#define TITAN_APP_GAME_THREAD_PRIORITY 64
#define TITAN_APP_GAME_SHUTDOWN_TIMEOUT_MS 2000u
#define TITAN_APP_INIT_TIMEOUT_MS 15000u
#define TITAN_APP_SPLASH_MIN_MS 900u
#define TITAN_APP_SPLASH_FADE_MS 220u
#define TITAN_APP_RUNTIME_MAX_ALLOCATIONS 32u
#define TITAN_APP_RUNTIME_MAX_HEAP_BYTES (4u * 1024u * 1024u)
#define TITAN_APP_RUNTIME_COORD_LIMIT 4096.0f
#define TITAN_APP_RUNTIME_SCALE_LIMIT 8.0f
#define TITAN_APP_IMAGE_SOURCE_MAX (512u * 1024u)

/* Titan-native apps are windowed by default, like Orbis browser/live-area apps.
 * The app continues to draw in the public 854x448 logical canvas; the renderer
 * maps that canvas into this viewport. R3 toggles shell-owned fullscreen. */
#define TITAN_APP_WINDOW_SCALE 0.72f
#define TITAN_APP_WINDOW_X 119.56f
#define TITAN_APP_WINDOW_Y 62.72f
#define TITAN_APP_WINDOW_W 614.88f
#define TITAN_APP_WINDOW_H 322.56f
#define TITAN_APP_WINDOW_PAD 18.0f

typedef struct TitanTmodElfHeader
{
    unsigned char ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} TitanTmodElfHeader;

typedef struct TitanTmodSection
{
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
} TitanTmodSection;

typedef struct TitanTmodSymbol
{
    uint32_t name;
    uint32_t value;
    uint32_t size;
    unsigned char info;
    unsigned char other;
    uint16_t shndx;
} TitanTmodSymbol;

typedef struct TitanTmodRel
{
    uint32_t offset;
    uint32_t info;
} TitanTmodRel;

typedef struct TitanTmodRela
{
    uint32_t offset;
    uint32_t info;
    int32_t addend;
} TitanTmodRela;

typedef struct TitanTmodPendingHi16
{
    uint32_t *target;
    unsigned int symbol_index;
    unsigned int target_section_index;
    int32_t addend;
} TitanTmodPendingHi16;

typedef struct TitanTmodLoaded
{
    unsigned char *file_data;
    unsigned int file_size;
    void *allocation;
    unsigned char *image;
    unsigned int image_size;
    unsigned char *section_address[TITAN_TMOD_MAX_SECTIONS];
    const TitanTmodSection *sections;
    unsigned int section_count;
    const TitanTmodSymbol *symbols;
    unsigned int symbol_count;
    unsigned int symtab_section_index;
    const char *strings;
    unsigned int strings_size;
    TitanAppInitFn init;
    TitanAppUpdateFn update;
    TitanAppDrawFn draw;
    TitanAppShutdownFn shutdown;
} TitanTmodLoaded;

typedef enum TitanAppLaunchPhase
{
    TITAN_APP_LAUNCH_IDLE = 0,
    TITAN_APP_LAUNCH_WAIT_STORAGE,
    TITAN_APP_LAUNCH_LOAD_MODULE,
    TITAN_APP_LAUNCH_LOAD_BACKGROUND,
    TITAN_APP_LAUNCH_START_INIT,
    TITAN_APP_LAUNCH_WAIT_INIT
} TitanAppLaunchPhase;

typedef struct TitanAppAllocation
{
    void *pointer;
    uint32_t size;
} TitanAppAllocation;

typedef struct TitanAppRuntimeState
{
    int active;
    int launching;
    int exit_requested;
    int background_ready;
    int media_ready;
    int storage_exclusive;
    int fullscreen;
    int trusted_game;
    uint32_t pfs_owned_slots;
    TitanAppLifecycleState lifecycle;
    TitanAppRuntimeClass runtime_class;
    TitanAppExitReason exit_reason;
    int text_input_owned;
    int text_input_result;
    char text_input_value[TITAN_TEXT_INPUT_TEXT_CAPACITY];
    volatile int init_done;
    volatile int init_result;
    TitanAppLaunchPhase launch_phase;
    uint64_t launch_started_ms;
    unsigned short *background_pixels;
    unsigned int background_width;
    unsigned int background_height;
    unsigned short *media_pixels;
    TitanAppAllocation allocations[TITAN_APP_RUNTIME_MAX_ALLOCATIONS];
    uint32_t allocation_bytes;
    unsigned int allocation_count;
    unsigned int rejected_draw_calls;
    int draw_rejection_reported;
    volatile int game_thread_started;
    volatile int game_thread_done;
    TitanTmodLoaded module;
    char title[TITAN_APP_RUNTIME_TITLE_CAPACITY];
    char root_path[TITAN_APP_RUNTIME_PATH_CAPACITY];
    char module_path[TITAN_APP_RUNTIME_PATH_CAPACITY];
    char background_path[TITAN_APP_RUNTIME_PATH_CAPACITY];
    char launch_status[96];
    char last_error[128];
#if defined(__EE__) || defined(_EE)
    int init_thread_id;
    unsigned char init_thread_stack[TITAN_APP_INIT_THREAD_STACK_SIZE]
        __attribute__((aligned(64)));
    int game_thread_id;
    unsigned char game_thread_stack[TITAN_APP_GAME_THREAD_STACK_SIZE]
        __attribute__((aligned(64)));
#endif
} TitanAppRuntimeState;

static TitanAppRuntimeState g_app_runtime;

static void TitanAppRuntime_CopyText(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
    {
        return;
    }
    index = 0u;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void TitanAppRuntime_SetLifecycle(TitanAppLifecycleState state)
{
    if (g_app_runtime.lifecycle == state) return;
    g_app_runtime.lifecycle = state;
    TitanDiagnostics_Event(
        TITAN_DIAG_INFO, TITAN_DIAG_APP_RUNTIME,
        (uint16_t)(0x2700u + (uint16_t)state), 0,
        (uint32_t)g_app_runtime.runtime_class,
        (uint32_t)g_app_runtime.exit_reason,
        g_app_runtime.allocation_count,
        g_app_runtime.allocation_bytes);
}

static void TitanAppRuntime_SetError(const char *message)
{
    TitanAppRuntime_CopyText(
        g_app_runtime.last_error,
        sizeof(g_app_runtime.last_error),
        TitanLocalization_Compat(
            message != 0 ? message : "Titan App runtime error"
        )
    );
}

static void TitanAppRuntime_SetLaunchStatus(const char *message)
{
    TitanAppRuntime_CopyText(
        g_app_runtime.launch_status,
        sizeof(g_app_runtime.launch_status),
        TitanLocalization_Compat(
            message != 0 ? message : "Preparando Titan App"
        )
    );
}

static void TitanTmod_RecordFlagReject(
    const TitanTmodElfHeader *header,
    int reason,
    uint32_t architecture,
    uint32_t abi
)
{
    TitanDiagnostics_Event(
        TITAN_DIAG_WARN, TITAN_DIAG_APP_RUNTIME, 0x27F0u, reason,
        header != 0 ? header->flags : 0u,
        architecture,
        abi,
        header != 0 ? (header->flags & TITAN_TMOD_EF_MIPS_MACH_MASK) : 0u
    );
}

static int TitanTmod_TargetFlagsCompatible(const TitanTmodElfHeader *header)
{
    uint32_t architecture;
    uint32_t abi;

    if (header == 0)
    {
        TitanTmod_RecordFlagReject(0, -1, 0u, 0u);
        TitanAppRuntime_SetError("Cabecalho MIPS ausente");
        return 0;
    }

    architecture = header->flags & TITAN_TMOD_EF_MIPS_ARCH_MASK;
    abi = header->flags & TITAN_TMOD_EF_MIPS_ABI_MASK;

    /* Titan App is an in-process ABI, so accepting a merely "MIPS-looking"
     * object is unsafe: the first unsupported instruction corrupts the whole
     * shell.  The PS2DEV R5900 binutils target stamps EF_MIPS_ARCH_3 plus
     * EF_MIPS_MACH_5900.  Require that exact CPU family at runtime. */
    if (architecture != TITAN_TMOD_EF_MIPS_ARCH_3 ||
        (header->flags & TITAN_TMOD_EF_MIPS_MACH_MASK) !=
            TITAN_TMOD_EF_MIPS_MACH_5900)
    {
        TitanTmod_RecordFlagReject(header, -2, architecture, abi);
        TitanAppRuntime_SetError(
            "Modulo nao foi compilado para R5900; recompile com mips64r5900el-ps2-elf-gcc"
        );
        return 0;
    }
    if ((header->flags & TITAN_TMOD_EF_MIPS_ARCH_ASE) != 0u)
    {
        TitanTmod_RecordFlagReject(header, -3, architecture, abi);
        TitanAppRuntime_SetError("Modulo usa MIPS16/microMIPS/ASE nao suportado");
        return 0;
    }
    if ((header->flags & (TITAN_TMOD_EF_MIPS_PIC | TITAN_TMOD_EF_MIPS_CPIC)) != 0u)
    {
        TitanTmod_RecordFlagReject(header, -4, architecture, abi);
        TitanAppRuntime_SetError("Modulo PIC/abicalls nao suportado pela Titan App API v1");
        return 0;
    }
    /* The EE toolchain used by Titan itself and by Project NES is N32.  N32
     * is encoded by EF_MIPS_ABI2 with no legacy ABI nibble.  An older runtime
     * guard accidentally did the inverse and rejected the exact modules the
     * release builder produced. */
    if ((header->flags & TITAN_TMOD_EF_MIPS_ABI2) == 0u || abi != 0u)
    {
        TitanTmod_RecordFlagReject(header, -5, architecture, abi);
        TitanAppRuntime_SetError("Modulo Titan Native requer ABI N32/ABI2; recompile o aplicativo");
        return 0;
    }
    if ((header->flags & (TITAN_TMOD_EF_MIPS_FP64 | TITAN_TMOD_EF_MIPS_NAN2008)) != 0u)
    {
        TitanTmod_RecordFlagReject(header, -6, architecture, abi);
        TitanAppRuntime_SetError("ABI de ponto flutuante MIPS incompativel com EE/R5900");
        return 0;
    }
    return 1;
}

static int TitanTmod_Bounds(
    unsigned int offset,
    unsigned int size,
    unsigned int total
)
{
    return offset <= total && size <= total - offset;
}

static unsigned int TitanTmod_AlignUp(unsigned int value, unsigned int alignment)
{
    unsigned int mask;

    if (alignment <= 1u)
    {
        return value;
    }
    if ((alignment & (alignment - 1u)) != 0u || alignment > 4096u)
    {
        return 0xffffffffu;
    }
    mask = alignment - 1u;
    if (value > 0xffffffffu - mask)
    {
        return 0xffffffffu;
    }
    return (value + mask) & ~mask;
}

static int TitanTmod_ReadFile(const char *path, unsigned char **data_out, unsigned int *size_out)
{
    TitanStorageIoSize size;
    unsigned char *data;
    int descriptor;

    if (path == 0 || data_out == 0 || size_out == 0 ||
        !TitanStorageIo_PathInfo64(path, 0, &size) ||
        size < sizeof(TitanTmodElfHeader) || size > TITAN_TMOD_MAX_FILE_SIZE)
    {
        return 0;
    }
    data = (unsigned char *)malloc((size_t)size);
    if (data == 0)
    {
        return 0;
    }
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0 || !TitanStorageIo_ReadExact(descriptor, data, (unsigned int)size))
    {
        if (descriptor >= 0)
        {
            TitanStorageIo_Close(descriptor);
        }
        free(data);
        return 0;
    }
    if (TitanStorageIo_Close(descriptor) < 0)
    {
        free(data);
        return 0;
    }
    *data_out = data;
    *size_out = (unsigned int)size;
    return 1;
}

static int TitanTmod_ParseHeader(
    const unsigned char *data,
    unsigned int size,
    const TitanTmodElfHeader **header_out,
    const TitanTmodSection **sections_out
)
{
    const TitanTmodElfHeader *header;

    if (data == 0 || size < sizeof(TitanTmodElfHeader))
    {
        return 0;
    }
    header = (const TitanTmodElfHeader *)data;
    if (
        header->ident[0] != 0x7fu || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != TITAN_TMOD_ELFCLASS32 ||
        header->ident[5] != TITAN_TMOD_ELFDATA2LSB ||
        header->type != TITAN_TMOD_ET_REL ||
        header->machine != TITAN_TMOD_EM_MIPS ||
        header->shentsize != sizeof(TitanTmodSection) ||
        header->shnum == 0u || header->shnum > TITAN_TMOD_MAX_SECTIONS ||
        !TitanTmod_Bounds(
            header->shoff,
            (unsigned int)header->shnum * sizeof(TitanTmodSection),
            size
        )
    )
    {
        return 0;
    }
    *header_out = header;
    *sections_out = (const TitanTmodSection *)(data + header->shoff);
    return 1;
}

static int TitanTmod_IsIgnorableMipsMetadataSection(const TitanTmodSection *section)
{
    if (section == 0)
    {
        return 0;
    }
    if ((section->flags & (TITAN_TMOD_SHF_WRITE | TITAN_TMOD_SHF_EXECINSTR)) != 0u)
    {
        return 0;
    }
    return section->type == TITAN_TMOD_SHT_MIPS_REGINFO ||
        section->type == TITAN_TMOD_SHT_MIPS_OPTIONS ||
        section->type == TITAN_TMOD_SHT_MIPS_ABIFLAGS;
}

static int TitanTmod_ValidateSections(
    const unsigned char *data,
    unsigned int size,
    const TitanTmodElfHeader *header,
    const TitanTmodSection *sections,
    unsigned int *image_size_out,
    unsigned int section_offsets[TITAN_TMOD_MAX_SECTIONS],
    unsigned int *symtab_index_out
)
{
    unsigned int image_size;
    unsigned int index;
    unsigned int symtab_index;

    (void)data;
    image_size = 0u;
    symtab_index = 0xffffffffu;
    memset(section_offsets, 0, sizeof(unsigned int) * TITAN_TMOD_MAX_SECTIONS);

    for (index = 0u; index < header->shnum; index++)
    {
        const TitanTmodSection *section;
        unsigned int alignment;
        unsigned int aligned;

        section = &sections[index];
        alignment = section->addralign == 0u ? 1u : section->addralign;
        if ((alignment & (alignment - 1u)) != 0u || alignment > 4096u)
        {
            return 0;
        }
        if (section->type != TITAN_TMOD_SHT_NOBITS && section->size > 0u &&
            !TitanTmod_Bounds(section->offset, section->size, size))
        {
            return 0;
        }
        if (section->type == TITAN_TMOD_SHT_SYMTAB)
        {
            if (symtab_index != 0xffffffffu ||
                section->entsize != sizeof(TitanTmodSymbol) ||
                section->link >= header->shnum)
            {
                return 0;
            }
            symtab_index = index;
        }
        if ((section->flags & TITAN_TMOD_SHF_ALLOC) == 0u || section->size == 0u)
        {
            continue;
        }
        if (section->type != TITAN_TMOD_SHT_PROGBITS &&
            section->type != TITAN_TMOD_SHT_NOBITS)
        {
            /* Modern MIPS compilers mark ABI/register metadata SHF_ALLOC even
             * though the records are link-time metadata, not runtime image
             * bytes. Keep rejecting unknown allocated section kinds, but
             * explicitly ignore the standard non-writable/non-executable MIPS
             * metadata emitted by GCC/Clang. */
            if (TitanTmod_IsIgnorableMipsMetadataSection(section))
            {
                continue;
            }
            return 0;
        }
        aligned = TitanTmod_AlignUp(image_size, alignment);
        if (aligned == 0xffffffffu || section->size > TITAN_TMOD_MAX_IMAGE_SIZE - aligned)
        {
            return 0;
        }
        section_offsets[index] = aligned;
        image_size = aligned + section->size;
    }

    if (symtab_index == 0xffffffffu || image_size == 0u ||
        image_size > TITAN_TMOD_MAX_IMAGE_SIZE)
    {
        return 0;
    }
    *image_size_out = image_size;
    *symtab_index_out = symtab_index;
    return 1;
}

static const char *TitanTmod_SymbolName(
    const TitanTmodLoaded *module,
    const TitanTmodSymbol *symbol
)
{
    if (module == 0 || symbol == 0 || symbol->name >= module->strings_size)
    {
        return 0;
    }
    if (memchr(module->strings + symbol->name, '\0', module->strings_size - symbol->name) == 0)
    {
        return 0;
    }
    return module->strings + symbol->name;
}

/* P10.7: explicit pure N32 compiler/newlib runtime whitelist. */
static uintptr_t TitanTmod_ResolveN32RuntimeBuiltin(const char *name)
{
    if (name == 0) return 0u;
#if defined(__EE__) || defined(_EE)
    extern double __adddf3(double, double);
    extern double __subdf3(double, double);
    extern double __muldf3(double, double);
    extern double __divdf3(double, double);
    extern long long __divdi3(long long, long long);
    extern unsigned long long __udivdi3(unsigned long long, unsigned long long);
    extern long long __muldi3(long long, long long);
    extern double __extendsfdf2(float);
    extern long long __fixdfdi(double);
    extern int __fixdfsi(double);
    extern unsigned int __fixunsdfsi(double);
    extern double __floatsidf(int);
    extern double __floatunsidf(unsigned int);
    extern float __truncdfsf2(double);
#define TITAN_N32_BUILTIN(symbol) if (strcmp(name, #symbol) == 0) return (uintptr_t)&symbol
    TITAN_N32_BUILTIN(__adddf3); TITAN_N32_BUILTIN(__subdf3);
    TITAN_N32_BUILTIN(__muldf3); TITAN_N32_BUILTIN(__divdf3);
    TITAN_N32_BUILTIN(__divdi3); TITAN_N32_BUILTIN(__udivdi3);
    TITAN_N32_BUILTIN(__muldi3); TITAN_N32_BUILTIN(__extendsfdf2);
    TITAN_N32_BUILTIN(__fixdfdi); TITAN_N32_BUILTIN(__fixdfsi);
    TITAN_N32_BUILTIN(__fixunsdfsi); TITAN_N32_BUILTIN(__floatsidf);
    TITAN_N32_BUILTIN(__floatunsidf); TITAN_N32_BUILTIN(__truncdfsf2);
#undef TITAN_N32_BUILTIN
    if (strcmp(name, "_ctype_") == 0) return (uintptr_t)_ctype_;
#else
    if (strcmp(name, "__adddf3") == 0 || strcmp(name, "__subdf3") == 0 ||
        strcmp(name, "__muldf3") == 0 || strcmp(name, "__divdf3") == 0 ||
        strcmp(name, "__divdi3") == 0 || strcmp(name, "__udivdi3") == 0 ||
        strcmp(name, "__muldi3") == 0 || strcmp(name, "__extendsfdf2") == 0 ||
        strcmp(name, "__fixdfdi") == 0 || strcmp(name, "__fixdfsi") == 0 ||
        strcmp(name, "__fixunsdfsi") == 0 || strcmp(name, "__floatsidf") == 0 ||
        strcmp(name, "__floatunsidf") == 0 || strcmp(name, "__truncdfsf2") == 0 ||
        strcmp(name, "_ctype_") == 0) return 1u;
#endif
    return 0u;
}

static uintptr_t TitanTmod_ResolveExternal(
    const char *name,
    int allow_game_imports
)
{
    if (name == 0)
    {
        return 0u;
    }
    {
        uintptr_t builtin = TitanTmod_ResolveN32RuntimeBuiltin(name);
        if (builtin != 0u) return builtin;
    }

    /* A very small compatibility whitelist keeps accidental libc references
     * from turning into arbitrary symbol lookup. New apps should use the
     * TitanAppApiV1 table instead. */
    if (strcmp(name, "memcpy") == 0) return (uintptr_t)&memcpy;
    if (strcmp(name, "memset") == 0) return (uintptr_t)&memset;
    if (strcmp(name, "memcmp") == 0) return (uintptr_t)&memcmp;
    if (strcmp(name, "strlen") == 0) return (uintptr_t)&strlen;
    if (strcmp(name, "strcmp") == 0) return (uintptr_t)&strcmp;

    /* BF27 native-core compatibility: FCEUmm remains sandboxed for heap and
     * storage through TitanAppApiV1, but portable emulator code needs a small
     * set of side-effect-free libc/libm helpers. No file/device/process/network
     * or allocator entry points are exposed here. */
    if (strcmp(name, "memmove") == 0) return (uintptr_t)&memmove;
    if (strcmp(name, "strchr") == 0) return (uintptr_t)&strchr;
    if (strcmp(name, "strrchr") == 0) return (uintptr_t)&strrchr;
    if (strcmp(name, "strstr") == 0) return (uintptr_t)&strstr;
    if (strcmp(name, "strncmp") == 0) return (uintptr_t)&strncmp;
    if (strcmp(name, "strtok") == 0) return (uintptr_t)&strtok;
    if (strcmp(name, "atoi") == 0) return (uintptr_t)&atoi;
    if (strcmp(name, "atof") == 0) return (uintptr_t)&atof;
    if (strcmp(name, "strtoul") == 0) return (uintptr_t)&strtoul;
    if (strcmp(name, "snprintf") == 0) return (uintptr_t)&snprintf;
    if (strcmp(name, "vsnprintf") == 0) return (uintptr_t)&vsnprintf;
    if (strcmp(name, "sprintf") == 0) return (uintptr_t)&sprintf;
    if (strcmp(name, "sscanf") == 0) return (uintptr_t)&sscanf;
    if (strcmp(name, "tolower") == 0) return (uintptr_t)&tolower;
    if (strcmp(name, "toupper") == 0) return (uintptr_t)&toupper;
    if (strcmp(name, "sin") == 0) return (uintptr_t)&sin;
    if (strcmp(name, "cos") == 0) return (uintptr_t)&cos;
    if (strcmp(name, "atan") == 0) return (uintptr_t)&atan;
    if (strcmp(name, "sqrt") == 0) return (uintptr_t)&sqrt;
    if (!allow_game_imports) return 0u;

    /* Trusted titan-game modules are substantially larger than UI apps, but
     * they still do not receive a general symbol resolver.  This explicit
     * pure libc/libm surface contains no file, device, process, network or
     * allocation entry points. Game heaps and storage remain accounted and
     * sandboxed exclusively through TitanGameApiV1. */
    if (strcmp(name, "memmove") == 0) return (uintptr_t)&memmove;
    if (strcmp(name, "strchr") == 0) return (uintptr_t)&strchr;
    if (strcmp(name, "strrchr") == 0) return (uintptr_t)&strrchr;
    if (strcmp(name, "strstr") == 0) return (uintptr_t)&strstr;
    if (strcmp(name, "strncmp") == 0) return (uintptr_t)&strncmp;
    if (strcmp(name, "strcpy") == 0) return (uintptr_t)&strcpy;
    if (strcmp(name, "strncpy") == 0) return (uintptr_t)&strncpy;
    if (strcmp(name, "strcat") == 0) return (uintptr_t)&strcat;
    if (strcmp(name, "strncat") == 0) return (uintptr_t)&strncat;
    if (strcmp(name, "strspn") == 0) return (uintptr_t)&strspn;
    if (strcmp(name, "strcspn") == 0) return (uintptr_t)&strcspn;
    if (strcmp(name, "strpbrk") == 0) return (uintptr_t)&strpbrk;
    if (strcmp(name, "strcasecmp") == 0) return (uintptr_t)&strcasecmp;
    if (strcmp(name, "strncasecmp") == 0) return (uintptr_t)&strncasecmp;
    if (strcmp(name, "atoi") == 0) return (uintptr_t)&atoi;
    if (strcmp(name, "atol") == 0) return (uintptr_t)&atol;
    if (strcmp(name, "strtol") == 0) return (uintptr_t)&strtol;
    if (strcmp(name, "strtoul") == 0) return (uintptr_t)&strtoul;
    if (strcmp(name, "strtoll") == 0) return (uintptr_t)&strtoll;
    if (strcmp(name, "strtoull") == 0) return (uintptr_t)&strtoull;
    if (strcmp(name, "strtod") == 0) return (uintptr_t)&strtod;
    if (strcmp(name, "strtof") == 0) return (uintptr_t)&strtof;
    if (strcmp(name, "abs") == 0) return (uintptr_t)&abs;
    if (strcmp(name, "labs") == 0) return (uintptr_t)&labs;
    if (strcmp(name, "qsort") == 0) return (uintptr_t)&qsort;
    if (strcmp(name, "bsearch") == 0) return (uintptr_t)&bsearch;
    if (strcmp(name, "snprintf") == 0) return (uintptr_t)&snprintf;
    if (strcmp(name, "vsnprintf") == 0) return (uintptr_t)&vsnprintf;
    if (strcmp(name, "sprintf") == 0) return (uintptr_t)&sprintf;
    if (strcmp(name, "vsprintf") == 0) return (uintptr_t)&vsprintf;
    if (strcmp(name, "sscanf") == 0) return (uintptr_t)&sscanf;

#define TITAN_GAME_MATH_IMPORT(symbol) \
    if (strcmp(name, #symbol) == 0) return (uintptr_t)&symbol
    TITAN_GAME_MATH_IMPORT(sinf);
    TITAN_GAME_MATH_IMPORT(cosf);
    TITAN_GAME_MATH_IMPORT(tanf);
    TITAN_GAME_MATH_IMPORT(asinf);
    TITAN_GAME_MATH_IMPORT(acosf);
    TITAN_GAME_MATH_IMPORT(atanf);
    TITAN_GAME_MATH_IMPORT(atan2f);
    TITAN_GAME_MATH_IMPORT(sqrtf);
    TITAN_GAME_MATH_IMPORT(fabsf);
    TITAN_GAME_MATH_IMPORT(floorf);
    TITAN_GAME_MATH_IMPORT(ceilf);
    TITAN_GAME_MATH_IMPORT(fmodf);
    TITAN_GAME_MATH_IMPORT(powf);
    TITAN_GAME_MATH_IMPORT(logf);
    TITAN_GAME_MATH_IMPORT(log10f);
    TITAN_GAME_MATH_IMPORT(expf);
    TITAN_GAME_MATH_IMPORT(ldexpf);
    TITAN_GAME_MATH_IMPORT(frexpf);
    TITAN_GAME_MATH_IMPORT(lroundf);
    TITAN_GAME_MATH_IMPORT(sin);
    TITAN_GAME_MATH_IMPORT(cos);
    TITAN_GAME_MATH_IMPORT(tan);
    TITAN_GAME_MATH_IMPORT(asin);
    TITAN_GAME_MATH_IMPORT(acos);
    TITAN_GAME_MATH_IMPORT(atan);
    TITAN_GAME_MATH_IMPORT(atan2);
    TITAN_GAME_MATH_IMPORT(sqrt);
    TITAN_GAME_MATH_IMPORT(fabs);
    TITAN_GAME_MATH_IMPORT(floor);
    TITAN_GAME_MATH_IMPORT(ceil);
    TITAN_GAME_MATH_IMPORT(fmod);
    TITAN_GAME_MATH_IMPORT(pow);
    TITAN_GAME_MATH_IMPORT(log);
    TITAN_GAME_MATH_IMPORT(log10);
    TITAN_GAME_MATH_IMPORT(exp);
    TITAN_GAME_MATH_IMPORT(ldexp);
    TITAN_GAME_MATH_IMPORT(frexp);
    TITAN_GAME_MATH_IMPORT(lround);
#undef TITAN_GAME_MATH_IMPORT
    return 0u;
}

static uintptr_t TitanTmod_ResolveSymbol(
    const TitanTmodLoaded *module,
    unsigned int symbol_index,
    int allow_game_imports
)
{
    const TitanTmodSymbol *symbol;
    const char *name;

    if (module == 0 || symbol_index >= module->symbol_count)
    {
        return 0u;
    }
    symbol = &module->symbols[symbol_index];
    if (symbol->shndx == TITAN_TMOD_SHN_UNDEF)
    {
        name = TitanTmod_SymbolName(module, symbol);
        return TitanTmod_ResolveExternal(name, allow_game_imports);
    }
    if (symbol->shndx == TITAN_TMOD_SHN_ABS)
    {
        return (uintptr_t)symbol->value;
    }
    if (symbol->shndx >= module->section_count ||
        module->section_address[symbol->shndx] == 0 ||
        symbol->value > module->sections[symbol->shndx].size)
    {
        return 0u;
    }
    return (uintptr_t)(module->section_address[symbol->shndx] + symbol->value);
}

static uint32_t TitanTmod_ReadInstructionLe(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

static int TitanTmod_R5900Cop1Compatible(uint32_t instruction)
{
    unsigned int rs;
    unsigned int rt;
    unsigned int function;

    rs = (instruction >> 21u) & 0x1fu;
    rt = (instruction >> 16u) & 0x1fu;
    function = instruction & 0x3fu;

    /* EE COP1 is the R5900 single-precision FPU. Keep this table deliberately
     * narrow and aligned with the real R5900 decode: register transfers,
     * BC1, single-precision arithmetic/compare, and CVT.S.W. A generic MIPS
     * compiler may otherwise emit D-format COP1 instructions which raise a
     * reserved-instruction exception and freeze an in-process Titan App. */
    if (rs == 0u || rs == 2u || rs == 4u || rs == 6u)
        return 1;
    if (rs == 8u)
        return rt <= 3u;
    if (rs == 20u)
        return function == 32u; /* CVT.S.W */
    if (rs != 16u)
        return 0;

    switch (function)
    {
        case 0u:  /* ADD.S */
        case 1u:  /* SUB.S */
        case 2u:  /* MUL.S */
        case 3u:  /* DIV.S */
        case 4u:  /* SQRT.S */
        case 5u:  /* ABS.S */
        case 6u:  /* MOV.S */
        case 7u:  /* NEG.S */
        case 22u: /* RSQRT.S */
        case 24u: /* ADDA.S */
        case 25u: /* SUBA.S */
        case 26u: /* MULA.S */
        case 28u: /* MADD.S */
        case 29u: /* MSUB.S */
        case 30u: /* MADDA.S */
        case 31u: /* MSUBA.S */
        case 36u: /* CVT.W.S */
        case 40u: /* MAX.S */
        case 41u: /* MIN.S */
        case 48u: /* C.F.S */
        case 50u: /* C.EQ.S */
        case 52u: /* C.LT.S */
        case 54u: /* C.LE.S */
            return 1;
        default:
            return 0;
    }
}

static int TitanTmod_R5900InstructionCompatible(uint32_t instruction)
{
    unsigned int opcode;

    opcode = instruction >> 26u;

    /* R5900 has LWC1/SWC1 but not LDC1/SDC1. These were the exact opcodes
     * emitted by the unsafe generic-Clang Project Nes build that hard-froze
     * hardware immediately after titan_app_init. */
    if (opcode == 0x35u || opcode == 0x3du)
        return 0;
    if (opcode == 0x11u)
        return TitanTmod_R5900Cop1Compatible(instruction);
    return 1;
}

static int TitanTmod_ValidateR5900ExecutableCode(const TitanTmodLoaded *module)
{
    unsigned int symbol_index;

    if (module == 0 || module->file_data == 0 || module->sections == 0 ||
        module->symbols == 0)
        return 0;

    for (symbol_index = 0u; symbol_index < module->symbol_count; symbol_index++)
    {
        const TitanTmodSymbol *symbol;
        const TitanTmodSection *section;
        unsigned int start;
        unsigned int end;
        unsigned int next_index;
        unsigned int offset;

        symbol = &module->symbols[symbol_index];
        if ((symbol->info & 0x0fu) != TITAN_TMOD_STT_FUNC ||
            symbol->shndx == TITAN_TMOD_SHN_UNDEF ||
            symbol->shndx >= module->section_count)
            continue;

        section = &module->sections[symbol->shndx];
        if ((section->flags & TITAN_TMOD_SHF_EXECINSTR) == 0u ||
            section->type != TITAN_TMOD_SHT_PROGBITS ||
            symbol->value >= section->size || (symbol->value & 3u) != 0u)
            continue;

        start = symbol->value;
        if (symbol->size != 0u && symbol->size <= section->size - start)
        {
            end = start + symbol->size;
        }
        else
        {
            /* Some MIPS assemblers leave st_size at zero. In that case stop at
             * the next function in the same section, or at the section end. */
            end = section->size;
            for (next_index = 0u; next_index < module->symbol_count; next_index++)
            {
                const TitanTmodSymbol *next = &module->symbols[next_index];
                if ((next->info & 0x0fu) == TITAN_TMOD_STT_FUNC &&
                    next->shndx == symbol->shndx &&
                    next->value > start && next->value < end)
                    end = next->value;
            }
        }
        end &= ~3u;

        if (!TitanTmod_Bounds(section->offset + start, end - start, module->file_size))
            return 0;

        for (offset = start; offset + 4u <= end; offset += 4u)
        {
            uint32_t instruction = TitanTmod_ReadInstructionLe(
                module->file_data + section->offset + offset
            );
            if (!TitanTmod_R5900InstructionCompatible(instruction))
            {
                TitanAppRuntime_SetError(
                    "Titan App contem instrucao COP1 incompativel com EE/R5900"
                );
                return 0;
            }
        }
    }
    return 1;
}

static int TitanTmod_ValidateRelocationTables(const TitanTmodLoaded *module)
{
    unsigned int section_index;
    if (module == 0) return 0;
    for (section_index = 0u; section_index < module->section_count; section_index++)
    {
        const TitanTmodSection *rel_section = &module->sections[section_index];
        unsigned int entry_size, relocation_count, relocation_index;
        unsigned int pending_symbols[TITAN_TMOD_MAX_PENDING_HI16];
        unsigned int pending_count = 0u, target_size;
        int is_rela;
        if (rel_section->type != TITAN_TMOD_SHT_REL && rel_section->type != TITAN_TMOD_SHT_RELA) continue;
        is_rela = rel_section->type == TITAN_TMOD_SHT_RELA;
        entry_size = is_rela ? sizeof(TitanTmodRela) : sizeof(TitanTmodRel);
        if (rel_section->entsize != entry_size || rel_section->size % entry_size != 0u ||
            rel_section->link != module->symtab_section_index || rel_section->info >= module->section_count ||
            !TitanTmod_Bounds(rel_section->offset, rel_section->size, module->file_size)) return 0;
        target_size = module->sections[rel_section->info].size;
        relocation_count = rel_section->size / entry_size;
        for (relocation_index = 0u; relocation_index < relocation_count; relocation_index++)
        {
            uint32_t rel_offset, rel_info; unsigned int symbol_index, type;
            if (is_rela) { const TitanTmodRela *rel=(const TitanTmodRela *)(module->file_data+rel_section->offset)+relocation_index; rel_offset=rel->offset; rel_info=rel->info; }
            else { const TitanTmodRel *rel=(const TitanTmodRel *)(module->file_data+rel_section->offset)+relocation_index; rel_offset=rel->offset; rel_info=rel->info; }
            symbol_index=rel_info>>8u; type=rel_info&0xffu;
            if ((rel_offset&3u)!=0u || target_size<sizeof(uint32_t) || rel_offset>target_size-sizeof(uint32_t)) return 0;
            if (type==TITAN_TMOD_R_MIPS_NONE) continue;
            if (symbol_index>=module->symbol_count || (type!=TITAN_TMOD_R_MIPS_32 && type!=TITAN_TMOD_R_MIPS_26 && type!=TITAN_TMOD_R_MIPS_HI16 && type!=TITAN_TMOD_R_MIPS_LO16)) return 0;
            if (!is_rela && type==TITAN_TMOD_R_MIPS_HI16) { if (pending_count>=TITAN_TMOD_MAX_PENDING_HI16) return 0; pending_symbols[pending_count++]=symbol_index; }
            else if (!is_rela && type==TITAN_TMOD_R_MIPS_LO16) { unsigned int i=0u; while(i<pending_count){ if(pending_symbols[i]==symbol_index){ pending_symbols[i]=pending_symbols[pending_count-1u]; pending_count--; continue;} i++; } }
        }
        if (!is_rela && pending_count!=0u) return 0;
    }
    return 1;
}

static int TitanTmod_ApplyRelocations(TitanTmodLoaded *module, int allow_game_imports)
{
    TitanTmodPendingHi16 pending[TITAN_TMOD_MAX_PENDING_HI16];
    unsigned int section_index;
    for (section_index=0u; section_index<module->section_count; section_index++)
    {
        const TitanTmodSection *rel_section=&module->sections[section_index];
        unsigned int entry_size, relocation_count, relocation_index, target_section_index, target_size, pending_count=0u;
        unsigned char *target_section; int is_rela;
        if (rel_section->type!=TITAN_TMOD_SHT_REL && rel_section->type!=TITAN_TMOD_SHT_RELA) continue;
        is_rela=rel_section->type==TITAN_TMOD_SHT_RELA;
        entry_size=is_rela?sizeof(TitanTmodRela):sizeof(TitanTmodRel);
        if(rel_section->entsize!=entry_size || rel_section->size%entry_size!=0u || rel_section->link!=module->symtab_section_index || rel_section->info>=module->section_count || !TitanTmod_Bounds(rel_section->offset,rel_section->size,module->file_size)) return 0;
        target_section_index=rel_section->info; target_section=module->section_address[target_section_index]; target_size=module->sections[target_section_index].size;
        if(target_section==0) continue;
        relocation_count=rel_section->size/entry_size;
        for(relocation_index=0u; relocation_index<relocation_count; relocation_index++)
        {
            uint32_t rel_offset, rel_info; int32_t explicit_addend=0; unsigned int symbol_index,type; uintptr_t symbol_value; uint32_t *target,word;
            if(is_rela){ const TitanTmodRela *rel=(const TitanTmodRela *)(module->file_data+rel_section->offset)+relocation_index; rel_offset=rel->offset; rel_info=rel->info; explicit_addend=rel->addend; }
            else { const TitanTmodRel *rel=(const TitanTmodRel *)(module->file_data+rel_section->offset)+relocation_index; rel_offset=rel->offset; rel_info=rel->info; }
            symbol_index=rel_info>>8u; type=rel_info&0xffu;
            if((rel_offset&3u)!=0u || target_size<sizeof(uint32_t) || rel_offset>target_size-sizeof(uint32_t)) return 0;
            if(type==TITAN_TMOD_R_MIPS_NONE) continue;
            symbol_value=TitanTmod_ResolveSymbol(module,symbol_index,allow_game_imports);
            if(symbol_value==0u){ const char *name=symbol_index<module->symbol_count?TitanTmod_SymbolName(module,&module->symbols[symbol_index]):0; char error[128]; snprintf(error,sizeof(error),TitanLocalization_Compat("Unresolved N32 import: %.80s"),name?name:TitanLocalization_Compat("<invalid symbol>")); TitanAppRuntime_SetError(error); return 0; }
            target=(uint32_t *)(target_section+rel_offset); word=*target;
            if(type==TITAN_TMOD_R_MIPS_32){ intptr_t addend=is_rela?(intptr_t)explicit_addend:(intptr_t)(int32_t)word; *target=(uint32_t)((intptr_t)symbol_value+addend); }
            else if(type==TITAN_TMOD_R_MIPS_26){ intptr_t addend=is_rela?(intptr_t)explicit_addend:(intptr_t)((word&0x03ffffffu)<<2u); uintptr_t value=(uintptr_t)((intptr_t)symbol_value+addend); if((((uintptr_t)target+4u)&0xf0000000u)!=(value&0xf0000000u)){ TitanAppRuntime_SetError("Relocacao N32 R_MIPS_26 fora da regiao de salto"); return 0;} *target=(word&0xfc000000u)|(((uint32_t)value>>2u)&0x03ffffffu); }
            else if(type==TITAN_TMOD_R_MIPS_HI16){ if(is_rela){ uintptr_t value=(uintptr_t)((intptr_t)symbol_value+(intptr_t)explicit_addend); *target=(word&0xffff0000u)|(((uint32_t)(value+0x8000u)>>16u)&0xffffu); } else { if(pending_count>=TITAN_TMOD_MAX_PENDING_HI16) return 0; pending[pending_count].target=target; pending[pending_count].symbol_index=symbol_index; pending[pending_count].target_section_index=target_section_index; pending[pending_count].addend=(int32_t)((word&0xffffu)<<16u); pending_count++; } }
            else if(type==TITAN_TMOD_R_MIPS_LO16){ if(is_rela){ uintptr_t value=(uintptr_t)((intptr_t)symbol_value+(intptr_t)explicit_addend); *target=(word&0xffff0000u)|((uint32_t)value&0xffffu); } else { int32_t low_addend=(int16_t)(word&0xffffu); uintptr_t value=symbol_value+(intptr_t)low_addend; unsigned int i=0u; *target=(word&0xffff0000u)|((uint32_t)value&0xffffu); while(i<pending_count){ if(pending[i].symbol_index==symbol_index && pending[i].target_section_index==target_section_index){ uintptr_t full_value=symbol_value+(intptr_t)pending[i].addend+(intptr_t)low_addend; uint32_t hi_word=*pending[i].target; *pending[i].target=(hi_word&0xffff0000u)|(((uint32_t)(full_value+0x8000u)>>16u)&0xffffu); pending[i]=pending[pending_count-1u]; pending_count--; continue;} i++; } } }
            else return 0;
        }
        if(!is_rela && pending_count!=0u) return 0;
    }
    return 1;
}

static uintptr_t TitanTmod_FindExport(
    const TitanTmodLoaded *module,
    const char *name,
    int allow_game_imports
)
{
    unsigned int index;

    for (index = 0u; index < module->symbol_count; index++)
    {
        const char *symbol_name;
        const TitanTmodSymbol *symbol;

        symbol = &module->symbols[index];
        if (symbol->shndx == TITAN_TMOD_SHN_UNDEF ||
            (symbol->info & 0x0fu) != TITAN_TMOD_STT_FUNC ||
            symbol->shndx >= module->section_count ||
            module->section_address[symbol->shndx] == 0 ||
            (module->sections[symbol->shndx].flags & TITAN_TMOD_SHF_EXECINSTR) == 0u ||
            symbol->value >= module->sections[symbol->shndx].size ||
            (symbol->value & 3u) != 0u)
        {
            continue;
        }
        symbol_name = TitanTmod_SymbolName(module, symbol);
        if (symbol_name != 0 && strcmp(symbol_name, name) == 0)
        {
            return TitanTmod_ResolveSymbol(module, index, allow_game_imports);
        }
    }
    return 0u;
}

static void TitanTmod_Unload(TitanTmodLoaded *module)
{
    if (module == 0)
    {
        return;
    }
    free(module->allocation);
    free(module->file_data);
    memset(module, 0, sizeof(*module));
}

static int TitanTmod_Load(
    const char *path,
    TitanTmodLoaded *module,
    int load_image,
    int allow_game_imports
)
{
    const TitanTmodElfHeader *header;
    const TitanTmodSection *sections;
    unsigned int section_offsets[TITAN_TMOD_MAX_SECTIONS];
    unsigned int image_size;
    unsigned int symtab_index;
    const TitanTmodSection *symtab;
    const TitanTmodSection *strtab;
    unsigned int index;

    memset(module, 0, sizeof(*module));
    if (!TitanTmod_ReadFile(path, &module->file_data, &module->file_size) ||
        !TitanTmod_ParseHeader(module->file_data, module->file_size, &header, &sections) ||
        !TitanTmod_ValidateSections(
            module->file_data,
            module->file_size,
            header,
            sections,
            &image_size,
            section_offsets,
            &symtab_index
        ))
    {
        TitanTmod_Unload(module);
        return 0;
    }

    module->sections = sections;
    module->section_count = header->shnum;
    symtab = &sections[symtab_index];
    strtab = &sections[symtab->link];
    if (strtab->type != TITAN_TMOD_SHT_STRTAB ||
        !TitanTmod_Bounds(strtab->offset, strtab->size, module->file_size) ||
        symtab->size % sizeof(TitanTmodSymbol) != 0u)
    {
        TitanTmod_Unload(module);
        return 0;
    }
    module->symbols = (const TitanTmodSymbol *)(module->file_data + symtab->offset);
    module->symbol_count = symtab->size / sizeof(TitanTmodSymbol);
    module->symtab_section_index = symtab_index;
    module->strings = (const char *)(module->file_data + strtab->offset);
    module->strings_size = strtab->size;

    if (!TitanTmod_ValidateRelocationTables(module))
    {
        TitanAppRuntime_SetError("Tabela de relocacoes Titan App invalida");
        TitanTmod_Unload(module);
        return 0;
    }

    if (!load_image)
    {
        /* Structural validation also verifies that required exports exist and
         * that the object has no unsupported undefined symbols. */
        int has_init = 0;
        int has_update = 0;
        int has_draw = 0;
        for (index = 0u; index < module->symbol_count; index++)
        {
            const TitanTmodSymbol *symbol;
            const char *name;

            symbol = &module->symbols[index];
            name = TitanTmod_SymbolName(module, symbol);
            if (name == 0)
            {
                TitanTmod_Unload(module);
                return 0;
            }
            if (symbol->shndx == TITAN_TMOD_SHN_UNDEF && symbol->name != 0u &&
                TitanTmod_ResolveExternal(name, allow_game_imports) == 0u)
            {
                TitanTmod_Unload(module);
                return 0;
            }
            if (symbol->shndx != TITAN_TMOD_SHN_UNDEF &&
                symbol->shndx < module->section_count &&
                (symbol->info & 0x0fu) == TITAN_TMOD_STT_FUNC &&
                (module->sections[symbol->shndx].flags &
                    (TITAN_TMOD_SHF_ALLOC | TITAN_TMOD_SHF_EXECINSTR)) ==
                    (TITAN_TMOD_SHF_ALLOC | TITAN_TMOD_SHF_EXECINSTR) &&
                symbol->value < module->sections[symbol->shndx].size &&
                (symbol->value & 3u) == 0u)
            {
                has_init |= strcmp(name, "titan_app_init") == 0;
                has_update |= strcmp(name, "titan_app_update") == 0;
                has_draw |= strcmp(name, "titan_app_draw") == 0;
            }
        }
        TitanTmod_Unload(module);
        return has_init && has_update && has_draw;
    }

    if (!TitanTmod_TargetFlagsCompatible(header))
    {
        TitanTmod_Unload(module);
        return 0;
    }
    if (!TitanTmod_ValidateR5900ExecutableCode(module))
    {
        TitanTmod_Unload(module);
        return 0;
    }

    module->allocation = malloc((size_t)image_size + 63u);
    if (module->allocation == 0)
    {
        TitanTmod_Unload(module);
        return 0;
    }
    module->image = (unsigned char *)(((uintptr_t)module->allocation + 63u) & ~(uintptr_t)63u);
    module->image_size = image_size;
    memset(module->image, 0, image_size);

    for (index = 0u; index < module->section_count; index++)
    {
        const TitanTmodSection *section;

        section = &sections[index];
        if ((section->flags & TITAN_TMOD_SHF_ALLOC) == 0u ||
            section->size == 0u ||
            (section->type != TITAN_TMOD_SHT_PROGBITS &&
             section->type != TITAN_TMOD_SHT_NOBITS))
        {
            /* Metadata such as .reginfo/.MIPS.abiflags may carry SHF_ALLOC,
             * but it is intentionally not part of the relocated runtime image. */
            module->section_address[index] = 0;
            continue;
        }
        module->section_address[index] = module->image + section_offsets[index];
        if (section->type == TITAN_TMOD_SHT_PROGBITS)
        {
            memcpy(module->section_address[index], module->file_data + section->offset, section->size);
        }
    }

    if (!TitanTmod_ApplyRelocations(module, allow_game_imports))
    {
        if (g_app_runtime.last_error[0] == '\0')
            TitanAppRuntime_SetError("Falha ao aplicar relocacoes MIPS do Titan App");
        TitanTmod_Unload(module);
        return 0;
    }

    module->init = (TitanAppInitFn)TitanTmod_FindExport(
        module, "titan_app_init", allow_game_imports
    );
    module->update = (TitanAppUpdateFn)TitanTmod_FindExport(
        module, "titan_app_update", allow_game_imports
    );
    module->draw = (TitanAppDrawFn)TitanTmod_FindExport(
        module, "titan_app_draw", allow_game_imports
    );
    module->shutdown = (TitanAppShutdownFn)TitanTmod_FindExport(
        module, "titan_app_shutdown", allow_game_imports
    );
    if (module->init == 0 || module->update == 0 || module->draw == 0)
    {
        TitanAppRuntime_SetError("Exports Titan App ausentes ou nao executaveis");
        TitanTmod_Unload(module);
        return 0;
    }

#if defined(__EE__) || defined(_EE)
    FlushCache(0);
    FlushCache(2);
#endif
    return 1;
}

static int TitanAppRuntime_RelativeSafe(const char *path)
{
    const char *cursor;

    if (path == 0 || path[0] == '\0' || path[0] == '/' ||
        strchr(path, ':') != 0 || strchr(path, '\\') != 0)
    {
        return 0;
    }
    cursor = path;
    while (*cursor != '\0')
    {
        const char *segment;
        unsigned int length;

        segment = cursor;
        while (*cursor != '\0' && *cursor != '/')
        {
            if ((unsigned char)*cursor < 0x20u)
            {
                return 0;
            }
            cursor++;
        }
        length = (unsigned int)(cursor - segment);
        if (length == 0u || (length == 1u && segment[0] == '.') ||
            (length == 2u && segment[0] == '.' && segment[1] == '.'))
        {
            return 0;
        }
        if (*cursor == '/')
        {
            cursor++;
        }
    }
    return 1;
}

static int TitanAppRuntime_Join(char *output, unsigned int capacity, const char *relative)
{
    int length;

    if (!TitanAppRuntime_RelativeSafe(relative))
    {
        return 0;
    }
    length = snprintf(output, capacity, "%s/%s", g_app_runtime.root_path, relative);
    return length >= 0 && (unsigned int)length < capacity;
}

static int TitanAppRuntime_CreateParents(const char *path)
{
    char copy[TITAN_APP_RUNTIME_PATH_CAPACITY];
    unsigned int root_length;
    unsigned int index;

    TitanAppRuntime_CopyText(copy, sizeof(copy), path);
    root_length = (unsigned int)strlen(g_app_runtime.root_path);
    for (index = root_length + 1u; copy[index] != '\0'; index++)
    {
        if (copy[index] == '/')
        {
            copy[index] = '\0';
            (void)TitanStorageIo_CreateDirectory(copy);
            copy[index] = '/';
        }
    }
    return 1;
}

static void TitanAppApi_RequestExit(void)
{
    if (g_app_runtime.exit_reason == TITAN_APP_EXIT_NONE)
        g_app_runtime.exit_reason = TITAN_APP_EXIT_APP_REQUEST;
    g_app_runtime.exit_requested = 1;
    /* Titan Games may request exit from their worker. Keep diagnostics and
     * lifecycle mutation on the shell thread; the flag itself is the
     * cross-thread handoff. */
    if (g_app_runtime.runtime_class != TITAN_APP_RUNTIME_CLASS_GAME ||
        !g_app_runtime.game_thread_started)
        TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_EXIT_REQUEST);
}

static uint64_t TitanAppApi_UptimeMs(void)
{
    return TitanTime_NowMilliseconds();
}

static int TitanAppRuntime_RangeContains(
    const void *base,
    uint32_t size,
    const void *pointer,
    uint32_t bytes
)
{
    uintptr_t begin;
    uintptr_t end;
    uintptr_t target;
    uintptr_t target_end;

    if (base == 0 || pointer == 0 || bytes == 0u || size < bytes)
    {
        return 0;
    }
    begin = (uintptr_t)base;
    end = begin + (uintptr_t)size;
    target = (uintptr_t)pointer;
    target_end = target + (uintptr_t)bytes;
    if (end < begin || target_end < target)
    {
        return 0;
    }
    return target >= begin && target_end <= end;
}

static int TitanAppRuntime_ModuleSpanOwned(
    const void *pointer,
    uint32_t bytes,
    int require_write
)
{
    unsigned int index;

    if (pointer == 0 || bytes == 0u)
    {
        return 0;
    }
    for (index = 0u; index < g_app_runtime.module.section_count; index++)
    {
        const TitanTmodSection *section = &g_app_runtime.module.sections[index];
        void *address = g_app_runtime.module.section_address[index];
        if (address == 0 || section->size == 0u ||
            (section->flags & TITAN_TMOD_SHF_ALLOC) == 0u ||
            (require_write && (section->flags & TITAN_TMOD_SHF_WRITE) == 0u))
        {
            continue;
        }
        if (TitanAppRuntime_RangeContains(address, section->size, pointer, bytes))
        {
            return 1;
        }
    }
    return 0;
}

static int TitanAppRuntime_AllocationSpanOwned(
    const void *pointer,
    uint32_t bytes
)
{
    unsigned int index;

    for (index = 0u; index < TITAN_APP_RUNTIME_MAX_ALLOCATIONS; index++)
    {
        if (TitanAppRuntime_RangeContains(
                g_app_runtime.allocations[index].pointer,
                g_app_runtime.allocations[index].size,
                pointer,
                bytes))
        {
            return 1;
        }
    }
    return 0;
}

static int TitanAppRuntime_GameThreadStackSpanOwned(
    const void *pointer,
    uint32_t bytes
)
{
#if defined(__EE__) || defined(_EE)
    if (g_app_runtime.runtime_class != TITAN_APP_RUNTIME_CLASS_GAME)
        return 0;
    return TitanAppRuntime_RangeContains(
        g_app_runtime.game_thread_stack,
        sizeof(g_app_runtime.game_thread_stack),
        pointer,
        bytes
    );
#else
    (void)pointer;
    (void)bytes;
    return 0;
#endif
}

static int TitanAppRuntime_ReadableSpan(const void *pointer, uint32_t bytes)
{
    /* A loaded ET_REL module owns the complete packed runtime image, including
     * linker alignment gaps between SHF_ALLOC sections. Some R5900 objects place
     * string literals/constant pools at addresses that are valid inside that
     * packed image but do not map cleanly back to the source section boundary.
     * Treat the complete image as read-only app memory while keeping writes
     * restricted to explicitly writable sections/heap allocations. */
    return TitanAppRuntime_AllocationSpanOwned(pointer, bytes) ||
        TitanAppRuntime_GameThreadStackSpanOwned(pointer, bytes) ||
        TitanAppRuntime_RangeContains(
            g_app_runtime.module.image,
            g_app_runtime.module.image_size,
            pointer,
            bytes
        ) ||
        TitanAppRuntime_RangeContains(
            g_app_runtime.module.file_data,
            g_app_runtime.module.file_size,
            pointer,
            bytes
        );
}

static uint32_t TitanAppRuntime_ReadableCapacity(const void *pointer, uint32_t cap)
{
    uintptr_t target;
    unsigned int index;

    if (pointer == 0 || cap == 0u) return 0u;
    target = (uintptr_t)pointer;
#if defined(__EE__) || defined(_EE)
    /* P164_GAME_STACK_READABLE_CAPACITY */
    if (g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME)
    {
        uintptr_t begin = (uintptr_t)g_app_runtime.game_thread_stack;
        uintptr_t end = begin + (uintptr_t)sizeof(g_app_runtime.game_thread_stack);
        if (end >= begin && target >= begin && target < end)
        {
            uintptr_t available = end - target;
            return available < (uintptr_t)cap ? (uint32_t)available : cap;
        }
    }
#endif
    for (index = 0u; index < TITAN_APP_RUNTIME_MAX_ALLOCATIONS; index++)
    {
        uintptr_t begin = (uintptr_t)g_app_runtime.allocations[index].pointer;
        uintptr_t end = begin + (uintptr_t)g_app_runtime.allocations[index].size;
        if (begin != 0u && end >= begin && target >= begin && target < end)
        {
            uintptr_t available = end - target;
            return available < (uintptr_t)cap ? (uint32_t)available : cap;
        }
    }

    if (g_app_runtime.module.image != 0 && g_app_runtime.module.image_size != 0u)
    {
        uintptr_t begin = (uintptr_t)g_app_runtime.module.image;
        uintptr_t end = begin + (uintptr_t)g_app_runtime.module.image_size;
        if (end >= begin && target >= begin && target < end)
        {
            uintptr_t available = end - target;
            return available < (uintptr_t)cap ? (uint32_t)available : cap;
        }
    }

    /* file_data is kept alive for the lifetime of the module. Allowing it as
     * read-only ownership also covers toolchains that retain a constant-pool
     * reference into the ET_REL backing image after relocation. */
    if (g_app_runtime.module.file_data != 0 && g_app_runtime.module.file_size != 0u)
    {
        uintptr_t begin = (uintptr_t)g_app_runtime.module.file_data;
        uintptr_t end = begin + (uintptr_t)g_app_runtime.module.file_size;
        if (end >= begin && target >= begin && target < end)
        {
            uintptr_t available = end - target;
            return available < (uintptr_t)cap ? (uint32_t)available : cap;
        }
    }
    return 0u;
}

static int TitanAppRuntime_StringOwned(const char *text, uint32_t maximum)
{
    uint32_t capacity;
    uint32_t index;

    capacity = TitanAppRuntime_ReadableCapacity(text, maximum);
    if (capacity == 0u) return 0;
    for (index = 0u; index < capacity; index++)
    {
        if (text[index] == '\0') return 1;
    }
    return 0;
}

static int TitanAppRuntime_WritableSpan(void *pointer, uint32_t bytes)
{
    return TitanAppRuntime_AllocationSpanOwned(pointer, bytes) ||
        TitanAppRuntime_ModuleSpanOwned(pointer, bytes, 1) ||
        TitanAppRuntime_GameThreadStackSpanOwned(pointer, bytes);
}

static void *TitanAppRuntime_AllocDirect(uint32_t bytes)
{
    unsigned int index;
    void *pointer;

    if (bytes == 0u || bytes > TITAN_APP_RUNTIME_MAX_HEAP_BYTES ||
        g_app_runtime.allocation_bytes > TITAN_APP_RUNTIME_MAX_HEAP_BYTES - bytes)
    {
        return 0;
    }
    for (index = 0u; index < TITAN_APP_RUNTIME_MAX_ALLOCATIONS; index++)
    {
        if (g_app_runtime.allocations[index].pointer == 0)
        {
            break;
        }
    }
    if (index == TITAN_APP_RUNTIME_MAX_ALLOCATIONS)
    {
        return 0;
    }

    pointer = malloc(bytes);
    if (pointer == 0)
    {
        return 0;
    }
    g_app_runtime.allocations[index].pointer = pointer;
    g_app_runtime.allocations[index].size = bytes;
    g_app_runtime.allocation_bytes += bytes;
    g_app_runtime.allocation_count++;
    return pointer;
}

static int TitanAppRuntime_ReleaseDirect(void *pointer)
{
    unsigned int index;

    if (pointer == 0)
    {
        return 1;
    }
    for (index = 0u; index < TITAN_APP_RUNTIME_MAX_ALLOCATIONS; index++)
    {
        if (g_app_runtime.allocations[index].pointer == pointer)
        {
            uint32_t size = g_app_runtime.allocations[index].size;
            free(pointer);
            g_app_runtime.allocations[index].pointer = 0;
            g_app_runtime.allocations[index].size = 0u;
            if (g_app_runtime.allocation_bytes >= size)
                g_app_runtime.allocation_bytes -= size;
            else
                g_app_runtime.allocation_bytes = 0u;
            if (g_app_runtime.allocation_count != 0u)
                g_app_runtime.allocation_count--;
            return 1;
        }
    }

    /* Never feed an unowned pointer to libc free(). A malformed in-process app
     * must not turn one bad API call into corruption of the Titan heap. */
    TitanAppRuntime_SetError("Titan App tentou liberar ponteiro nao pertencente ao app");
    return 0;
}

static void TitanAppRuntime_ReleaseAllAllocations(void)
{
    unsigned int index;

    for (index = 0u; index < TITAN_APP_RUNTIME_MAX_ALLOCATIONS; index++)
    {
        if (g_app_runtime.allocations[index].pointer != 0)
        {
            free(g_app_runtime.allocations[index].pointer);
            g_app_runtime.allocations[index].pointer = 0;
            g_app_runtime.allocations[index].size = 0u;
        }
    }
    g_app_runtime.allocation_bytes = 0u;
    g_app_runtime.allocation_count = 0u;
}

static void *TitanAppApi_Alloc(uint32_t bytes)
{
    /* ABI v1.1 safety rule: titan_app_init is a pure, non-blocking phase.
     * Never bridge allocator work between the init thread and the shell thread;
     * a bad/slow module must not be able to wedge the EE scheduler before the
     * launch watchdog and input path run again.  Apps allocate from update(). */
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_SetError("Titan App tentou alocar memoria durante titan_app_init");
        return 0;
    }
    return TitanAppRuntime_AllocDirect(bytes);
}

static void TitanAppApi_Release(void *pointer)
{
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_SetError("Titan App tentou liberar memoria durante titan_app_init");
        return;
    }
    (void)TitanAppRuntime_ReleaseDirect(pointer);
}

static int TitanAppRuntime_SaneFloat(float value, float limit)
{
    return value == value && value >= -limit && value <= limit;
}

static int TitanAppRuntime_SanePoint(float x, float y)
{
    return TitanAppRuntime_SaneFloat(x, TITAN_APP_RUNTIME_COORD_LIMIT) &&
        TitanAppRuntime_SaneFloat(y, TITAN_APP_RUNTIME_COORD_LIMIT) &&
        x >= 0.0f && y >= 0.0f &&
        x <= (float)TITAN_APP_LOGICAL_WIDTH &&
        y <= (float)TITAN_APP_LOGICAL_HEIGHT;
}

static int TitanAppRuntime_SaneRect(float x, float y, float width, float height)
{
    return TitanAppRuntime_SanePoint(x, y) &&
        TitanAppRuntime_SaneFloat(width, TITAN_APP_RUNTIME_COORD_LIMIT) &&
        TitanAppRuntime_SaneFloat(height, TITAN_APP_RUNTIME_COORD_LIMIT) &&
        width > 0.0f && height > 0.0f &&
        width <= (float)TITAN_APP_LOGICAL_WIDTH &&
        height <= (float)TITAN_APP_LOGICAL_HEIGHT &&
        x + width <= (float)TITAN_APP_LOGICAL_WIDTH + 0.5f &&
        y + height <= (float)TITAN_APP_LOGICAL_HEIGHT + 0.5f;
}

static int TitanAppRuntime_SaneText(float x, float y, float scale, const char *text)
{
    return TitanAppRuntime_StringOwned(text, 512u) &&
        TitanAppRuntime_SanePoint(x, y) &&
        TitanAppRuntime_SaneFloat(scale, TITAN_APP_RUNTIME_SCALE_LIMIT) &&
        scale > 0.0f;
}

static void TitanAppRuntime_RejectDraw(void)
{
    if (g_app_runtime.rejected_draw_calls != 0xffffffffu)
    {
        g_app_runtime.rejected_draw_calls++;
    }
}

static void TitanAppRuntime_ReportRejectedDraws(void)
{
    if (g_app_runtime.rejected_draw_calls == 0u ||
        g_app_runtime.draw_rejection_reported)
    {
        return;
    }
    g_app_runtime.draw_rejection_reported = 1;
    (void)TitanSystemError_Raise(
        TitanLocalization_Compat("Titan App renderer"),
        TitanLocalization_Compat(
            "O aplicativo enviou comandos de desenho invalidos; a interface foi protegida."
        )
    );
}

static void TitanAppRuntime_MapPoint(float *x, float *y)
{
    if (!g_app_runtime.fullscreen)
    {
        *x = TITAN_APP_WINDOW_X + (*x * TITAN_APP_WINDOW_SCALE);
        *y = TITAN_APP_WINDOW_Y + (*y * TITAN_APP_WINDOW_SCALE);
    }
}

static void TitanAppRuntime_MapRect(float *x, float *y, float *width, float *height)
{
    TitanAppRuntime_MapPoint(x, y);
    if (!g_app_runtime.fullscreen)
    {
        *width *= TITAN_APP_WINDOW_SCALE;
        *height *= TITAN_APP_WINDOW_SCALE;
    }
}

static float TitanAppRuntime_MapScale(float scale)
{
    return g_app_runtime.fullscreen ? scale : scale * TITAN_APP_WINDOW_SCALE;
}

static void TitanAppApi_DrawText(
    float x, float y, float scale, const char *text,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha
)
{
    if (g_app_runtime.launching) return;
    if (!TitanAppRuntime_SaneText(x, y, scale, text))
    {
        TitanAppRuntime_RejectDraw();
        return;
    }
    TitanAppRuntime_MapPoint(&x, &y);
    TitanFont_DrawTextAlpha(
        x, y, TitanAppRuntime_MapScale(scale), text,
        red, green, blue, alpha
    );
}

static void TitanAppApi_DrawTextShadow(
    float x, float y, float scale, const char *text,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha
)
{
    if (g_app_runtime.launching) return;
    if (!TitanAppRuntime_SaneText(x, y, scale, text))
    {
        TitanAppRuntime_RejectDraw();
        return;
    }
    TitanAppRuntime_MapPoint(&x, &y);
    TitanFont_DrawTextShadow(
        x, y, TitanAppRuntime_MapScale(scale), text,
        red, green, blue, alpha
    );
}

static void TitanAppApi_DrawRect(
    float x, float y, float width, float height,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha
)
{
    if (g_app_runtime.launching) return;
    if (!TitanAppRuntime_SaneRect(x, y, width, height))
    {
        TitanAppRuntime_RejectDraw();
        return;
    }
    TitanAppRuntime_MapRect(&x, &y, &width, &height);
    TitanVideo_DrawRectAlpha(x, y, width, height, red, green, blue, alpha);
}

static void TitanAppApi_DrawPanel(float x, float y, float width, float height, uint8_t alpha)
{
    float radius;

    if (g_app_runtime.launching) return;
    if (!TitanAppRuntime_SaneRect(x, y, width, height))
    {
        TitanAppRuntime_RejectDraw();
        return;
    }
    TitanAppRuntime_MapRect(&x, &y, &width, &height);
    radius = g_app_runtime.fullscreen ? 8.0f : 8.0f * TITAN_APP_WINDOW_SCALE;
    TitanVideo_DrawRoundedRectAlpha(
        x, y, width, height, radius,
        5, 7, 12, (unsigned char)((214u * alpha) / 255u)
    );
    TitanVideo_DrawLineAlpha(x, y, x + width, y, 1.0f, 222, 43, 82, alpha);
}

static void TitanAppApi_DrawProgress(
    float x, float y, float width, float height,
    uint32_t percent, uint8_t alpha
)
{
    float filled;

    if (g_app_runtime.launching) return;
    if (!TitanAppRuntime_SaneRect(x, y, width, height))
    {
        TitanAppRuntime_RejectDraw();
        return;
    }
    TitanAppRuntime_MapRect(&x, &y, &width, &height);
    if (percent > 100u) percent = 100u;
    TitanVideo_DrawRoundedRectAlpha(x, y, width, height, height * 0.5f, 30, 34, 44, alpha);
    filled = width * ((float)percent / 100.0f);
    if (filled > 0.0f)
    {
        TitanVideo_DrawRoundedRectAlpha(x, y, filled, height, height * 0.5f, 222, 43, 82, alpha);
    }
}

static int TitanAppRuntime_NotifyDirect(
    TitanAppNotificationKind kind,
    const char *title,
    const char *message
)
{
    TitanNotificationKind mapped;

    mapped = TITAN_NOTIFICATION_INFO;
    if (kind == TITAN_APP_NOTIFICATION_ACTIVITY) mapped = TITAN_NOTIFICATION_ACTIVITY;
    else if (kind == TITAN_APP_NOTIFICATION_SUCCESS) mapped = TITAN_NOTIFICATION_SUCCESS;
    else if (kind == TITAN_APP_NOTIFICATION_WARNING) mapped = TITAN_NOTIFICATION_WARNING;
    else if (kind == TITAN_APP_NOTIFICATION_ERROR) mapped = TITAN_NOTIFICATION_ERROR;
    return TitanNotifications_Push(mapped, title, message);
}

static int TitanAppApi_Notify(
    TitanAppNotificationKind kind,
    const char *title,
    const char *message
)
{
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_SetError("Titan App tentou notificar durante titan_app_init");
        return 0;
    }
    if (!TitanAppRuntime_StringOwned(title, 80u) ||
        !TitanAppRuntime_StringOwned(message, 192u))
    {
        return 0;
    }
    return TitanAppRuntime_NotifyDirect(kind, title, message);
}

static int TitanAppRuntime_FileReadDirect(
    const char *relative_path,
    uint32_t offset,
    void *buffer,
    uint32_t capacity
)
{
    char path[TITAN_APP_RUNTIME_PATH_CAPACITY];
    int descriptor;
    TitanStorageIoResult result;

    if (buffer == 0 || capacity == 0u ||
        !TitanAppRuntime_WritableSpan(buffer, capacity) ||
        !TitanAppRuntime_Join(path, sizeof(path), relative_path))
    {
        return -1;
    }
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        return -1;
    }
    if (TitanStorageIo_Seek64(descriptor, offset, TITAN_STORAGE_IO_SEEK_SET) != (TitanStorageIoOffset)offset)
    {
        TitanStorageIo_Close(descriptor);
        return -1;
    }
    result = TitanStorageIo_ReadSome(descriptor, buffer, capacity);
    TitanStorageIo_Close(descriptor);
    return result >= 0 ? (int)result : -1;
}

static int TitanAppApi_FileRead(
    const char *relative_path,
    uint32_t offset,
    void *buffer,
    uint32_t capacity
)
{
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_SetError("Titan App tentou ler storage durante titan_app_init");
        return -1;
    }
    if (!TitanAppRuntime_StringOwned(relative_path, TITAN_APP_RUNTIME_PATH_CAPACITY)) return -1;
    return TitanAppRuntime_FileReadDirect(relative_path, offset, buffer, capacity);
}

static int TitanAppRuntime_FileWriteDirect(
    const char *relative_path,
    const void *buffer,
    uint32_t size
)
{
    char path[TITAN_APP_RUNTIME_PATH_CAPACITY];
    char temporary[TITAN_APP_RUNTIME_PATH_CAPACITY];
    char backup[TITAN_APP_RUNTIME_PATH_CAPACITY];
    int descriptor;
    int length;

    if (buffer == 0 || size == 0u || relative_path == 0 ||
        !TitanAppRuntime_ReadableSpan(buffer, size) ||
        strncmp(relative_path, "data/", 5u) != 0 ||
        !TitanAppRuntime_Join(path, sizeof(path), relative_path))
    {
        return -1;
    }
    length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (length < 0 || (unsigned int)length >= sizeof(temporary)) return -1;
    length = snprintf(backup, sizeof(backup), "%s.bak", path);
    if (length < 0 || (unsigned int)length >= sizeof(backup)) return -1;

    TitanAppRuntime_CreateParents(path);
    TitanStorageIo_Remove(temporary);
    descriptor = TitanStorageIo_Open(
        temporary,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
    {
        return -1;
    }
    if (!TitanStorageIo_WriteExact(descriptor, buffer, size) ||
        TitanStorageIo_Flush(descriptor) < 0 || TitanStorageIo_Close(descriptor) < 0)
    {
        TitanStorageIo_Close(descriptor);
        TitanStorageIo_Remove(temporary);
        return -1;
    }
    return TitanStorageIo_AtomicReplace(temporary, path, backup) ? (int)size : -1;
}

static int TitanAppApi_FileWriteData(
    const char *relative_path,
    const void *buffer,
    uint32_t size
)
{
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_SetError("Titan App tentou gravar storage durante titan_app_init");
        return -1;
    }
    if (!TitanAppRuntime_StringOwned(relative_path, TITAN_APP_RUNTIME_PATH_CAPACITY)) return -1;
    return TitanAppRuntime_FileWriteDirect(relative_path, buffer, size);
}

static int TitanAppRuntime_FileExistsDirect(const char *relative_path)
{
    char path[TITAN_APP_RUNTIME_PATH_CAPACITY];
    return TitanAppRuntime_Join(path, sizeof(path), relative_path) &&
        TitanStorageIo_PathInfo64(path, 0, 0);
}

static int TitanAppApi_FileExists(const char *relative_path)
{
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_SetError("Titan App tentou consultar storage durante titan_app_init");
        return 0;
    }
    if (!TitanAppRuntime_StringOwned(relative_path, TITAN_APP_RUNTIME_PATH_CAPACITY)) return 0;
    return TitanAppRuntime_FileExistsDirect(relative_path);
}

static int TitanAppRuntime_StoragePrefixAllowed(const char *path, int writing)
{
    const char *cursor;

    if (path == 0 || path[0] == '\0') return 0;
    if (strncmp(path, "mass:/", 6u) == 0) return 1;
    if (strncmp(path, "mass", 4u) == 0 &&
        path[4] >= '0' && path[4] <= '9' &&
        path[5] == ':' && path[6] == '/') return 1;
    if (strncmp(path, "mc0:/", 5u) == 0 || strncmp(path, "mc1:/", 5u) == 0)
        return 1;
    if (!writing && strncmp(path, "cdfs:/", 6u) == 0) return 1;
    if (!writing && strncmp(path, "hdd0:", 5u) == 0) return 1;
    if (strncmp(path, "pfs", 3u) == 0 &&
        path[3] >= '0' && path[3] < (char)('0' + TITAN_STORAGE_MOUNT_MAX_SLOTS) &&
        path[4] == ':' && path[5] == '/')
    {
        TitanStorageMountInfo mount;
        uint32_t slot = (uint32_t)(path[3] - '0');
        uint32_t need = writing ? TITAN_STORAGE_MOUNT_CAP_WRITE : TITAN_STORAGE_MOUNT_CAP_READ;
        if (TitanStorageMount_Get(slot, &mount) && mount.state == TITAN_STORAGE_MOUNT_ONLINE &&
            (mount.capabilities & need) == need) return 1;
    }

    /* Keep ROM/host/pfs/vmc namespaces under the kernel unless a PFS slot is
     * explicitly ONLINE through TitanStorageMount above. A Titan App must not
     * inherit the broad device ownership historically assumed by standalone
     * homebrew. */
    cursor = path;
    (void)cursor;
    return 0;
}

static int TitanAppRuntime_StoragePathSafe(const char *path, int writing)
{
    unsigned int index;

    if (!TitanAppRuntime_StringOwned(path, TITAN_APP_RUNTIME_PATH_CAPACITY) ||
        !TitanAppRuntime_StoragePrefixAllowed(path, writing))
        return 0;
    for (index = 0u; path[index] != '\0'; index++)
    {
        if (path[index] == '\\' || path[index] == '\r' || path[index] == '\n')
            return 0;
        if (path[index] == '.' && path[index + 1u] == '.' &&
            (index == 0u || path[index - 1u] == '/' || path[index - 1u] == ':') &&
            (path[index + 2u] == '\0' || path[index + 2u] == '/'))
            return 0;
    }
    return 1;
}

static int TitanAppRuntime_StorageRootPath(const char *path)
{
    unsigned int length;
    if (path == 0) return 0;
    length = (unsigned int)strlen(path);
    if (strcmp(path, "mass:/") == 0 || strcmp(path, "mc0:/") == 0 ||
        strcmp(path, "mc1:/") == 0 || strcmp(path, "cdfs:/") == 0 ||
        strcmp(path, "hdd0:") == 0)
        return 1;
    if (length == 6u && strncmp(path, "pfs", 3u) == 0 &&
        path[3] >= '0' && path[3] < (char)('0' + TITAN_STORAGE_MOUNT_MAX_SLOTS) &&
        path[4] == ':' && path[5] == '/') return 1;
    return length == 7u && strncmp(path, "mass", 4u) == 0 &&
        path[4] >= '0' && path[4] <= '9' && path[5] == ':' && path[6] == '/';
}

static int TitanAppApi_StorageList(
    const char *absolute_path,
    uint32_t start_index,
    TitanAppStorageEntry *entries,
    uint32_t capacity
)
{
    TitanStorageIoEntry temporary[16];
    uint32_t total;

    if (g_app_runtime.launching || capacity == 0u || capacity > 128u ||
        !TitanAppRuntime_StoragePathSafe(absolute_path, 0) ||
        !TitanAppRuntime_WritableSpan(entries, capacity * (uint32_t)sizeof(*entries)))
        return -1;

    total = 0u;
    while (total < capacity)
    {
        uint32_t batch = capacity - total;
        int count;
        unsigned int index;
        if (batch > 16u) batch = 16u;
        count = TitanStorageIo_ListDirectory(
            absolute_path, start_index + total, temporary, batch);
        if (count < 0) return total != 0u ? (int)total : -1;
        if (count == 0) break;
        for (index = 0u; index < (unsigned int)count; index++)
        {
            TitanAppStorageEntry *out = &entries[total + index];
            memset(out, 0, sizeof(*out));
            TitanAppRuntime_CopyText(out->name, sizeof(out->name), temporary[index].name);
            out->size = temporary[index].size;
            if ((temporary[index].flags & TITAN_STORAGE_IO_ENTRY_DIRECTORY) != 0u)
                out->flags |= TITAN_APP_STORAGE_ENTRY_DIRECTORY;
            if (!TitanAppRuntime_StoragePrefixAllowed(absolute_path, 1))
                out->flags |= TITAN_APP_STORAGE_ENTRY_READ_ONLY;
        }
        total += (uint32_t)count;
        if ((uint32_t)count < batch) break;
    }
    return (int)total;
}

static int TitanAppApi_StorageStat(
    const char *absolute_path,
    TitanAppStorageEntry *entry
)
{
    int is_directory;
    TitanStorageIoSize size;
    const char *name;

    if (g_app_runtime.launching ||
        !TitanAppRuntime_StoragePathSafe(absolute_path, 0) ||
        !TitanAppRuntime_WritableSpan(entry, sizeof(*entry)) ||
        !TitanStorageIo_PathInfo64(absolute_path, &is_directory, &size))
        return 0;
    memset(entry, 0, sizeof(*entry));
    name = strrchr(absolute_path, '/');
    if (name == 0) name = strrchr(absolute_path, ':');
    if (name != 0 && name[1] != '\0') name++;
    else name = absolute_path;
    TitanAppRuntime_CopyText(entry->name, sizeof(entry->name), name);
    entry->size = size;
    if (is_directory) entry->flags |= TITAN_APP_STORAGE_ENTRY_DIRECTORY;
    if (!TitanAppRuntime_StoragePrefixAllowed(absolute_path, 1))
        entry->flags |= TITAN_APP_STORAGE_ENTRY_READ_ONLY;
    return 1;
}

static int TitanAppApi_StorageRead(
    const char *absolute_path,
    uint64_t offset,
    void *buffer,
    uint32_t capacity
)
{
    int descriptor;
    TitanStorageIoResult result;

    if (g_app_runtime.launching || capacity == 0u ||
        !TitanAppRuntime_StoragePathSafe(absolute_path, 0) ||
        !TitanAppRuntime_WritableSpan(buffer, capacity))
        return -1;
    descriptor = TitanStorageIo_Open(absolute_path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0) return -1;
    if (TitanStorageIo_Seek64(descriptor, (TitanStorageIoOffset)offset,
            TITAN_STORAGE_IO_SEEK_SET) != (TitanStorageIoOffset)offset)
    {
        TitanStorageIo_Close(descriptor);
        return -1;
    }
    result = TitanStorageIo_ReadSome(descriptor, buffer, capacity);
    TitanStorageIo_Close(descriptor);
    return result >= 0 && result <= INT_MAX ? (int)result : -1;
}

static int TitanAppApi_StorageWrite(
    const char *absolute_path,
    uint64_t offset,
    const void *buffer,
    uint32_t size,
    uint32_t flags
)
{
    int descriptor;
    int open_flags;
    TitanStorageIoResult result;

    if (g_app_runtime.launching ||
        (size != 0u && !TitanAppRuntime_ReadableSpan(buffer, size)) ||
        !TitanAppRuntime_StoragePathSafe(absolute_path, 1) ||
        (flags & ~(TITAN_APP_STORAGE_WRITE_CREATE | TITAN_APP_STORAGE_WRITE_TRUNCATE)) != 0u ||
        ((flags & TITAN_APP_STORAGE_WRITE_TRUNCATE) != 0u && offset != 0u))
        return -1;
    open_flags = TITAN_STORAGE_IO_WRITE;
    if ((flags & TITAN_APP_STORAGE_WRITE_CREATE) != 0u)
        open_flags |= TITAN_STORAGE_IO_CREATE;
    if ((flags & TITAN_APP_STORAGE_WRITE_TRUNCATE) != 0u)
        open_flags |= TITAN_STORAGE_IO_TRUNCATE;
    descriptor = TitanStorageIo_Open(absolute_path, open_flags);
    if (descriptor < 0) return -1;
    if (TitanStorageIo_Seek64(descriptor, (TitanStorageIoOffset)offset,
            TITAN_STORAGE_IO_SEEK_SET) != (TitanStorageIoOffset)offset)
    {
        TitanStorageIo_Close(descriptor);
        return -1;
    }
    if (size == 0u)
    {
        result = 0;
    }
    else
    {
        result = TitanStorageIo_WriteSome(descriptor, buffer, size);
    }
    if (result >= 0) (void)TitanStorageIo_Flush(descriptor);
    TitanStorageIo_Close(descriptor);
    return result >= 0 && result <= INT_MAX ? (int)result : -1;
}

static int TitanAppApi_StorageMkdir(const char *absolute_path)
{
    if (g_app_runtime.launching || TitanAppRuntime_StorageRootPath(absolute_path) ||
        !TitanAppRuntime_StoragePathSafe(absolute_path, 1)) return 0;
    return TitanStorageIo_CreateDirectory(absolute_path) >= 0;
}

static int TitanAppApi_StorageRemove(const char *absolute_path)
{
    if (g_app_runtime.launching || TitanAppRuntime_StorageRootPath(absolute_path) ||
        !TitanAppRuntime_StoragePathSafe(absolute_path, 1)) return 0;
    return TitanStorageIo_RemoveAny(absolute_path) >= 0;
}

static int TitanAppApi_StorageRename(const char *source, const char *destination)
{
    if (g_app_runtime.launching || TitanAppRuntime_StorageRootPath(source) ||
        !TitanAppRuntime_StoragePathSafe(source, 1) ||
        !TitanAppRuntime_StoragePathSafe(destination, 1)) return 0;
    return TitanStorageIo_Rename(source, destination) >= 0;
}

static int TitanAppApi_StorageListVolumes(
    uint32_t start_index,
    TitanAppStorageVolume *volumes,
    uint32_t capacity
)
{
    static const char *const roots[] = {
        "mass0:/", "mass1:/", "mass2:/", "mass3:/", "mass4:/",
        "mass5:/", "mass6:/", "mass7:/", "mass8:/", "mass9:/",
        "mc0:/", "mc1:/", "cdfs:/", "hdd0:", "mass:/"
    };
    uint32_t logical = 0u, produced = 0u, i;
    int explicit_mass = 0;
    if (volumes == 0 || capacity == 0u ||
        !TitanAppRuntime_WritableSpan(volumes, capacity * (uint32_t)sizeof(*volumes)))
        return -1;
    for (i = 0u; i < 10u; i++)
    {
        TitanStorageIoEntry probe;
        if (TitanStorageIo_ListDirectory(roots[i], 0u, &probe, 1u) >= 0)
            explicit_mass = 1;
    }
    for (i = 0u; i < (uint32_t)(sizeof(roots)/sizeof(roots[0])) && produced < capacity; i++)
    {
        TitanStorageIoEntry probe;
        int online;
        uint32_t flags;
        if (i == 14u && explicit_mass) continue;
        online = TitanStorageIo_ListDirectory(roots[i], 0u, &probe, 1u) >= 0;
        if (!online) continue;
        if (logical++ < start_index) continue;
        memset(&volumes[produced], 0, sizeof(volumes[produced]));
        TitanAppRuntime_CopyText(volumes[produced].root,
            sizeof(volumes[produced].root), roots[i]);
        flags = TITAN_APP_STORAGE_VOLUME_ONLINE | TITAN_APP_STORAGE_VOLUME_READ;
        if (strncmp(roots[i], "cdfs:/", 6u) == 0 || strncmp(roots[i], "hdd0:", 5u) == 0)
            flags |= TITAN_APP_STORAGE_VOLUME_READ_ONLY;
        else
            flags |= TITAN_APP_STORAGE_VOLUME_WRITE;
        volumes[produced].flags = flags;
        produced++;
    }
    for (i = 0u; i < TITAN_STORAGE_MOUNT_MAX_SLOTS && produced < capacity; ++i)
    {
        TitanStorageMountInfo mount;
        uint32_t flags;
        if (!TitanStorageMount_Get(i, &mount) || mount.state != TITAN_STORAGE_MOUNT_ONLINE) continue;
        if (logical++ < start_index) continue;
        memset(&volumes[produced], 0, sizeof(volumes[produced]));
        TitanAppRuntime_CopyText(volumes[produced].root, sizeof(volumes[produced].root), mount.root);
        flags = TITAN_APP_STORAGE_VOLUME_ONLINE;
        if ((mount.capabilities & TITAN_STORAGE_MOUNT_CAP_READ) != 0u) flags |= TITAN_APP_STORAGE_VOLUME_READ;
        if ((mount.capabilities & TITAN_STORAGE_MOUNT_CAP_WRITE) != 0u) flags |= TITAN_APP_STORAGE_VOLUME_WRITE;
        else flags |= TITAN_APP_STORAGE_VOLUME_READ_ONLY;
        volumes[produced].flags = flags;
        produced++;
    }
    return (int)produced;
}

static int TitanAppApi_StorageCommitReplace(
    const char *temporary_path,
    const char *target_path
)
{
    if (!TitanAppRuntime_StoragePathSafe(temporary_path, 1) ||
        !TitanAppRuntime_StoragePathSafe(target_path, 1)) return 0;
    return TitanStorageIo_AtomicReplace(temporary_path, target_path, 0);
}

static void TitanAppRuntime_TextConfirmed(const char *text, void *user_data)
{
    (void)user_data;
    TitanAppRuntime_CopyText(g_app_runtime.text_input_value,
        sizeof(g_app_runtime.text_input_value), text);
    g_app_runtime.text_input_result = TITAN_APP_TEXT_INPUT_CONFIRMED;
    g_app_runtime.text_input_owned = 0;
}

static void TitanAppRuntime_TextCancelled(void *user_data)
{
    (void)user_data;
    g_app_runtime.text_input_value[0] = '\0';
    g_app_runtime.text_input_result = TITAN_APP_TEXT_INPUT_CANCELLED;
    g_app_runtime.text_input_owned = 0;
}

static int TitanAppApi_TextInputOpen(
    const char *title,
    const char *initial_text,
    uint32_t maximum_codepoints
)
{
    TitanTextInputRequest request;
    if (!g_app_runtime.active || g_app_runtime.text_input_owned ||
        !TitanAppRuntime_StringOwned(title, 95u) ||
        (initial_text != 0 && !TitanAppRuntime_StringOwned(initial_text, 255u))) return 0;
    memset(&request, 0, sizeof(request));
    request.title = title;
    request.initial_text = initial_text != 0 ? initial_text : "";
    request.placeholder = "";
    request.maximum_codepoints = maximum_codepoints == 0u || maximum_codepoints > 255u
        ? 255u : maximum_codepoints;
    request.field_type = TITAN_TEXT_INPUT_FIELD_TEXT;
    request.confirm_callback = TitanAppRuntime_TextConfirmed;
    request.cancel_callback = TitanAppRuntime_TextCancelled;
    if (!TitanTextInput_Open(&request)) return 0;
    g_app_runtime.text_input_value[0] = '\0';
    g_app_runtime.text_input_result = TITAN_APP_TEXT_INPUT_PENDING;
    g_app_runtime.text_input_owned = 1;
    return 1;
}

static int TitanAppApi_TextInputPoll(char *text, uint32_t capacity)
{
    int result = g_app_runtime.text_input_result;
    if (result == TITAN_APP_TEXT_INPUT_CONFIRMED)
    {
        if (text == 0 || capacity == 0u ||
            !TitanAppRuntime_WritableSpan(text, capacity)) return TITAN_APP_TEXT_INPUT_PENDING;
        TitanAppRuntime_CopyText(text, capacity, g_app_runtime.text_input_value);
        g_app_runtime.text_input_result = TITAN_APP_TEXT_INPUT_PENDING;
    }
    else if (result == TITAN_APP_TEXT_INPUT_CANCELLED)
        g_app_runtime.text_input_result = TITAN_APP_TEXT_INPUT_PENDING;
    return result;
}

static int TitanAppApi_NetworkGetBegin(
    const char *url, uint32_t maximum_bytes, uint32_t deadline_ms
)
{
    if (!g_app_runtime.active || g_app_runtime.launching) return 0;
    return TitanAppNetwork_BeginGet(url, maximum_bytes, deadline_ms);
}

static int TitanAppApi_NetworkGetStatus(TitanAppNetworkResult *result)
{
    if (!g_app_runtime.active || result == 0) return 0;
    return TitanAppNetwork_GetStatus(result);
}

static int TitanAppApi_NetworkGetRead(uint32_t offset, void *buffer, uint32_t capacity)
{
    if (!g_app_runtime.active || buffer == 0 || capacity == 0u) return -1;
    return TitanAppNetwork_Read(offset, buffer, capacity);
}

static void TitanAppApi_NetworkCancel(void)
{
    TitanAppNetwork_Cancel();
}

static int TitanAppApi_FtpStart(const char *absolute_root, uint16_t port)
{
    if (!g_app_runtime.active || g_app_runtime.launching ||
        !TitanAppRuntime_StoragePathSafe(absolute_root, 1)) return 0;
    return TitanFtpService_Start(absolute_root, port);
}

static void TitanAppApi_FtpStop(void)
{
    TitanFtpService_Stop();
}

static int TitanAppApi_FtpStatus(TitanAppFtpStatus *status)
{
    TitanFtpServiceSnapshot snapshot;
    if (!g_app_runtime.active || status == 0 ||
        !TitanAppRuntime_WritableSpan(status, sizeof(*status))) return 0;
    TitanFtpService_GetSnapshot(&snapshot);
    memset(status, 0, sizeof(*status));
    status->state = (uint32_t)snapshot.state;
    status->sessions_accepted = snapshot.sessions_accepted;
    status->commands_processed = snapshot.commands_processed;
    status->commands_rejected = snapshot.commands_rejected;
    status->control_port = snapshot.control_port;
    return 1;
}


static int TitanAppApi_VmcListActiveRoot(uint32_t index, TitanAppVmcEntry *entry)
{
    const TitanUserProfile *profile;
    TitanVmcDirectoryEntry vmc_entry;
    int result;
    if (!g_app_runtime.active || entry == 0 ||
        !TitanAppRuntime_WritableSpan(entry, sizeof(*entry))) return -1;
    profile = TitanUserProfiles_GetActive();
    if (profile == 0) return -1;
    result = TitanVmc_ListRoot(profile, index, &vmc_entry);
    if (result != 1)
    {
        if (result == 0) memset(entry, 0, sizeof(*entry));
        return result;
    }
    memset(entry, 0, sizeof(*entry));
    TitanAppRuntime_CopyText(entry->name, sizeof(entry->name), vmc_entry.name);
    entry->size = vmc_entry.size;
    if ((vmc_entry.flags & TITAN_VMC_DIRECTORY_ENTRY_DIRECTORY) != 0u)
        entry->flags |= TITAN_APP_VMC_ENTRY_DIRECTORY;
    if ((vmc_entry.flags & TITAN_VMC_DIRECTORY_ENTRY_READABLE) != 0u)
        entry->flags |= TITAN_APP_VMC_ENTRY_READABLE;
    if ((vmc_entry.flags & TITAN_VMC_DIRECTORY_ENTRY_WRITABLE) != 0u)
        entry->flags |= TITAN_APP_VMC_ENTRY_WRITABLE;
    if ((vmc_entry.flags & TITAN_VMC_DIRECTORY_ENTRY_HIDDEN) != 0u)
        entry->flags |= TITAN_APP_VMC_ENTRY_HIDDEN;
    return 1;
}

static int TitanAppApi_VmcListActivePath(
    const char *relative_path,
    uint32_t index,
    TitanAppVmcEntry *entry
)
{
    const TitanUserProfile *profile;
    TitanVmcDirectoryEntry vmc_entry;
    char safe_path[256];
    int result;
    if (!g_app_runtime.active || relative_path == 0 || entry == 0 ||
        !TitanAppRuntime_StringOwned(relative_path, sizeof(safe_path)) ||
        !TitanAppRuntime_WritableSpan(entry, sizeof(*entry))) return -1;
    TitanAppRuntime_CopyText(safe_path, sizeof(safe_path), relative_path);
    profile = TitanUserProfiles_GetActive();
    if (profile == 0) return -1;
    result = TitanVmc_ListDirectory(profile, safe_path, index, &vmc_entry);
    if (result != 1)
    {
        if (result == 0) memset(entry, 0, sizeof(*entry));
        return result;
    }
    memset(entry, 0, sizeof(*entry));
    TitanAppRuntime_CopyText(entry->name, sizeof(entry->name), vmc_entry.name);
    entry->size = vmc_entry.size;
    if ((vmc_entry.flags & TITAN_VMC_DIRECTORY_ENTRY_DIRECTORY) != 0u)
        entry->flags |= TITAN_APP_VMC_ENTRY_DIRECTORY;
    if ((vmc_entry.flags & TITAN_VMC_DIRECTORY_ENTRY_READABLE) != 0u)
        entry->flags |= TITAN_APP_VMC_ENTRY_READABLE;
    if ((vmc_entry.flags & TITAN_VMC_DIRECTORY_ENTRY_WRITABLE) != 0u)
        entry->flags |= TITAN_APP_VMC_ENTRY_WRITABLE;
    if ((vmc_entry.flags & TITAN_VMC_DIRECTORY_ENTRY_HIDDEN) != 0u)
        entry->flags |= TITAN_APP_VMC_ENTRY_HIDDEN;
    return 1;
}

static int TitanAppApi_VmcReadActiveFile(
    const char *relative_path,
    uint32_t offset,
    void *buffer,
    uint32_t capacity
)
{
    const TitanUserProfile *profile;
    char safe_path[256];
    if (!g_app_runtime.active || relative_path == 0 || buffer == 0 || capacity == 0u ||
        capacity > TITAN_APP_VMC_READ_MAX_BYTES ||
        !TitanAppRuntime_StringOwned(relative_path, sizeof(safe_path)) ||
        !TitanAppRuntime_WritableSpan(buffer, capacity)) return -1;
    TitanAppRuntime_CopyText(safe_path, sizeof(safe_path), relative_path);
    profile = TitanUserProfiles_GetActive();
    if (profile == 0) return -1;
    return TitanVmc_ReadFile(profile, safe_path, offset, buffer, capacity);
}

static int TitanAppApi_LaunchElf(const char *absolute_path)
{
    if (g_app_runtime.launching || !TitanAppRuntime_StoragePathSafe(absolute_path, 0))
        return -1;
#if defined(__EE__) || defined(_EE)
    if (TitanElf_ValidateFile(absolute_path) != TITAN_ELF_VALID) return -2;
    {
        char *argv[2];
        TitanExecHandoffResult handoff;
        argv[0] = (char *)absolute_path;
        argv[1] = 0;
        handoff = TitanExecHandoff_Run(1, argv);
        return handoff == TITAN_EXEC_HANDOFF_EXEC_RETURNED ? -4 : (int)handoff;
    }
#else
    return -3;
#endif
}

static int TitanAppApi_MediaBeginAudio(void)
{
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_SetError("Titan App tentou iniciar audio durante titan_app_init");
        return 0;
    }
    return TitanAudio_BeginExternalStream();
}

static uint32_t TitanAppApi_MediaSubmitPcm(const int16_t *samples, uint32_t frames)
{
    uint32_t bytes;

    if (g_app_runtime.launching || samples == 0 || frames == 0u || frames > 16384u)
        return 0u;
    if (frames > 0xffffffffu / (2u * (uint32_t)sizeof(int16_t)))
        return 0u;
    bytes = frames * 2u * (uint32_t)sizeof(int16_t);
    if (!TitanAppRuntime_ReadableSpan(samples, bytes))
        return 0u;
    return TitanAudio_SubmitExternalPcm((const short *)samples, frames);
}

static uint32_t TitanAppApi_MediaAudioFreeFrames(void)
{
    if (g_app_runtime.launching) return 0u;
    return TitanAudio_GetExternalFreeFrames();
}

static void TitanAppApi_MediaEndAudio(void)
{
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_SetError("Titan App tentou encerrar audio durante titan_app_init");
        return;
    }
    TitanAudio_EndExternalStream();
}

static int TitanAppApi_MediaPresentRgb565(
    const uint16_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride
)
{
    unsigned int row;

    if (g_app_runtime.launching)
    {
        return 0;
    }

    if (
        pixels == 0 ||
        width != TITAN_VIDEO_MEDIA_SURFACE_WIDTH ||
        height != TITAN_VIDEO_MEDIA_SURFACE_HEIGHT ||
        stride < width || stride > 4096u
    )
    {
        return 0;
    }
    {
        uint32_t last_row_words = (height - 1u) * stride;
        uint32_t span_words;
        uint32_t span_bytes;
        if (last_row_words > 0xffffffffu - width) return 0;
        span_words = last_row_words + width;
        if (span_words > 0xffffffffu / (uint32_t)sizeof(uint16_t)) return 0;
        span_bytes = span_words * (uint32_t)sizeof(uint16_t);
        if (!TitanAppRuntime_ReadableSpan(pixels, span_bytes)) return 0;
    }

    if (g_app_runtime.media_pixels == 0)
    {
        g_app_runtime.media_pixels =
            (unsigned short *)malloc(TITAN_APP_MEDIA_BYTES);
        if (g_app_runtime.media_pixels == 0)
            return 0;
    }

    for (row = 0u; row < height; row++)
    {
        memcpy(
            &g_app_runtime.media_pixels[row * width],
            &pixels[row * stride],
            width * sizeof(unsigned short)
        );
    }
    g_app_runtime.media_ready = 1;

    /* Once an app presents video, discard its static background so a media
     * player never carries two 256 KiB RGB565 frames unnecessarily. */
    free(g_app_runtime.background_pixels);
    g_app_runtime.background_pixels = 0;
    g_app_runtime.background_width = 0u;
    g_app_runtime.background_height = 0u;
    g_app_runtime.background_ready = 0;
    return 1;
}

static void TitanAppRuntime_ImageDecodeYield(void *user_data)
{
    (void)user_data;
#if defined(__EE__) || defined(_EE)
    DelayThread(0);
#endif
}

static int TitanAppApi_MediaPresentImageFile(const char *absolute_path)
{
    TitanStorageIoSize source_size;
    unsigned char *source = 0;
    unsigned short *pixels = 0;
    int descriptor = -1;
    int ok = 0;
    TitanAssetImageOptions options;
    if (g_app_runtime.launching ||
        !TitanAppRuntime_StoragePathSafe(absolute_path, 0) ||
        !TitanStorageIo_PathInfo64(absolute_path, 0, &source_size) ||
        source_size == 0u || source_size > TITAN_APP_IMAGE_SOURCE_MAX)
        return 0;
    source = (unsigned char *)malloc((size_t)source_size);
    pixels = (unsigned short *)malloc(TITAN_APP_MEDIA_BYTES);
    if (source == 0 || pixels == 0) goto done;
    descriptor = TitanStorageIo_Open(absolute_path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0 ||
        !TitanStorageIo_ReadExact(descriptor, source, (unsigned int)source_size))
        goto done;
    TitanStorageIo_Close(descriptor);
    descriptor = -1;
    memset(&options, 0, sizeof(options));
    options.yield = TitanAppRuntime_ImageDecodeYield;
    options.yield_every_rows = 8u;
    if (!TitanAssetImage_DecodeMediaRgb565Fit(source, (unsigned int)source_size,
            pixels, TITAN_VIDEO_MEDIA_SURFACE_WIDTH, TITAN_VIDEO_MEDIA_SURFACE_HEIGHT,
            TITAN_VIDEO_MEDIA_SURFACE_WIDTH, &options))
        goto done;
    free(g_app_runtime.media_pixels);
    g_app_runtime.media_pixels = pixels;
    pixels = 0;
    g_app_runtime.media_ready = 1;
    free(g_app_runtime.background_pixels);
    g_app_runtime.background_pixels = 0;
    g_app_runtime.background_width = 0u;
    g_app_runtime.background_height = 0u;
    g_app_runtime.background_ready = 0;
    ok = 1;
done:
    if (descriptor >= 0) TitanStorageIo_Close(descriptor);
    free(source);
    free(pixels);
    return ok;
}

static void TitanAppApi_MediaDraw(
    float x, float y, float width, float height, uint8_t alpha
)
{
    if (g_app_runtime.launching)
    {
        return;
    }
    if (!TitanAppRuntime_SaneRect(x, y, width, height))
    {
        TitanAppRuntime_RejectDraw();
        return;
    }
    if (!g_app_runtime.media_ready || g_app_runtime.media_pixels == 0)
        return;
    TitanAppRuntime_MapRect(&x, &y, &width, &height);
    (void)TitanVideo_DrawMediaImage16(
        g_app_runtime.media_pixels,
        TITAN_VIDEO_MEDIA_SURFACE_WIDTH,
        TITAN_VIDEO_MEDIA_SURFACE_HEIGHT,
        TITAN_VIDEO_MEDIA_SURFACE_WIDTH,
        x, y, width, height, alpha
    );
}

static int TitanAppApi_GameBegin(
    const TitanGameSessionConfig *config,
    const TitanGameApiV1 **game_api
)
{
    if (!g_app_runtime.active || !g_app_runtime.trusted_game ||
        game_api == 0 ||
        !TitanAppRuntime_WritableSpan(game_api, sizeof(*game_api)) ||
        (config != 0 &&
         !TitanAppRuntime_ReadableSpan(config, sizeof(*config))))
    {
        return 0;
    }
    if (!TitanGameRuntime_SetStorageRoot(g_app_runtime.root_path))
        return 0;

    /* The launch/live-area background has completed its job by the time a
     * Titan Native Game asks for the exclusive game runtime. Drop the 640x480
     * EE allocation before reserving that arena; the game owns the screen from
     * this boundary onward. */
    free(g_app_runtime.background_pixels);
    g_app_runtime.background_pixels = 0;
    g_app_runtime.background_width = 0u;
    g_app_runtime.background_height = 0u;
    g_app_runtime.background_ready = 0;
    return TitanGameRuntime_Begin(config, game_api);
}

static void TitanAppApi_GameEnd(void)
{
    if (g_app_runtime.trusted_game && TitanGameRuntime_IsActive())
        (void)TitanGameRuntime_End();
}

static void TitanAppRuntime_CopyPfsStatus(const TitanStorageMountInfo *mount, TitanAppPfsMountStatus *status)
{
    memset(status, 0, sizeof(*status));
    status->slot = mount->slot;
    status->generation = mount->generation;
    status->capabilities = mount->capabilities;
    status->state = (uint32_t)mount->state;
    TitanAppRuntime_CopyText(status->root, sizeof(status->root), mount->root);
}

static int TitanAppApi_PfsAvailable(void)
{
    return !g_app_runtime.launching && TitanStorageMount_PfsAvailable();
}

static int TitanAppApi_PfsMount(const char *source, uint32_t slot, uint32_t capabilities, TitanAppPfsMountStatus *status)
{
    TitanStorageMountInfo mount;
    uint32_t mount_caps = 0u;
    if (g_app_runtime.launching || !g_app_runtime.active || slot >= TITAN_STORAGE_MOUNT_MAX_SLOTS ||
        !TitanAppRuntime_StringOwned(source, TITAN_STORAGE_MOUNT_SOURCE_CAPACITY) ||
        !TitanAppRuntime_WritableSpan(status, sizeof(*status)) ||
        (capabilities & ~(TITAN_APP_PFS_CAP_READ | TITAN_APP_PFS_CAP_WRITE)) != 0u ||
        (capabilities & TITAN_APP_PFS_CAP_READ) == 0u ||
        (g_app_runtime.pfs_owned_slots & (1u << slot)) != 0u) return 0;
    if ((capabilities & TITAN_APP_PFS_CAP_READ) != 0u) mount_caps |= TITAN_STORAGE_MOUNT_CAP_READ;
    if ((capabilities & TITAN_APP_PFS_CAP_WRITE) != 0u) mount_caps |= TITAN_STORAGE_MOUNT_CAP_WRITE;
    if (!TitanStorageMount_MountPfs(source, slot, mount_caps, &mount))
    {
        if (TitanStorageMount_Get(slot, &mount)) TitanAppRuntime_CopyPfsStatus(&mount, status);
        return 0;
    }
    g_app_runtime.pfs_owned_slots |= (1u << slot);
    TitanAppRuntime_CopyPfsStatus(&mount, status);
    return 1;
}

static int TitanAppApi_PfsUnmount(uint32_t slot)
{
    if (slot >= TITAN_STORAGE_MOUNT_MAX_SLOTS || (g_app_runtime.pfs_owned_slots & (1u << slot)) == 0u) return 0;
    if (!TitanStorageMount_Unmount(slot)) return 0;
    g_app_runtime.pfs_owned_slots &= ~(1u << slot);
    return 1;
}

static int TitanAppApi_PfsStatus(uint32_t slot, TitanAppPfsMountStatus *status)
{
    TitanStorageMountInfo mount;
    if (slot >= TITAN_STORAGE_MOUNT_MAX_SLOTS || !TitanAppRuntime_WritableSpan(status, sizeof(*status)) ||
        !TitanStorageMount_Get(slot, &mount)) return 0;
    TitanAppRuntime_CopyPfsStatus(&mount, status);
    return 1;
}

static void TitanAppRuntime_ReleasePfsMounts(void)
{
    uint32_t slot;
    for (slot = 0u; slot < TITAN_STORAGE_MOUNT_MAX_SLOTS; ++slot)
        if ((g_app_runtime.pfs_owned_slots & (1u << slot)) != 0u) (void)TitanStorageMount_Unmount(slot);
    g_app_runtime.pfs_owned_slots = 0u;
}

static int TitanAppApi_InputGetState(uint32_t player, TitanAppNormalizedInput *out)
{
    const TitanInputDeviceDescriptor *d;
    uint32_t reserved_mask = TITAN_NBTN_SYSTEM;
    if (player >= TITAN_UNIFIED_INPUT_MAX_PLAYERS ||
        !TitanAppRuntime_WritableSpan(out, sizeof(*out))) return 0;
    memset(out, 0, sizeof(*out));
    d = TitanUnifiedInput_GetPlayer(player);
    if (d == 0 || !d->connected) return 0;
    out->held = d->state.buttons & ~reserved_mask;
    out->pressed = d->state.pressed & ~reserved_mask;
    out->released = d->state.released & ~reserved_mask;
    out->left_stick_x = d->state.left_x; out->left_stick_y = d->state.left_y;
    out->right_stick_x = d->state.right_x; out->right_stick_y = d->state.right_y;
    out->trigger_l = d->state.trigger_l; out->trigger_r = d->state.trigger_r;
    out->pointer_dx = d->state.pointer_dx; out->pointer_dy = d->state.pointer_dy;
    out->wheel = d->state.wheel;
    return 1;
}

static int TitanAppApi_InputGetDevice(uint32_t player, TitanAppInputDeviceInfo *out)
{
    const TitanInputDeviceDescriptor *d;
    if (player >= TITAN_UNIFIED_INPUT_MAX_PLAYERS ||
        !TitanAppRuntime_WritableSpan(out, sizeof(*out))) return 0;
    memset(out, 0, sizeof(*out));
    d = TitanUnifiedInput_GetPlayer(player);
    if (d == 0 || !d->connected) { out->player = TITAN_APP_INPUT_PLAYER_UNASSIGNED; return 0; }
    out->device_id = d->device_id; out->capabilities = d->capabilities;
    out->vendor_id = d->vendor_id; out->product_id = d->product_id;
    out->device_type = (uint8_t)d->type; out->transport = (uint8_t)d->transport;
    out->player = d->player; out->connected = d->connected;
    out->battery_state = (uint8_t)d->battery_state;
    out->battery_percent = d->battery_percent; out->battery_percent_valid = d->battery_valid;
    out->battery_raw = d->battery_raw; out->packet_rate = d->packet_rate;
    out->reconnect_count = d->reconnect_count; out->errors = d->errors;
    out->latency_us = d->last_latency_us;
    return 1;
}

static const TitanAppApiV1 g_titan_app_api_v1 =
{
    TITAN_APP_API_VERSION,
    sizeof(TitanAppApiV1),
    TitanAppApi_RequestExit,
    TitanAppApi_UptimeMs,
    TitanAppApi_Alloc,
    TitanAppApi_Release,
    TitanAppApi_DrawText,
    TitanAppApi_DrawTextShadow,
    TitanAppApi_DrawRect,
    TitanAppApi_DrawPanel,
    TitanAppApi_DrawProgress,
    TitanAppApi_Notify,
    TitanAppApi_FileRead,
    TitanAppApi_FileWriteData,
    TitanAppApi_FileExists,
    TitanAppApi_MediaBeginAudio,
    TitanAppApi_MediaSubmitPcm,
    TitanAppApi_MediaAudioFreeFrames,
    TitanAppApi_MediaEndAudio,
    TitanAppApi_MediaPresentRgb565,
    TitanAppApi_MediaDraw,
    TitanAppApi_StorageList,
    TitanAppApi_StorageStat,
    TitanAppApi_StorageRead,
    TitanAppApi_StorageWrite,
    TitanAppApi_StorageMkdir,
    TitanAppApi_StorageRemove,
    TitanAppApi_StorageRename,
    TitanAppApi_LaunchElf,
    TitanAppApi_GameBegin,
    TitanAppApi_GameEnd,
    TitanAppApi_StorageListVolumes,
    TitanAppApi_StorageCommitReplace,
    TitanAppApi_TextInputOpen,
    TitanAppApi_TextInputPoll,
    TitanAppApi_NetworkGetBegin,
    TitanAppApi_NetworkGetStatus,
    TitanAppApi_NetworkGetRead,
    TitanAppApi_NetworkCancel,
    TitanAppApi_MediaPresentImageFile,
    TitanAppApi_FtpStart,
    TitanAppApi_FtpStop,
    TitanAppApi_FtpStatus,
    TitanAppApi_VmcListActiveRoot,
    TitanAppApi_VmcListActivePath,
    TitanAppApi_VmcReadActiveFile,
    TitanAppApi_PfsAvailable,
    TitanAppApi_PfsMount,
    TitanAppApi_PfsUnmount,
    TitanAppApi_PfsStatus,
    TitanAppApi_InputGetState,
    TitanAppApi_InputGetDevice
};

int TitanAppRuntime_ValidateModuleFile(const char *path)
{
    TitanTmodLoaded module;
    return TitanTmod_Load(path, &module, 0, 0);
}

int TitanAppRuntime_ValidateRuntimeModuleFile(const char *path)
{
    TitanTmodLoaded module;
    int result;

    result = TitanTmod_Load(path, &module, 1, 0);
    if (result)
    {
        TitanTmod_Unload(&module);
    }
    return result;
}

int TitanAppRuntime_ValidateGameModuleFile(const char *path)
{
    TitanTmodLoaded module;
    int result = TitanTmod_Load(path, &module, 1, 1);
    if (result) TitanTmod_Unload(&module);
    return result;
}

static int TitanAppRuntime_LoadBackground(const char *path)
{
    TitanStorageIoSize size;
    unsigned int bytes;
    unsigned int width;
    unsigned int height;
    int descriptor;

    g_app_runtime.background_width = 0u;
    g_app_runtime.background_height = 0u;
    if (path == 0 || path[0] == '\0' ||
        !TitanStorageIo_PathInfo64(path, 0, &size))
    {
        return 0;
    }

    if (size == (TitanStorageIoSize)TITAN_APP_BACKGROUND_PRIMARY_BYTES)
    {
        bytes = TITAN_APP_BACKGROUND_PRIMARY_BYTES;
        width = TITAN_VIDEO_MEDIA_PRIMARY_WIDTH;
        height = TITAN_VIDEO_MEDIA_PRIMARY_HEIGHT;
    }
    else if (size == (TitanStorageIoSize)TITAN_APP_BACKGROUND_LEGACY_BYTES)
    {
        bytes = TITAN_APP_BACKGROUND_LEGACY_BYTES;
        width = TITAN_VIDEO_MEDIA_LEGACY_WIDTH;
        height = TITAN_VIDEO_MEDIA_LEGACY_HEIGHT;
    }
    else
    {
        return 0;
    }

    g_app_runtime.background_pixels =
        (unsigned short *)malloc(bytes);
    if (g_app_runtime.background_pixels == 0)
    {
        return 0;
    }
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0 ||
        !TitanStorageIo_ReadExact(
            descriptor,
            g_app_runtime.background_pixels,
            bytes
        ))
    {
        if (descriptor >= 0) TitanStorageIo_Close(descriptor);
        free(g_app_runtime.background_pixels);
        g_app_runtime.background_pixels = 0;
        return 0;
    }
    TitanStorageIo_Close(descriptor);
    g_app_runtime.background_width = width;
    g_app_runtime.background_height = height;
    return 1;
}

static void TitanAppRuntime_ReleaseStorageExclusive(void)
{
    if (!g_app_runtime.storage_exclusive)
    {
        return;
    }
    TitanStorageScheduler_SetTitanAppExclusiveMode(0);
    g_app_runtime.storage_exclusive = 0;
    TitanAudio_StartMusic();
}

#if defined(__EE__) || defined(_EE)
static void TitanAppRuntime_InitThread(void *argument)
{
    int result;

    (void)argument;
    result = -1;
    if (g_app_runtime.launching && g_app_runtime.module.init != 0)
    {
        result = g_app_runtime.module.init(
            &g_titan_app_api_v1,
            g_app_runtime.root_path
        );
    }
    g_app_runtime.init_result = result;
    g_app_runtime.init_done = 1;
    ExitThread();
}

static void TitanAppRuntime_StopInitThread(int force)
{
    if (g_app_runtime.init_thread_id < 0)
    {
        return;
    }
    if (force && !g_app_runtime.init_done)
    {
        (void)TerminateThread(g_app_runtime.init_thread_id);
    }
    (void)DeleteThread(g_app_runtime.init_thread_id);
    g_app_runtime.init_thread_id = -1;
}

static void TitanAppRuntime_GameThread(void *argument)
{
    TitanAppInput neutral_input;

    (void)argument;
    memset(&neutral_input, 0, sizeof(neutral_input));

    while (g_app_runtime.active && !g_app_runtime.exit_requested)
    {
        /* Titan Native Game input is sourced through TGR. The App update
         * callback receives a neutral frame so a long-lived game main loop can
         * safely own this worker without sharing mutable shell input structs. */
        g_app_runtime.module.update(&neutral_input, 16u);
        if (!g_app_runtime.active || g_app_runtime.exit_requested) break;
        DelayThread(1000);
    }

    g_app_runtime.game_thread_done = 1;
    ExitThread();
}

static int TitanAppRuntime_StartGameThread(void)
{
    ee_thread_t thread;
    int id;

    if (g_app_runtime.game_thread_started) return 1;
    memset(&thread, 0, sizeof(thread));
    thread.attr = 0;
    thread.option = 0;
    thread.func = TitanAppRuntime_GameThread;
    thread.stack = g_app_runtime.game_thread_stack;
    thread.stack_size = sizeof(g_app_runtime.game_thread_stack);
    thread.gp_reg = &_gp;
    thread.initial_priority = TITAN_APP_GAME_THREAD_PRIORITY;

    id = CreateThread(&thread);
    if (id < 0) return 0;
    g_app_runtime.game_thread_id = id;
    g_app_runtime.game_thread_done = 0;
    g_app_runtime.game_thread_started = 1;
    if (StartThread(id, 0) < 0)
    {
        (void)DeleteThread(id);
        g_app_runtime.game_thread_id = -1;
        g_app_runtime.game_thread_started = 0;
        g_app_runtime.game_thread_done = 0;
        return 0;
    }
    return 1;
}

static void TitanAppRuntime_StopGameThread(int force)
{
    if (!g_app_runtime.game_thread_started || g_app_runtime.game_thread_id < 0)
        return;
    if (!g_app_runtime.game_thread_done && !force) return;
    if (!g_app_runtime.game_thread_done && force)
        (void)TerminateThread(g_app_runtime.game_thread_id);
    (void)DeleteThread(g_app_runtime.game_thread_id);
    g_app_runtime.game_thread_id = -1;
    g_app_runtime.game_thread_started = 0;
    g_app_runtime.game_thread_done = 0;
}
#else
static void TitanAppRuntime_StopInitThread(int force)
{
    (void)force;
}

static int TitanAppRuntime_StartGameThread(void)
{
    g_app_runtime.game_thread_started = 1;
    g_app_runtime.game_thread_done = 0;
    return 1;
}

static void TitanAppRuntime_StopGameThread(int force)
{
    (void)force;
    g_app_runtime.game_thread_started = 0;
    g_app_runtime.game_thread_done = 0;
}
#endif

static void TitanAppRuntime_ResetSessionMemory(void)
{
    TitanAppRuntime_ReleasePfsMounts();
    TitanAppNetwork_AbandonSession();
    if (g_app_runtime.text_input_owned || TitanTextInput_IsOpen())
    {
        TitanTextInput_Initialize();
        g_app_runtime.text_input_owned = 0;
        g_app_runtime.text_input_result = TITAN_APP_TEXT_INPUT_CANCELLED;
    }
    TitanAudio_EndExternalStream();
    TitanAppRuntime_ReleaseAllAllocations();
    TitanTmod_Unload(&g_app_runtime.module);
    free(g_app_runtime.background_pixels);
    free(g_app_runtime.media_pixels);
    g_app_runtime.background_pixels = 0;
    g_app_runtime.background_width = 0u;
    g_app_runtime.background_height = 0u;
    g_app_runtime.media_pixels = 0;
    g_app_runtime.background_ready = 0;
    g_app_runtime.media_ready = 0;
}

static void TitanAppRuntime_AbortLaunch(const char *message)
{
    char error_copy[sizeof(g_app_runtime.last_error)];

    if (g_app_runtime.exit_reason == TITAN_APP_EXIT_NONE)
        g_app_runtime.exit_reason = TITAN_APP_EXIT_LAUNCH_FAILURE;
    TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_FAILED);
    TitanAppRuntime_SetError(message);
    TitanAppRuntime_CopyText(
        error_copy,
        sizeof(error_copy),
        g_app_runtime.last_error
    );
    TitanAppRuntime_StopInitThread(1);
    TitanAppRuntime_ResetSessionMemory();
    g_app_runtime.launching = 0;
    g_app_runtime.active = 0;
    g_app_runtime.exit_requested = 0;
    g_app_runtime.launch_phase = TITAN_APP_LAUNCH_IDLE;
    TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_DESTROY);
    TitanAppRuntime_ReleaseStorageExclusive();
    if (TitanSession_GetState() == TITAN_SESSION_RETURN_REQUESTED) (void)TitanSession_Transition(TITAN_SESSION_RETURNING);
    if (TitanSession_IsActive()) { if (TitanSession_GetState() != TITAN_SESSION_RETURNING) TitanSession_Fail(); TitanSession_End(); }
    TitanOverlay_Reset();
    TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_IDLE);
    TitanNotifications_Push(
        TITAN_NOTIFICATION_ERROR,
        "Falha ao iniciar Titan App",
        error_copy
    );
}

#if defined(__EE__) || defined(_EE)
static int TitanAppRuntime_StartInitThread(void)
{
    ee_thread_t thread;
    int id;

    memset(&thread, 0, sizeof(thread));
    thread.attr = 0;
    thread.option = 0;
    thread.func = TitanAppRuntime_InitThread;
    thread.stack = g_app_runtime.init_thread_stack;
    thread.stack_size = sizeof(g_app_runtime.init_thread_stack);
    thread.gp_reg = &_gp;
    thread.initial_priority = TITAN_APP_INIT_THREAD_PRIORITY;

    id = CreateThread(&thread);
    if (id < 0)
    {
        return 0;
    }
    g_app_runtime.init_thread_id = id;
    if (StartThread(id, 0) < 0)
    {
        (void)DeleteThread(id);
        g_app_runtime.init_thread_id = -1;
        return 0;
    }
    return 1;
}
#else
static int TitanAppRuntime_StartInitThread(void)
{
    /* Host validation never executes MIPS modules.  This branch only keeps
     * source-level lifecycle tests deterministic on non-PS2 builds. */
    (void)g_titan_app_api_v1.abi_version;
    g_app_runtime.init_result = 0;
    g_app_runtime.init_done = 1;
    return 1;
}
#endif

int TitanAppRuntime_Open(
    const char *title,
    const char *root_path,
    const char *module_path,
    const char *background_path,
    int trusted_game
)
{
    if (g_app_runtime.active || g_app_runtime.launching ||
        title == 0 || root_path == 0 || module_path == 0)
    {
        TitanAppRuntime_SetError("Titan App runtime is busy");
        return 0;
    }

    memset(&g_app_runtime, 0, sizeof(g_app_runtime));
#if defined(__EE__) || defined(_EE)
    g_app_runtime.init_thread_id = -1;
    g_app_runtime.game_thread_id = -1;
#endif
    TitanAppRuntime_CopyText(g_app_runtime.title, sizeof(g_app_runtime.title), title);
    TitanAppRuntime_CopyText(
        g_app_runtime.root_path,
        sizeof(g_app_runtime.root_path),
        root_path
    );
    TitanAppRuntime_CopyText(
        g_app_runtime.module_path,
        sizeof(g_app_runtime.module_path),
        module_path
    );
    TitanAppRuntime_CopyText(
        g_app_runtime.background_path,
        sizeof(g_app_runtime.background_path),
        background_path
    );

    g_app_runtime.launching = 1;
    g_app_runtime.trusted_game = trusted_game != 0;
    g_app_runtime.runtime_class = trusted_game ? TITAN_APP_RUNTIME_CLASS_GAME : TITAN_APP_RUNTIME_CLASS_MODULE;
    g_app_runtime.exit_reason = TITAN_APP_EXIT_NONE;
    g_app_runtime.lifecycle = TITAN_APP_LIFECYCLE_IDLE;
    TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_PREPARE);
    g_app_runtime.launch_phase = TITAN_APP_LAUNCH_WAIT_STORAGE;
    g_app_runtime.launch_started_ms = TitanTime_NowMilliseconds();
    TitanAppRuntime_SetLaunchStatus("Reservando recursos do Titan App");

    /* A Titan-native app remains in the Titan process.  Give its session one
     * explicit FileXio owner: the main EE thread.  The background storage
     * worker parks before module load/init and remains parked until app exit. */
    TitanAudio_StopMusic();
    TitanStorageScheduler_SetTitanAppExclusiveMode(1);
    g_app_runtime.storage_exclusive = 1;
    return 1;
}

static int TitanAppRuntime_LaunchTimedOut(void)
{
    uint64_t now;

    now = TitanTime_NowMilliseconds();
    return now >= g_app_runtime.launch_started_ms &&
        now - g_app_runtime.launch_started_ms > TITAN_APP_INIT_TIMEOUT_MS;
}

static void TitanAppRuntime_UpdateLaunching(const TitanInputState *input)
{
    if (input != 0 &&
        (input->buttons & TITAN_INPUT_BUTTON_SELECT) != 0u &&
        (input->pressed & TITAN_INPUT_BUTTON_CIRCLE) != 0u)
    {
        TitanAppRuntime_AbortLaunch("Inicializacao do Titan App cancelada");
        return;
    }

    if (TitanAppRuntime_LaunchTimedOut())
    {
        if (g_app_runtime.launch_phase == TITAN_APP_LAUNCH_WAIT_STORAGE)
        {
            TitanAppRuntime_AbortLaunch(
                "handoff de storage do Titan App excedeu 15 segundos; sessao recuperada"
            );
        }
        else if (g_app_runtime.launch_phase == TITAN_APP_LAUNCH_START_INIT ||
                 g_app_runtime.launch_phase == TITAN_APP_LAUNCH_WAIT_INIT)
        {
            TitanAppRuntime_AbortLaunch(
                "titan_app_init excedeu 15 segundos; sessao recuperada"
            );
        }
        else
        {
            TitanAppRuntime_AbortLaunch(
                "carregamento do Titan App excedeu 15 segundos; sessao recuperada"
            );
        }
        return;
    }

    switch (g_app_runtime.launch_phase)
    {
        case TITAN_APP_LAUNCH_WAIT_STORAGE:
            TitanAppRuntime_SetLaunchStatus("Estacionando servicos de armazenamento");
            if (TitanStorageScheduler_IsTitanAppWorkerParked())
            {
                g_app_runtime.launch_phase = TITAN_APP_LAUNCH_LOAD_MODULE;
            }
#if defined(__EE__) || defined(_EE)
            else
            {
                /* P164_NATIVE_STORAGE_HANDOFF_YIELD:
                 * The storage worker runs at a deliberately low background
                 * priority. While the shell waits for its exclusive-handoff
                 * acknowledgement, explicitly leave the ready queue so the
                 * worker can reach the park point deterministically. */
                DelayThread(1000);
            }
#endif
            break;

        case TITAN_APP_LAUNCH_LOAD_MODULE:
            TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_LOAD);
            TitanAppRuntime_SetLaunchStatus("Validando modulo para EE/R5900");
            if (!TitanTmod_Load(
                    g_app_runtime.module_path,
                    &g_app_runtime.module,
                    1,
                    g_app_runtime.trusted_game))
            {
                const char *error = g_app_runtime.last_error[0] != '\0'
                    ? g_app_runtime.last_error
                    : "Modulo Titan App invalido";
                char copy[sizeof(g_app_runtime.last_error)];
                TitanAppRuntime_CopyText(copy, sizeof(copy), error);
                TitanAppRuntime_AbortLaunch(copy);
                return;
            }
            g_app_runtime.launch_phase = TITAN_APP_LAUNCH_LOAD_BACKGROUND;
            break;

        case TITAN_APP_LAUNCH_LOAD_BACKGROUND:
            TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_CREATE);
            TitanAppRuntime_SetLaunchStatus("Preparando superficie do aplicativo");
            g_app_runtime.background_ready =
                TitanAppRuntime_LoadBackground(g_app_runtime.background_path);
            g_app_runtime.launch_phase = TITAN_APP_LAUNCH_START_INIT;
            break;

        case TITAN_APP_LAUNCH_START_INIT:
            TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_START);
            TitanAppRuntime_SetLaunchStatus("Iniciando Titan App em contexto protegido");
            g_app_runtime.init_done = 0;
            g_app_runtime.init_result = -1;
            if (!TitanAppRuntime_StartInitThread())
            {
                TitanAppRuntime_AbortLaunch("Nao foi possivel criar a thread do Titan App");
                return;
            }
            g_app_runtime.launch_phase = TITAN_APP_LAUNCH_WAIT_INIT;
#if defined(__EE__) || defined(_EE)
            /* P163_NATIVE_INIT_SCHEDULER_YIELD:
             * The shell/main EE thread remains continuously runnable while the
             * init worker is priority 80.  On the EE priority scheduler that
             * can starve the lower-priority worker indefinitely.  Sleeping the
             * shell for one millisecond here gives titan_app_init a deterministic
             * scheduling window without moving module code onto the shell thread. */
            DelayThread(1000);
#endif
            break;

        case TITAN_APP_LAUNCH_WAIT_INIT:
            TitanAppRuntime_SetLaunchStatus("Inicializando aplicativo");
#if defined(__EE__) || defined(_EE)
            /* Keep yielding while init is pending.  This also covers future
             * valid init handshakes that take more than one scheduler slice. */
            if (!g_app_runtime.init_done)
                DelayThread(1000);
#endif
            if (g_app_runtime.init_done)
            {
                TitanAppRuntime_StopInitThread(0);
                if (g_app_runtime.init_result != 0)
                {
                    if (g_app_runtime.init_result == -2)
                    {
                        TitanAppRuntime_AbortLaunch(
                            "Titan App requer um runtime mais novo (atualize o sistema)"
                        );
                    }
                    else
                    {
                        TitanAppRuntime_AbortLaunch("titan_app_init rejeitou a sessao");
                    }
                    return;
                }
                /* Native apps get a short, deterministic launch presentation.
                 * Preparation is still fully asynchronous; this only prevents a
                 * successful fast init from flashing HOME -> app in a few frames. */
                {
                    uint64_t now = TitanTime_NowMilliseconds();
                    if (now >= g_app_runtime.launch_started_ms &&
                        now - g_app_runtime.launch_started_ms < TITAN_APP_SPLASH_MIN_MS)
                    {
                        TitanAppRuntime_SetLaunchStatus("Aplicativo pronto");
                        break;
                    }
                }
                g_app_runtime.launching = 0;
                g_app_runtime.active = 1;
                g_app_runtime.exit_requested = 0;
                g_app_runtime.launch_phase = TITAN_APP_LAUNCH_IDLE;
                TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_ACTIVE);
                {
                    TitanSessionDescriptor session;
                    TitanOverlayLevel overlay_level;
                    uint32_t profile = 0u;
                    memset(&session, 0, sizeof(session));
                    strncpy(session.session_id, "native", sizeof(session.session_id)-1u);
                    strncpy(session.title_id, strstr(g_app_runtime.title, "Project NES") != 0 ? "TITAN-NES" : "TITAN-NATIVE", sizeof(session.title_id)-1u);
                    strncpy(session.launch_path, g_app_runtime.root_path, sizeof(session.launch_path)-1u);
                    session.engine = TITAN_SESSION_ENGINE_NATIVE;
                    session.start_time_ms = TitanTime_NowMilliseconds();
                    session.runtime_capabilities = TITAN_SESSION_CAP_SAFE_RETURN | TITAN_SESSION_CAP_OVERLAY_MINIMAL |
                        TITAN_SESSION_CAP_GS_STATE_RESTORE | TITAN_SESSION_CAP_NATIVE_SAFEPOINT | TITAN_SESSION_CAP_INPUT_NORMALIZED;
                    if (strcmp(session.title_id, "TITAN-NES") == 0) session.runtime_capabilities |= TITAN_SESSION_CAP_OVERLAY_FULL;
                    session.igr_compatibility = TitanIgrCompatibility_Evaluate(session.title_id, session.engine, session.runtime_capabilities, 0u, &overlay_level, &profile);
                    session.overlay_level = overlay_level; session.compatibility_profile = profile;
                    TitanSession_Initialize(); TitanOverlay_Initialize();
                    if (TitanSession_Begin(&session)) { (void)TitanSession_Transition(TITAN_SESSION_LAUNCHING); (void)TitanSession_Transition(TITAN_SESSION_RUNNING); }
                }
                TitanAppRuntime_SetLaunchStatus("Titan App ativo");
            }
            break;

        case TITAN_APP_LAUNCH_IDLE:
        default:
            TitanAppRuntime_AbortLaunch("Estado de inicializacao Titan App invalido");
            break;
    }
}

void TitanAppRuntime_Update(const TitanInputState *input, unsigned int delta_ms)
{
    TitanAppInput app_input;

    if (g_app_runtime.launching)
    {
        TitanAppRuntime_UpdateLaunching(input);
        return;
    }
    if (!g_app_runtime.active)
    {
        return;
    }

    TitanFtpService_Tick();
    memset(&app_input, 0, sizeof(app_input));
    if (input != 0)
    {
        app_input.held = input->buttons;
        app_input.pressed = input->pressed;
        app_input.released = input->released;
        app_input.connected = input->connected != 0;
        app_input.left_stick_x = input->left_stick_x;
        app_input.left_stick_y = input->left_stick_y;
        app_input.right_stick_x = input->right_stick_x;
        app_input.right_stick_y = input->right_stick_y;
    }

    {
        const TitanInputDeviceDescriptor *overlay_device = TitanUnifiedInput_GetPlayer(0u);
        uint64_t session_token = TitanOverhead_Begin(TITAN_OVERHEAD_SESSION, TitanSession_IsActive());
        uint64_t overlay_token;
        TitanOverlayAction overlay_action;
        TitanSession_Update(TitanTime_NowMilliseconds());
        TitanOverhead_End(TITAN_OVERHEAD_SESSION, session_token, TitanSession_IsActive());
        overlay_token = TitanOverhead_Begin(TITAN_OVERHEAD_OVERLAY, TitanOverlay_IsOpen());
        overlay_action = TitanOverlay_Update(overlay_device ? &overlay_device->state : 0);
        TitanOverhead_End(TITAN_OVERHEAD_OVERLAY, overlay_token, TitanOverlay_IsOpen());
        if (TitanOverlay_IsOpen()) return;
        if (overlay_action == TITAN_OVERLAY_ACTION_RESUME) return;
        if (overlay_action == TITAN_OVERLAY_ACTION_RETURN) {
            g_app_runtime.exit_reason = TITAN_APP_EXIT_USER_CHORD;
            g_app_runtime.exit_requested = 1;
            TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_EXIT_REQUEST);
        }
    }

    /* SELECT+CIRCLE remains a kernel-owned escape hatch once init completed. */
    if (!g_app_runtime.exit_requested && (app_input.held & TITAN_APP_BUTTON_SELECT) != 0u &&
        (app_input.pressed & TITAN_APP_BUTTON_CIRCLE) != 0u)
    {
        g_app_runtime.exit_reason = TITAN_APP_EXIT_USER_CHORD;
        g_app_runtime.exit_requested = 1;
        TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_EXIT_REQUEST);
    }
    else if ((app_input.pressed & TITAN_APP_BUTTON_R3) != 0u)
    {
        /* Fullscreen is shell chrome, not application state. Keep the toggle
         * kernel-owned so every Titan App gets identical behavior without an
         * ABI bump or app-specific code. */
        g_app_runtime.fullscreen = !g_app_runtime.fullscreen;
    }
    else
    {
        /* R3 is reserved by the shell for window/fullscreen chrome and is not
         * forwarded to applications. */
        app_input.held &= (uint16_t)~TITAN_APP_BUTTON_R3;
        app_input.pressed &= (uint16_t)~TITAN_APP_BUTTON_R3;
        app_input.released &= (uint16_t)~TITAN_APP_BUTTON_R3;

        if (g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME)
        {
            /* A Titan Game may enter a long-lived native main loop (GoldenEye
             * bossEntry is one). Never execute that callback on the shell
             * update thread: doing so starves lifecycle, input and recovery. */
            if (!g_app_runtime.game_thread_started &&
                !TitanAppRuntime_StartGameThread())
            {
                g_app_runtime.exit_reason = TITAN_APP_EXIT_LAUNCH_FAILURE;
                g_app_runtime.exit_requested = 1;
                TitanAppRuntime_SetError("Nao foi possivel criar a thread do Titan Game");
                TitanNotifications_Push(
                    TITAN_NOTIFICATION_ERROR,
                    "Falha ao iniciar Titan Game",
                    g_app_runtime.last_error
                );
            }
        }
        else
        {
            g_app_runtime.module.update(&app_input, delta_ms);
        }
    }

    if (g_app_runtime.exit_requested &&
        g_app_runtime.lifecycle != TITAN_APP_LIFECYCLE_EXIT_REQUEST)
        TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_EXIT_REQUEST);

    if (g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME &&
        g_app_runtime.exit_requested)
    {
        /* The worker must unwind bossEntry()/its renderer before its module
         * image or TGR state can be destroyed. */
        TitanGameRuntime_RequestExit();
    }

    if (g_app_runtime.exit_requested &&
        (g_app_runtime.runtime_class != TITAN_APP_RUNTIME_CLASS_GAME ||
         !g_app_runtime.game_thread_started || g_app_runtime.game_thread_done))
    {
        TitanAppRuntime_Close();
    }
}

void TitanAppRuntime_Draw(unsigned int frame)
{
    float window_x;
    float window_y;
    float window_w;
    float window_h;

    if (g_app_runtime.launching)
    {
        uint64_t now;
        uint64_t elapsed;
        float fade;
        float title_width;
        float badge_width;
        float badge_x;
        float badge_y;
        float pulse;
        unsigned int pulse_phase;
        unsigned char overlay_alpha;
        unsigned char content_alpha;
        const char *class_label;

        now = TitanTime_NowMilliseconds();
        elapsed = now >= g_app_runtime.launch_started_ms
            ? now - g_app_runtime.launch_started_ms
            : 0u;
        fade = (float)elapsed / (float)TITAN_APP_SPLASH_FADE_MS;
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;
        content_alpha = (unsigned char)(255.0f * fade);
        overlay_alpha = (unsigned char)(235.0f * fade);

        TitanVideo_DrawRectAlpha(
            0.0f, 0.0f,
            (float)TITAN_APP_LOGICAL_WIDTH,
            (float)TITAN_APP_LOGICAL_HEIGHT,
            0, 0, 0, overlay_alpha
        );

        if (g_app_runtime.background_ready && g_app_runtime.background_pixels != 0)
        {
            (void)TitanVideo_DrawMediaImage16(
                g_app_runtime.background_pixels,
                g_app_runtime.background_width,
                g_app_runtime.background_height,
                g_app_runtime.background_width,
                0.0f, 0.0f,
                (float)TITAN_APP_LOGICAL_WIDTH,
                (float)TITAN_APP_LOGICAL_HEIGHT,
                content_alpha
            );
            TitanVideo_DrawRectAlpha(
                0.0f, 0.0f,
                (float)TITAN_APP_LOGICAL_WIDTH,
                (float)TITAN_APP_LOGICAL_HEIGHT,
                0, 0, 0,
                (unsigned char)(118.0f * fade)
            );
        }

        class_label = g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME
            ? "TITAN NATIVE GAME"
            : "TITAN NATIVE";

        title_width = TitanFont_MeasureText(
            0.92f,
            g_app_runtime.title[0] != '\0' ? g_app_runtime.title : "Titan App"
        );
        TitanFont_DrawTextShadow(
            ((float)TITAN_APP_LOGICAL_WIDTH - title_width) * 0.5f,
            188.0f,
            0.92f,
            g_app_runtime.title[0] != '\0' ? g_app_runtime.title : "Titan App",
            255, 255, 255, content_alpha
        );

        TitanFont_DrawTextAlpha(
            ((float)TITAN_APP_LOGICAL_WIDTH -
                TitanFont_MeasureText(0.50f, "Starting application")) * 0.5f,
            232.0f,
            0.50f,
            "Starting application",
            218, 220, 228, content_alpha
        );

        pulse_phase = (frame / 10u) % 3u;
        pulse = 0.40f + 0.60f * fade;
        TitanVideo_DrawRoundedRectAlpha(
            397.0f, 259.0f, 12.0f, 4.0f, 2.0f,
            222, 43, 82,
            (unsigned char)((pulse_phase == 0u ? 255.0f : 92.0f) * pulse)
        );
        TitanVideo_DrawRoundedRectAlpha(
            421.0f, 259.0f, 12.0f, 4.0f, 2.0f,
            222, 43, 82,
            (unsigned char)((pulse_phase == 1u ? 255.0f : 92.0f) * pulse)
        );
        TitanVideo_DrawRoundedRectAlpha(
            445.0f, 259.0f, 12.0f, 4.0f, 2.0f,
            222, 43, 82,
            (unsigned char)((pulse_phase == 2u ? 255.0f : 92.0f) * pulse)
        );

        badge_width = g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME
            ? 176.0f
            : 144.0f;
        badge_x = (float)TITAN_APP_LOGICAL_WIDTH - badge_width - 24.0f;
        badge_y = (float)TITAN_APP_LOGICAL_HEIGHT - 58.0f;
        TitanVideo_DrawRoundedRectAlpha(
            badge_x, badge_y, badge_width, 34.0f, 7.0f,
            5, 6, 10,
            (unsigned char)(220.0f * fade)
        );
        TitanVideo_DrawLineAlpha(
            badge_x + 1.0f, badge_y + 1.0f,
            badge_x + badge_width - 1.0f, badge_y + 1.0f,
            1.6f, 222, 43, 82, content_alpha
        );
        TitanFont_DrawTextAlpha(
            badge_x + 14.0f,
            badge_y + 10.0f,
            0.42f,
            class_label,
            246, 247, 250, content_alpha
        );

        TitanFont_DrawTextAlpha(
            24.0f,
            (float)TITAN_APP_LOGICAL_HEIGHT - 31.0f,
            0.38f,
            "Select + Circle: cancel",
            176, 179, 190,
            (unsigned char)(190.0f * fade)
        );
        return;
    }
    if (!g_app_runtime.active)
    {
        return;
    }

    if (g_app_runtime.fullscreen)
    {
        /* Fullscreen means the app owns the content canvas, while global Titan
         * overlays/notifications are still composed after it by the kernel. */
        TitanVideo_DrawRectAlpha(
            0.0f, 0.0f,
            (float)TITAN_APP_LOGICAL_WIDTH,
            (float)TITAN_APP_LOGICAL_HEIGHT,
            0, 0, 0, 255
        );

        if (g_app_runtime.background_ready && g_app_runtime.background_pixels != 0)
        {
            (void)TitanVideo_DrawMediaImage16(
                g_app_runtime.background_pixels,
                g_app_runtime.background_width,
                g_app_runtime.background_height,
                g_app_runtime.background_width,
                0.0f, 0.0f,
                (float)TITAN_APP_LOGICAL_WIDTH,
                (float)TITAN_APP_LOGICAL_HEIGHT,
                255u
            );
        }

        /* Titan Apps draw only through the public API. The API maps coordinates
         * itself, so no process-global renderer transform is exposed to module
         * execution. */
        g_app_runtime.module.draw();
        TitanAppRuntime_ReportRejectedDraws();
        TitanVideo_ResetTransform();
        TitanOverlay_Draw();
        return;
    }

    /* Default Orbis-style app window. The Home grid is rendered first by the
     * shell scene and behaves as a frozen live-area frame around this surface. */
    window_x = TITAN_APP_WINDOW_X - TITAN_APP_WINDOW_PAD;
    window_y = TITAN_APP_WINDOW_Y - TITAN_APP_WINDOW_PAD;
    window_w = TITAN_APP_WINDOW_W + (TITAN_APP_WINDOW_PAD * 2.0f);
    window_h = TITAN_APP_WINDOW_H + (TITAN_APP_WINDOW_PAD * 2.0f);

    TitanVideo_DrawRoundedRectAlpha(
        window_x + 6.0f, window_y + 8.0f, window_w, window_h,
        14.0f, 0, 0, 0, 128
    );
    TitanVideo_DrawRoundedRectAlpha(
        window_x, window_y, window_w, window_h,
        14.0f, 6, 7, 13, 246
    );
    TitanVideo_DrawLineAlpha(
        window_x, window_y, window_x + window_w, window_y,
        1.0f, 222, 43, 82, 238
    );

    if (g_app_runtime.background_ready && g_app_runtime.background_pixels != 0)
    {
        (void)TitanVideo_DrawMediaImage16(
            g_app_runtime.background_pixels,
            g_app_runtime.background_width,
            g_app_runtime.background_height,
            g_app_runtime.background_width,
            TITAN_APP_WINDOW_X, TITAN_APP_WINDOW_Y,
            TITAN_APP_WINDOW_W, TITAN_APP_WINDOW_H,
            255u
        );
    }

    /* Do not install a global TitanVideo transform while foreign module code is
     * running. Each public drawing primitive maps the app canvas into the
     * window explicitly, which keeps renderer state recoverable even if an app
     * produces a malformed call. */
    g_app_runtime.module.draw();
    TitanAppRuntime_ReportRejectedDraws();
    TitanVideo_ResetTransform();
    TitanOverlay_Draw();

    TitanFont_DrawTextAlpha(
        window_x + window_w - 112.0f,
        window_y + window_h - 24.0f,
        0.40f,
        "R3  Full screen",
        196, 201, 214, 224
    );
}

void TitanAppRuntime_Close(void)
{
    if (g_app_runtime.launching)
    {
        TitanAppRuntime_AbortLaunch("Inicializacao do Titan App encerrada");
        return;
    }
    if (!g_app_runtime.active && g_app_runtime.module.allocation == 0)
    {
        return;
    }
    if (g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME &&
        g_app_runtime.game_thread_started && !g_app_runtime.game_thread_done)
    {
        TitanGameRuntime_RequestExit();
        return;
    }
    TitanAppRuntime_StopGameThread(0);
    if (g_app_runtime.lifecycle != TITAN_APP_LIFECYCLE_QUIESCE)
    {
        TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_QUIESCE);
        if (g_app_runtime.active && g_app_runtime.module.shutdown != 0)
        {
            g_app_runtime.module.shutdown();
        }
        TitanFtpService_Stop();
    }
    /* If VIF1/PATH1 ownership is still fault-latched, stay QUIESCE and retry
     * on the next exit pump.  Never destroy the module/session and expose the
     * shell while TitanVideo still owns the game GS/VIF state. */
    if (TitanGameRuntime_IsActive() && !TitanGameRuntime_End())
        return;
    TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_DESTROY);
    TitanAppRuntime_StopInitThread(0);
    TitanAppRuntime_ResetSessionMemory();
    g_app_runtime.active = 0;
    g_app_runtime.launching = 0;
    g_app_runtime.exit_requested = 0;
    g_app_runtime.launch_phase = TITAN_APP_LAUNCH_IDLE;
    TitanAppRuntime_ReleaseStorageExclusive();
    TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_IDLE);
}

void TitanAppRuntime_QuiesceForSystemShutdown(void)
{
    if (!TitanAppRuntime_OwnsShell()) return;
    g_app_runtime.exit_reason = TITAN_APP_EXIT_SYSTEM_SHUTDOWN;
    g_app_runtime.exit_requested = 1;
    TitanAppRuntime_SetLifecycle(TITAN_APP_LIFECYCLE_EXIT_REQUEST);
    if (g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME &&
        g_app_runtime.game_thread_started && !g_app_runtime.game_thread_done)
    {
        TitanGameRuntime_RequestExit();
#if defined(__EE__) || defined(_EE)
        {
            uint64_t started = TitanTime_NowMilliseconds();
            while (!g_app_runtime.game_thread_done &&
                TitanTime_NowMilliseconds() - started < TITAN_APP_GAME_SHUTDOWN_TIMEOUT_MS)
                DelayThread(1000);
        }
        if (!g_app_runtime.game_thread_done)
            TitanAppRuntime_StopGameThread(1);
#endif
    }
    TitanAppRuntime_Close();
}

int TitanAppRuntime_OwnsExclusiveVideo(void)
{
    return g_app_runtime.active &&
        g_app_runtime.runtime_class == TITAN_APP_RUNTIME_CLASS_GAME &&
        g_app_runtime.game_thread_started && !g_app_runtime.game_thread_done;
}

void TitanAppRuntime_GetContext(TitanAppRuntimeContext *context)
{
    if (context == 0) return;
    memset(context, 0, sizeof(*context));
    context->lifecycle = g_app_runtime.lifecycle;
    context->runtime_class = g_app_runtime.runtime_class;
    context->exit_reason = g_app_runtime.exit_reason;
    context->allocation_count = g_app_runtime.allocation_count;
    context->allocation_bytes = g_app_runtime.allocation_bytes;
    context->storage_exclusive = g_app_runtime.storage_exclusive != 0;
    context->active = g_app_runtime.active != 0;
    context->launching = g_app_runtime.launching != 0;
    context->fullscreen = g_app_runtime.fullscreen != 0;
    TitanAppRuntime_CopyText(context->title, sizeof(context->title), g_app_runtime.title);
    TitanAppRuntime_CopyText(context->root_path, sizeof(context->root_path), g_app_runtime.root_path);
}

int TitanAppRuntime_IsActive(void)
{
    return g_app_runtime.active;
}

int TitanAppRuntime_IsLaunching(void)
{
    return g_app_runtime.launching;
}

int TitanAppRuntime_OwnsShell(void)
{
    return g_app_runtime.active || g_app_runtime.launching;
}

int TitanAppRuntime_IsFullscreen(void)
{
    return g_app_runtime.active && g_app_runtime.fullscreen;
}

const char *TitanAppRuntime_GetTitle(void)
{
    return g_app_runtime.title;
}

const char *TitanAppRuntime_GetLastError(void)
{
    return g_app_runtime.last_error[0] != '\0'
        ? g_app_runtime.last_error
        : "Titan App runtime error";
}
