#include <ultra64.h>
#include <ramrom.h>
#include <memp.h>
#include "image.h"
#include "image_bank.h"
#ifdef NATIVE_PORT
#include <stdio.h>
#include "gfx_pc.h"
#endif

// bss
//8008D0A0
u8* img_curpos;
//8008D0A4
u32 img_curdatatable;
//8008D0A8
s32 img_bitcount;
//8008D0AC
s32 dword_CODE_bss_8008D0AC;
//8008D0B0;
s32 globalbank_rdram_offset;
//8008D0B4;
s32 *pGlobalimagetable;
//8008D0B8;
struct sImageTableEntry *genericimage;
//8008D0BC
struct sImageTableEntry *impactimages;
//8008D0C0
struct sImageTableEntry *explosion_smokeimages;
//8008D0C4
struct sImageTableEntry *scattered_explosions;
//8008D0C8
struct sImageTableEntry *flareimage1;
//8008D0CC
struct sImageTableEntry *flareimage2;
//8008D0D0
struct sImageTableEntry *flareimage3;
//8008D0D4
struct sImageTableEntry *flareimage4;
//8008D0D8
struct sImageTableEntry *flareimage5;
//8008D0DC
struct sImageTableEntry *ammo9mmimage;
//8008D0E0
struct sImageTableEntry *rifleammoimage;
//8008D0E4
struct sImageTableEntry *shotgunammoimage;
//8008D0E8
struct sImageTableEntry *knifeammoimage;
//8008D0EC
struct sImageTableEntry *glaunchammoimage;
//8008D0F0
struct sImageTableEntry *rocketammoimage;
//8008D0F4
struct sImageTableEntry *genericmineammoimage;
//8008D0F8
struct sImageTableEntry *grenadeammoimage;
//8008D0FC
struct sImageTableEntry *magnumammoimage;
//8008D100
struct sImageTableEntry *goldengunammoimage;
//8008D104
struct sImageTableEntry *remotemineammoimage;
//8008D108
struct sImageTableEntry *timedmineammoimage;
//8008D10C
struct sImageTableEntry *proxmineammoimage;
//8008D110
struct sImageTableEntry *tankammoimage;
//8008D114;
struct sImageTableEntry *crosshairimage;
//8008D118
struct sImageTableEntry *betacrosshairimage;
//8008D11C
struct sImageTableEntry *glassoverlayimage;
//8008D120
struct sImageTableEntry *monitorimages;
//8008D124
struct sImageTableEntry *skywaterimages;
//8008D128
struct sImageTableEntry *mainfolderimages;
//8008D12C
struct sImageTableEntry *mpradarimages;
//8008D130
struct sImageTableEntry *mpcharselimages;
//8008D134
struct sImageTableEntry *mpstageselimages;


extern u8* _GlobalimagetableSegmentRomStart;


static void texPreloadImageEntry(struct sImageTableEntry *entry)
{
#ifdef NATIVE_PORT
    texLoadFromTextureNum(entry->index, 0);
#else
    texLoad((u32 *)entry, 0);
#endif
}


#ifdef NATIVE_PORT
void texSetBitstring(u8 *pos) {
    img_curpos = pos;
    img_curdatatable = 0;
    img_bitcount = 0;
}
#else
void texSetBitstring(s32 pos) {
    img_curpos = pos;
    img_curdatatable = 0;
    img_bitcount = 0;
}
#endif



u32 texReadBits(s32 bitCount)
{
    if (img_bitcount < bitCount)
    {
        do
        {
            img_curdatatable = (*img_curpos | (img_curdatatable << 8));
            img_curpos++;
            img_bitcount = img_bitcount + 8;
        } while (img_bitcount < bitCount);
    }
    
    img_bitcount -= bitCount;
    return (img_curdatatable >> img_bitcount) & ((1 << bitCount) - 1);
}



