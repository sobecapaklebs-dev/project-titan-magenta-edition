#ifndef PROJECT_NES_CORE_H
#define PROJECT_NES_CORE_H

#include <stdint.h>

#define PROJECT_NES_CORE_INPUT_A      0x01u
#define PROJECT_NES_CORE_INPUT_B      0x02u
#define PROJECT_NES_CORE_INPUT_SELECT 0x04u
#define PROJECT_NES_CORE_INPUT_START  0x08u
#define PROJECT_NES_CORE_INPUT_UP     0x10u
#define PROJECT_NES_CORE_INPUT_DOWN   0x20u
#define PROJECT_NES_CORE_INPUT_LEFT   0x40u
#define PROJECT_NES_CORE_INPUT_RIGHT  0x80u

#define PROJECT_NES_CORE_LOAD_OK                  1
#define PROJECT_NES_CORE_LOAD_INVALID             0
#define PROJECT_NES_CORE_LOAD_UNSUPPORTED_MAPPER -2
#define PROJECT_NES_CORE_LOAD_UNSUPPORTED_PRG    -3
#define PROJECT_NES_CORE_LOAD_UNSUPPORTED_CHR    -4
#define PROJECT_NES_CORE_LOAD_TRUNCATED_PRG      -5
#define PROJECT_NES_CORE_LOAD_TRUNCATED_CHR      -6
#define PROJECT_NES_CORE_LOAD_UNSUPPORTED_FORMAT -7

typedef enum ProjectNesCoreRomFormat
{
    PROJECT_NES_CORE_ROM_INES1 = 1,
    PROJECT_NES_CORE_ROM_NES2 = 2
} ProjectNesCoreRomFormat;

typedef enum ProjectNesCoreRegionHint
{
    PROJECT_NES_CORE_REGION_NTSC = 0,
    PROJECT_NES_CORE_REGION_PAL = 1,
    PROJECT_NES_CORE_REGION_UNKNOWN = 255
} ProjectNesCoreRegionHint;

typedef struct ProjectNesCoreRomInfo
{
    ProjectNesCoreRomFormat format;
    ProjectNesCoreRegionHint region_hint;
    uint32_t mapper;
    uint8_t submapper;
    /* Byte counts are normalized for classic iNES. They are zero for NES 2.0
     * because the common probe does not pretend to decode exponent/multiplier
     * size encoding; a mature provider may perform its own full NES2 parse. */
    uint32_t prg_rom_bytes;
    uint32_t chr_rom_bytes;
    uint8_t battery_backed;
    uint8_t has_trainer;
    uint8_t vertical_mirror;
} ProjectNesCoreRomInfo;

/* Header-only metadata inspection is platform-neutral and does not imply that
 * the active provider can execute the reported mapper/format. */
int ProjectNesCore_ProbeRom(const uint8_t *data, uint32_t size, ProjectNesCoreRomInfo *info);

/* Platform-neutral input passed from the Titan frontend to an emulator core.
 * Reserved Titan system chords never cross this boundary. */
typedef struct ProjectNesCoreInput
{
    uint8_t buttons;
} ProjectNesCoreInput;

typedef struct ProjectNesCoreVideoTarget
{
    uint16_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} ProjectNesCoreVideoTarget;

typedef struct ProjectNesCoreBackend
{
    const char *name;
    int (*load_rom)(void *context, uint8_t *data, uint32_t size);
    void (*reset)(void *context);
    void (*set_input)(void *context, const ProjectNesCoreInput *input);
    int (*run_frame)(void *context, const ProjectNesCoreVideoTarget *video);
    uint32_t (*frame_rate_millihz)(void *context);
    uint32_t (*next_audio_frames)(void *context);
    uint32_t (*render_audio_s16_stereo)(
        void *context,
        int16_t *samples,
        uint32_t capacity_frames
    );

    /* Optional battery-backed save RAM. The core owns emulation memory; the
     * frontend owns durable storage and lifecycle/flush policy. */
    uint32_t (*save_ram_size)(void *context);
    int (*save_ram_export)(void *context, uint8_t *data, uint32_t capacity);
    int (*save_ram_import)(void *context, const uint8_t *data, uint32_t size);
    int (*save_ram_dirty)(void *context);
    void (*save_ram_clear_dirty)(void *context);

    /* Optional metadata probe. A successful probe reports cartridge metadata;
     * it is not a promise that load_rom supports that mapper or file format. */
    int (*probe_rom)(void *context, const uint8_t *data, uint32_t size, ProjectNesCoreRomInfo *info);

    /* Optional provider lifecycle tail. A mature external core may allocate
     * resources that must be released before Titan frees ROM/app memory. */
    void (*shutdown)(void *context);
} ProjectNesCoreBackend;

#define PROJECT_NES_PROVIDER_ABI_VERSION 1u

typedef struct ProjectNesProviderDescriptor
{
    uint32_t abi_version;
    uint32_t struct_size;
    const char *provider_name;
    const char *license_spdx;
    const char *source_commit;
    const ProjectNesCoreBackend *backend;
    void *context;
} ProjectNesProviderDescriptor;

#endif