extern u32* _GlobalimagetableSegmentStart;
extern u32* _GlobalimagetableSegmentEnd;
extern void* s_genericimage;
extern void* s_impactimages;
extern void* s_explosion_smokeimages;
extern void* s_scattered_explosions;
extern void* s_flareimage1;
extern void* s_flareimage2;
extern void* s_flareimage3;
extern void* s_flareimage4;
extern void* s_flareimage5;
extern void* s_ammo9mmimage;
extern void* s_rifleammoimage;
extern void* s_shotgunammoimage;
extern void* s_knifeammoimage;
extern void* s_glammoimage;
extern void* s_rocketammoimage;
extern void* s_genericmineammoimage;
extern void* s_grenadeammoimage;
extern void* s_magnumammoimage;
extern void* s_goldengunammoimage;
extern void* s_remotemineammoimage;
extern void* s_timedmineammoimage;
extern void* s_proxmineammoimage;
extern void* s_tankammoimage;
extern void* s_crosshairimage;
extern void* s_betacrosshairimage;
extern void* s_glassoverlayimage;
extern void* s_monitorimages;
extern void* s_skywaterimages;
extern void* s_mainfolderimages;
extern void* s_mpradarimages;
extern void* s_mpcharselimages;
extern void* s_mpstageselimages;

extern Gfx globalDL_0x000[];
extern Gfx globalDL_0x078[];
extern Gfx globalDL_0x120[];
extern Gfx globalDL_0x1c8[];
extern Gfx globalDL_0x270[];
extern Gfx globalDL_0x318[];
extern Gfx globalDL_0x3c0[];
extern Gfx globalDL_0x468[];
extern Gfx globalDL_0x510[];
extern Gfx globalDL_0x5b8[];
extern Gfx globalDL_0x660[];
extern Gfx globalDL_0x708[];
extern Gfx globalDL_0x7b0[];
extern Gfx globalDL_0x858[];
extern Gfx globalDL_0x900[];
extern Gfx globalDL_0x9a8[];
extern Gfx globalDL_0xa50[];


static void texPreloadGlobalDisplayList(Gfx *dl)
{
#ifdef NATIVE_PORT
    Gfx *cmd = dl;
    size_t count = 1;

    while ((((cmd->words.w0 >> 24) & 0xff) != (u8)G_ENDDL) && count < 256) {
        cmd++;
        count++;
    }

    gfx_register_extra_pc_dl(dl, count * sizeof(Gfx));
#endif

    texLoadFromDisplayList(dl, 0);
}


static void texPreloadGlobalDisplayLists(void)
{
#ifdef NATIVE_PORT
#define GLOBAL_DL_PTR(name) (name)
#else
#define GLOBAL_DL_PTR(name) ((Gfx *)(globalbank_rdram_offset + (u32)(name)))
#endif

    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x000));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x078));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x120));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x1c8));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x270));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x318));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x3c0));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x468));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x510));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x5b8));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x660));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x708));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x7b0));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x858));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x900));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0x9a8));
    texPreloadGlobalDisplayList(GLOBAL_DL_PTR(globalDL_0xa50));

#undef GLOBAL_DL_PTR
}

#ifdef NATIVE_PORT
/* HUD image-entry validation (backlog M2.6 / audit R6). HUD draw paths
 * (ammo icons, watch readouts) historically trusted compiled sImageTableEntry
 * metadata: a NULL global or a garbage entry drew nothing, or fed bogus
 * dimensions into texSelect/display_image_at_position. Applies to NATIVE
 * table entries only, where index is a plain texture number (< the compiled
 * table); N64-style entries carrying ROM addresses in index must not be
 * passed here.
 *
 * Max dimension: the largest entry in the compiled global image table is
 * 128 px (0x80 — folder/backdrop plates; ammo icons are <= 14x28). 160 allows
 * 25% headroom for future legitimate HUD art while still rejecting most
 * garbage in the u8 width/height fields. */
#define PORT_HUD_IMAGE_MAX_DIM 160

s32 portValidateImageEntry(const struct sImageTableEntry *img, const char *label)
{
    static s32 s_invalidImageLogCount = 0;
    const char *reason = NULL;

    if (img == NULL) {
        reason = "NULL entry";
    } else if (img->width == 0 || img->height == 0) {
        reason = "nonpositive dimensions";
    } else if (img->width > PORT_HUD_IMAGE_MAX_DIM || img->height > PORT_HUD_IMAGE_MAX_DIM) {
        reason = "dimensions exceed HUD max";
    } else if (img->index >= (u32)(texGetCompiledTableCount() - 1)) {
        /* texLoad reads g_Textures[index] and g_Textures[index + 1] */
        reason = "texture index outside compiled table";
    }

    if (reason == NULL) {
        return 1;
    }

    if (s_invalidImageLogCount++ < 16) {
        fprintf(stderr,
                "[HUD][RENDER-HEALTH] image entry invalid (%s): %s"
                " (entry=%p index=%u w=%u h=%u)\n",
                label != NULL ? label : "-", reason,
                (const void *)img,
                img != NULL ? img->index : 0,
                img != NULL ? img->width : 0,
                img != NULL ? img->height : 0);
        fflush(stderr);
    }

    return 0;
}
#endif

static void texPreloadRuntimeImageEntries(void)
{
    s32 i;

    texPreloadImageEntry(genericimage);

    for (i = 0; i < 6; i++)
    {
        texPreloadImageEntry(&explosion_smokeimages[i]);
    }

    for (i = 0; i < 5; i++)
    {
        texPreloadImageEntry(&scattered_explosions[i]);
    }

#ifdef NATIVE_PORT
    texPreloadImageEntry(flareimage1);
    texPreloadImageEntry(flareimage2);
    texPreloadImageEntry(flareimage3);
    texPreloadImageEntry(flareimage4);
    texPreloadImageEntry(flareimage5);
#endif
}

void texReset(void)
{
    u32 size;

#ifdef NATIVE_PORT
    texResetPaletteCache();

    /* On PC, GlobalImageTable.c is compiled directly into the binary.
     * No ROM copy needed — just point the image table pointers at the
     * compiled-in data arrays. The N64 version copies from ROM and rebases
     * via globalbank_rdram_offset, but on PC the symbols are already valid. */
    globalbank_rdram_offset = 0;
    pGlobalimagetable = NULL;

    /* CRITICAL: The extern declarations above use `void*` but the actual
     * definitions in GlobalImageTable.c are ARRAYS (e.g. sImageTableEntry[]).
     * On 64-bit, `(void*)symbol` reads the first 8 bytes of array data as a
     * pointer (garbage), while `(void*)&symbol` gets the array's address.
     * The N64 path (line 260+) correctly uses `&` — the PC path must too. */
    genericimage = (void *)&s_genericimage;
    impactimages = (void *)&s_impactimages;
    explosion_smokeimages = (void *)&s_explosion_smokeimages;
    scattered_explosions = (void *)&s_scattered_explosions;
    flareimage1 = (void *)&s_flareimage1;
    flareimage2 = (void *)&s_flareimage2;
    flareimage3 = (void *)&s_flareimage3;
    flareimage4 = (void *)&s_flareimage4;
    flareimage5 = (void *)&s_flareimage5;
    ammo9mmimage = (void *)&s_ammo9mmimage;
    rifleammoimage = (void *)&s_rifleammoimage;
    shotgunammoimage = (void *)&s_shotgunammoimage;
    knifeammoimage = (void *)&s_knifeammoimage;
    glaunchammoimage = (void *)&s_glammoimage;
    rocketammoimage = (void *)&s_rocketammoimage;
    genericmineammoimage = (void *)&s_genericmineammoimage;
    grenadeammoimage = (void *)&s_grenadeammoimage;
    magnumammoimage = (void *)&s_magnumammoimage;
    goldengunammoimage = (void *)&s_goldengunammoimage;
    remotemineammoimage = (void *)&s_remotemineammoimage;
    timedmineammoimage = (void *)&s_timedmineammoimage;
    proxmineammoimage = (void *)&s_proxmineammoimage;
    tankammoimage = (void *)&s_tankammoimage;
    crosshairimage = (void *)&s_crosshairimage;
    betacrosshairimage = (void *)&s_betacrosshairimage;
    glassoverlayimage = (void *)&s_glassoverlayimage;
    monitorimages = (void *)&s_monitorimages;
    skywaterimages = (void *)&s_skywaterimages;
    mainfolderimages = (void *)&s_mainfolderimages;
    mpradarimages = (void *)&s_mpradarimages;
    mpcharselimages = (void *)&s_mpcharselimages;
    mpstageselimages = (void *)&s_mpstageselimages;

    texPreloadGlobalDisplayLists();
    texPreloadRuntimeImageEntries();

#else /* N64 */
    size = (u32)&_GlobalimagetableSegmentEnd - (u32)&_GlobalimagetableSegmentStart;
    pGlobalimagetable = mempAllocBytesInBank(size + 0x1000, MEMPOOL_STAGE);
    pGlobalimagetable = ((u32)pGlobalimagetable + 0xFFFU) & 0xFFFFF000;

    romCopy(pGlobalimagetable, &_GlobalimagetableSegmentRomStart, size);

    globalbank_rdram_offset = (u32)pGlobalimagetable + 0xFE000000;
    genericimage = (void *) (globalbank_rdram_offset + (u32)&s_genericimage);
    impactimages = (void *) (globalbank_rdram_offset + (u32)&s_impactimages);
    explosion_smokeimages = (void *) (globalbank_rdram_offset + (u32)&s_explosion_smokeimages);
    scattered_explosions = (void *) (globalbank_rdram_offset + (u32)&s_scattered_explosions);
    flareimage1 = (void *) (globalbank_rdram_offset + (u32)&s_flareimage1);
    flareimage2 = (void *) (globalbank_rdram_offset + (u32)&s_flareimage2);
    flareimage3 = (void *) (globalbank_rdram_offset + (u32)&s_flareimage3);
    flareimage4 = (void *) (globalbank_rdram_offset + (u32)&s_flareimage4);
    flareimage5 = (void *) (globalbank_rdram_offset + (u32)&s_flareimage5);
    ammo9mmimage = (void *) (globalbank_rdram_offset + (u32)&s_ammo9mmimage);
    rifleammoimage = (void *) (globalbank_rdram_offset + (u32)&s_rifleammoimage);
    shotgunammoimage = (void *) (globalbank_rdram_offset + (u32)&s_shotgunammoimage);
    knifeammoimage = (void *) (globalbank_rdram_offset + (u32)&s_knifeammoimage);
    glaunchammoimage = (void *) (globalbank_rdram_offset + (u32)&s_glammoimage);
    rocketammoimage = (void *) (globalbank_rdram_offset + (u32)&s_rocketammoimage);
    genericmineammoimage = (void *) (globalbank_rdram_offset + (u32)&s_genericmineammoimage);
    grenadeammoimage = (void *) (globalbank_rdram_offset + (u32)&s_grenadeammoimage);
    magnumammoimage = (void *) (globalbank_rdram_offset + (u32)&s_magnumammoimage);
    goldengunammoimage = (void *) (globalbank_rdram_offset + (u32)&s_goldengunammoimage);
    remotemineammoimage = (void *) (globalbank_rdram_offset + (u32)&s_remotemineammoimage);
    timedmineammoimage = (void *) (globalbank_rdram_offset + (u32)&s_timedmineammoimage);
    proxmineammoimage = (void *) (globalbank_rdram_offset + (u32)&s_proxmineammoimage);
    tankammoimage = (void *) (globalbank_rdram_offset + (u32)&s_tankammoimage);
    crosshairimage = (void *) (globalbank_rdram_offset + (u32)&s_crosshairimage);
    betacrosshairimage = (void *) (globalbank_rdram_offset + (u32)&s_betacrosshairimage);
    glassoverlayimage = (void *) (globalbank_rdram_offset + (u32)&s_glassoverlayimage);
    monitorimages = (void *) (globalbank_rdram_offset + (u32)&s_monitorimages);
    skywaterimages = (void *) (globalbank_rdram_offset + (u32)&s_skywaterimages);
    mainfolderimages = (void *) (globalbank_rdram_offset + (u32)&s_mainfolderimages);
    mpradarimages = (void *) (globalbank_rdram_offset + (u32)&s_mpradarimages);
    mpcharselimages = (void *) (globalbank_rdram_offset + (u32)&s_mpcharselimages);
    mpstageselimages = (void *) (globalbank_rdram_offset + (u32)&s_mpstageselimages);

    texPreloadGlobalDisplayLists();
    texPreloadRuntimeImageEntries();
#endif
}
