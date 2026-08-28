#include <ultra64.h>
#include <R4300.h>
#ifdef NATIVE_PORT
#include <stdio.h>
#include <stdlib.h>
#endif
#include "unk_0CC4C0.h"
#include "image.h"
#include "lvl.h"
#include "bondtypes.h"
#include "othermodemicrocode.h"
#include "gbi_extension.h"

#ifdef NATIVE_PORT
extern struct sImageTableEntry *genericimage;
extern struct sImageTableEntry *impactimages;
extern struct sImageTableEntry *explosion_smokeimages;
extern struct sImageTableEntry *scattered_explosions;

typedef struct TexDebugFireEvent {
    uint32_t seq;
    int32_t global_timer;
    const char *source_tag;
    const void *tconfig;
    uint32_t index;
    uint16_t width;
    uint16_t height;
    uint8_t level;
    uint8_t format;
    uint8_t depth;
    uint8_t arg2;
    int8_t arg3;
    uint8_t ulst;
} TexDebugFireEvent;

static const char *g_TexDebugSourceTag = NULL;
static int g_TexDebugTraceFire = -1;
static int g_TexDebugTraceSelect = -1;
static TexDebugFireEvent g_TexDebugFireRing[128];
static uint32_t g_TexDebugFireSeq = 0;
static uint32_t g_TexDebugFireRingHead = 0;
static uint32_t g_TexDebugFireRingCount = 0;

static int texDebugTraceFireEnabled(void)
{
    if (g_TexDebugTraceFire < 0) {
        g_TexDebugTraceFire = getenv("GE007_TRACE_FIRE_TEX") ? 1 : 0;
    }

    return g_TexDebugTraceFire != 0;
}

static int texDebugTraceSelectEnabled(void)
{
    if (g_TexDebugTraceSelect < 0) {
        g_TexDebugTraceSelect = getenv("GE007_TRACE_TEXSELECT") ? 1 : 0;
    }

    return g_TexDebugTraceSelect != 0;
}

/* -----------------------------------------------------------------------
 * TMEM-1 format-reinterpretation census (GE007_TRACE_TMEM_REINTERP).
 *
 * Off by default. One-shot per (texnum, table-fmt, table-depth) tuple. Logs
 * every texSelect where the requesting image-table entry's declared fmt/siz
 * (tconfig->format/depth) disagrees with the catalogued pool entry's fmt/siz
 * (tex->gbiformat/tex->depth) -- i.e. the "reinterpretation" class the Dam
 * render deep-dive (DAM_PARITY 4.7 / DEEP_DIVE TMEM-1) asked to enumerate
 * game-wide. Also records the (informational) tex==NULL fallback case, where
 * texSelect decodes by the table entry because the pool has no catalogued
 * entry.
 *
 * The census sits at the ONLY choke point where both the table entry and the
 * pool entry are simultaneously in hand: texSelect (retail 0x7F076D68). The
 * 2026-07-18 session's G_SETTEX-path attempt saw only a texturenum + a stale
 * dummy tile, never the table intent -- this is its real home.
 * --------------------------------------------------------------------- */
static const char *texDebugImageTableName(struct sImageTableEntry *entry, int *index_out);

static int g_TexDebugTmemReinterp = -1;
#define TEX_REINTERP_SEEN_MAX 512
static struct {
    uint16_t texnum;
    uint8_t table_fmt;
    uint8_t table_depth;
    uint8_t pooled;   /* 1 = tex!=NULL branch, 0 = tex==NULL fallback */
} g_TexReinterpSeen[TEX_REINTERP_SEEN_MAX];
static int g_TexReinterpSeenCount = 0;

static int texDebugTmemReinterpEnabled(void)
{
    if (g_TexDebugTmemReinterp < 0) {
        g_TexDebugTmemReinterp = getenv("GE007_TRACE_TMEM_REINTERP") ? 1 : 0;
    }
    return g_TexDebugTmemReinterp != 0;
}

/* Returns 1 the first time this (texnum,fmt,depth,pooled) tuple is seen. */
static int texReinterpMarkSeen(uint16_t texnum, uint8_t fmt, uint8_t depth, uint8_t pooled)
{
    int i;
    for (i = 0; i < g_TexReinterpSeenCount; i++) {
        if (g_TexReinterpSeen[i].texnum == texnum &&
            g_TexReinterpSeen[i].table_fmt == fmt &&
            g_TexReinterpSeen[i].table_depth == depth &&
            g_TexReinterpSeen[i].pooled == pooled) {
            return 0;
        }
    }
    if (g_TexReinterpSeenCount < TEX_REINTERP_SEEN_MAX) {
        g_TexReinterpSeen[g_TexReinterpSeenCount].texnum = texnum;
        g_TexReinterpSeen[g_TexReinterpSeenCount].table_fmt = fmt;
        g_TexReinterpSeen[g_TexReinterpSeenCount].table_depth = depth;
        g_TexReinterpSeen[g_TexReinterpSeenCount].pooled = pooled;
        g_TexReinterpSeenCount++;
    }
    return 1;
}

static void texDebugCensusReinterp(struct sImageTableEntry *tconfig,
                                   struct tex *tex,
                                   u32 texture_index)
{
    uint16_t texnum;
    uint8_t pool_fmt;
    uint8_t pool_depth;
    int mismatch;
    int table_name_index;
    const char *table_name;

    if (!texDebugTmemReinterpEnabled() || tconfig == NULL) {
        return;
    }

    if (tex != NULL) {
        texnum = (uint16_t)tex->texturenum;
        pool_fmt = (uint8_t)tex->gbiformat;
        pool_depth = (uint8_t)tex->depth;
        mismatch = (tconfig->format != pool_fmt) || (tconfig->depth != pool_depth);
        if (!mismatch) {
            return; /* pooled + agrees -> not a reinterpretation candidate */
        }
        if (!texReinterpMarkSeen(texnum, tconfig->format, tconfig->depth, 1)) {
            return;
        }
        table_name = texDebugImageTableName(tconfig, &table_name_index);
        /* resolved_fmt/depth = what texSelect actually emits into the DL.
         * When pooled, texSelect uses the POOL fmt/siz (retail-identical:
         * the RDP executes the emitted tile), so the table entry's differing
         * fmt/siz is discarded metadata, NOT a raw-TMEM reinterpretation. */
        fprintf(stderr,
                "[TMEM-REINTERP] gt=%d tag=%s src=%s[%d] texnum=%u pooled=1 "
                "table_fmt=%u table_depth=%u pool_fmt=%u pool_depth=%u "
                "resolved_fmt=%u resolved_depth=%u wh=%ux%u lutmode=%u\n",
                g_GlobalTimer,
                g_TexDebugSourceTag != NULL ? g_TexDebugSourceTag : "-",
                table_name, table_name_index,
                texnum,
                tconfig->format, tconfig->depth,
                pool_fmt, pool_depth,
                pool_fmt, pool_depth,
                tconfig->width, tconfig->height,
                tex->lutmodeindex);
        fflush(stderr);
    } else {
        /* Unpooled: texSelect decodes by the table entry itself. Census it so
         * the "table fmt drives the decode" cases are enumerated too (these are
         * the only ones where tconfig->format actually reaches the decoder). */
        if (!texReinterpMarkSeen((uint16_t)texture_index, tconfig->format, tconfig->depth, 0)) {
            return;
        }
        table_name = texDebugImageTableName(tconfig, &table_name_index);
        fprintf(stderr,
                "[TMEM-REINTERP] gt=%d tag=%s src=%s[%d] texnum=%u pooled=0 "
                "table_fmt=%u table_depth=%u pool_fmt=- pool_depth=- "
                "resolved_fmt=%u resolved_depth=%u wh=%ux%u lutmode=-\n",
                g_GlobalTimer,
                g_TexDebugSourceTag != NULL ? g_TexDebugSourceTag : "-",
                table_name, table_name_index,
                texture_index,
                tconfig->format, tconfig->depth,
                tconfig->format, tconfig->depth,
                tconfig->width, tconfig->height);
        fflush(stderr);
    }
}

static const char *texDebugImageTableName(struct sImageTableEntry *entry, int *index_out)
{
    if (index_out != NULL) {
        *index_out = -1;
    }

    if (entry == NULL) {
        return "-";
    }

    if (genericimage != NULL && entry >= genericimage && entry < genericimage + 1) {
        if (index_out != NULL) *index_out = (int)(entry - genericimage);
        return "genericimage";
    }
    if (impactimages != NULL && entry >= impactimages && entry < impactimages + 20) {
        if (index_out != NULL) *index_out = (int)(entry - impactimages);
        return "impactimages";
    }
    if (explosion_smokeimages != NULL && entry >= explosion_smokeimages && entry < explosion_smokeimages + 6) {
        if (index_out != NULL) *index_out = (int)(entry - explosion_smokeimages);
        return "explosion_smokeimages";
    }
    if (scattered_explosions != NULL && entry >= scattered_explosions && entry < scattered_explosions + 5) {
        if (index_out != NULL) *index_out = (int)(entry - scattered_explosions);
        return "scattered_explosions";
    }

    return "-";
}

static u32 texResolveNativeImageIndex(u32 raw_index)
{
    if (raw_index < NUM_TEXTURES) {
        return raw_index;
    }

    {
        u32 swapped = ((raw_index & 0x000000ffU) << 24) |
                      ((raw_index & 0x0000ff00U) << 8) |
                      ((raw_index & 0x00ff0000U) >> 8) |
                      ((raw_index & 0xff000000U) >> 24);

        if (swapped < NUM_TEXTURES) {
            return swapped;
        }
    }

    return raw_index;
}

#define TEX_NATIVE_IMAGESEG_TOKEN_PREFIX 0xABCD0000U
#define TEX_NATIVE_IMAGESEG_TOKEN_ID_MASK 0x0000FFFFU

static uintptr_t texNativeTextureImageForSettimg(u32 texture_index,
                                                 struct tex *tex,
                                                 const struct sImageTableEntry *tconfig)
{
    if (texture_index < NUM_TEXTURES) {
        return (uintptr_t)(TEX_NATIVE_IMAGESEG_TOKEN_PREFIX |
                           (texture_index & TEX_NATIVE_IMAGESEG_TOKEN_ID_MASK));
    }

    if (tex != NULL && tex->data != NULL) {
        return (uintptr_t)tex->data;
    }

    return (uintptr_t)tconfig->index;
}

void texDebugPushSourceTag(const char *tag)
{
    g_TexDebugSourceTag = tag;
}

void texDebugPopSourceTag(void)
{
    g_TexDebugSourceTag = NULL;
}

static void texDebugRecordFireSelect(struct sImageTableEntry *tconfig, u32 arg2, s32 arg3, u32 ulst)
{
    TexDebugFireEvent *event;

    if (!texDebugTraceFireEnabled() || g_TexDebugSourceTag == NULL || tconfig == NULL) {
        return;
    }

    event = &g_TexDebugFireRing[g_TexDebugFireRingHead];
    event->seq = ++g_TexDebugFireSeq;
    event->global_timer = g_GlobalTimer;
    event->source_tag = g_TexDebugSourceTag;
    event->tconfig = tconfig;
    event->index = tconfig->index;
    event->width = tconfig->width;
    event->height = tconfig->height;
    event->level = tconfig->level;
    event->format = tconfig->format;
    event->depth = tconfig->depth;
    event->arg2 = (uint8_t)arg2;
    event->arg3 = (int8_t)arg3;
    event->ulst = (uint8_t)ulst;

    g_TexDebugFireRingHead = (g_TexDebugFireRingHead + 1) %
                             (sizeof(g_TexDebugFireRing) / sizeof(g_TexDebugFireRing[0]));
    if (g_TexDebugFireRingCount < sizeof(g_TexDebugFireRing) / sizeof(g_TexDebugFireRing[0])) {
        g_TexDebugFireRingCount++;
    }

    fprintf(stderr,
            "[FIRE-TEXSELECT] seq=%u gt=%d src=%s img=%p index=%u wh=%ux%u lvl=%u fmt=%u depth=%u arg2=%u arg3=%d ulst=%u\n",
            event->seq,
            event->global_timer,
            event->source_tag,
            event->tconfig,
            event->index,
            event->width,
            event->height,
            event->level,
            event->format,
            event->depth,
            event->arg2,
            event->arg3,
            event->ulst);
    fflush(stderr);
}

void texDebugDumpRecentFireEvents(FILE *fp)
{
    uint32_t count;
    uint32_t start;
    uint32_t i;

    if (fp == NULL || g_TexDebugFireRingCount == 0) {
        return;
    }

    count = g_TexDebugFireRingCount;
    start = (g_TexDebugFireRingHead + (sizeof(g_TexDebugFireRing) / sizeof(g_TexDebugFireRing[0])) - count) %
            (sizeof(g_TexDebugFireRing) / sizeof(g_TexDebugFireRing[0]));

    fprintf(fp, "[FIRE-TEX-DUMP] recent=%u\n", count);

    for (i = 0; i < count; i++) {
        const TexDebugFireEvent *event =
            &g_TexDebugFireRing[(start + i) % (sizeof(g_TexDebugFireRing) / sizeof(g_TexDebugFireRing[0]))];

        fprintf(fp,
                "[FIRE-TEX-DUMP] seq=%u gt=%d src=%s img=%p index=%u wh=%ux%u lvl=%u fmt=%u depth=%u arg2=%u arg3=%d ulst=%u\n",
                event->seq,
                event->global_timer,
                event->source_tag ? event->source_tag : "?",
                event->tconfig,
                event->index,
                event->width,
                event->height,
                event->level,
                event->format,
                event->depth,
                event->arg2,
                event->arg3,
                event->ulst);
    }
    fflush(fp);
}
#else
void texDebugPushSourceTag(const char *tag) { (void)tag; }
void texDebugPopSourceTag(void) {}
static void texDebugRecordFireSelect(struct sImageTableEntry *tconfig, u32 arg2, s32 arg3, u32 ulst)
{
    (void)tconfig;
    (void)arg2;
    (void)arg3;
    (void)ulst;
}
#endif

s32 is_less_than_certain_power_of_2(int number)
{
    if (number < 2) {
        return 0;
    }

    if (number < 3) {
        return 1;
    }

    if (number < 5) {
        return 2;
    }

    if (number < 9) {
        return 3;
    }

    if (number < 0x11) {
        return 4;
    }

    if (number < 0x21) {
        return 5;
    }

    if (number < 0x41) {
        return 6;
    }

    if (number < 0x81) {
        return 7;
    }

    return 8;
}


s32 ceil8000(s32 arg0)
{
    f32 temp_f0;
    s32 temp_f10;

    temp_f0 = 32768.0f / ((((s32) (arg0 + 15)) / 16) * 16);
    temp_f10 = temp_f0;

    return (temp_f10 & 0xFFFFFFFF)
         + ((s32) ((temp_f0 - temp_f10) + 0.999999f));
}


s32 ceil4000(s32 arg0)
{
    f32 temp_f0;
    s32 temp_f10;

    temp_f0 = 16384.0f / ((((s32) (arg0 + 7)) / 8) * 8);
    temp_f10 = temp_f0;

    return (temp_f10 & 0xFFFFFFFF)
         + ((s32) ((temp_f0 - temp_f10) + 0.999999f));
}


s32 ceil2000(s32 arg0)
{
    f32 temp_f0;
    s32 temp_f10;

    temp_f0 = 8192.0f / ((((s32) (arg0 + 3)) / 4) * 4);
    temp_f10 = temp_f0;

    return (temp_f10 & 0xFFFFFFFF)
         + ((s32) ((temp_f0 - temp_f10) + 0.999999f));
}


s32 ceil1000(s32 arg0)
{
    f32 temp_f0;
    s32 temp_f10;

    temp_f0 = 4096.0f / ((((s32) (arg0 + 3)) / 4) * 4);
    temp_f10 = temp_f0;

    return (temp_f10 & 0xFFFFFFFF)
         + ((s32) ((temp_f0 - temp_f10) + 0.999999f));
}


s32 sub_GAME_7F0767D8(s32 arg0, s32 arg1, s32 arg2)
{
    s32 ret;
    ret = 0;

    if (arg2 <= 0) { arg2 = 1; }

    while (arg2 > 0)
    {
        ret += ((arg0 + 15) / 16) * 4 * arg1;
        arg2--;
        if (arg0 >= 2) { arg0 >>= 1; }
        if (arg1 >= 2) { arg1 >>= 1; }
    }
    return ret;
}


s32 sub_GAME_7F076848(s32 arg0, s32 arg1, s32 arg2)
{
    s32 ret;
    ret = 0;

    if (arg2 <= 0) { arg2 = 1; }

    while (arg2 > 0)
    {
        ret += ((arg0 + 7) / 8) * 4 * arg1;
        arg2--;
        if (arg0 >= 2) { arg0 >>= 1; }
        if (arg1 >= 2) { arg1 >>= 1; }
    }
    return ret;
}


s32 sub_GAME_7F0768B8(s32 arg0, s32 arg1, s32 arg2)
{
    s32 ret;
    ret = 0;

    if (arg2 <= 0) { arg2 = 1; }

    while (arg2 > 0)
    {
        ret += ((arg0 + 3) / 4) * 4 * arg1;
        arg2--;
        if (arg0 >= 2) { arg0 >>= 1; }
        if (arg1 >= 2) { arg1 >>= 1; }
    }
    return ret;
}


s32 sub_GAME_7F076928(s32 arg0, s32 arg1, s32 arg2)
{
    s32 ret;
    ret = 0;

    if (arg2 <= 0) { arg2 = 1; }

    while (arg2 > 0)
    {
        ret += ((arg0 + 3) / 4) * 4 * arg1;
        arg2--;
        if (arg0 >= 2) { arg0 >>= 1; }
        if (arg1 >= 2) { arg1 >>= 1; }
    }
    return ret;
}


void texSetRenderMode(Gfx **gdlptr, s32 arg1, s32 numcycles, s32 arg3)
{
    Gfx *gdl = *gdlptr;

    if (numcycles == 1)
    {
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);

        switch (arg1)
        {
            default:
            case 1:
                if (arg3)
                {
                    if (arg3 >= 2)
                    {
                        gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_DECAL, G_RM_AA_ZB_OPA_DECAL2);
                    }
                    else
                    {
                        gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
                    }
                }
                else
                {
                    gDPSetRenderMode(gdl++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
                }
                break;

            case 2:
                if (arg3)
                {
                    if (arg3 >= 2)
                    {
                        gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2);
                    }
                    else
                    {
                        gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
                    }
                }
                else
                {
                    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
                }
                break;

            case 3:
                if (arg3)
                {
                    gDPSetRenderMode(gdl++, G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2);
                }
                else
                {
                    gDPSetRenderMode(gdl++, G_RM_AA_TEX_EDGE, G_RM_AA_TEX_EDGE2);
                }
                break;

            case 4:
                if (arg3)
                {
                    gDPSetRenderMode(gdl++, G_RM_ZB_CLD_SURF, G_RM_ZB_CLD_SURF2);
                }
                else
                {
                    gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
                }
                break;
        }
    }
    else
    {
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_2CYCLE);

        switch (arg1)
        {
            default:
            case 1:
                if (arg3)
                {
                    if (arg3 >= 2)
                    {
                        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_OPA_DECAL2);
                    }
                    else
                    {
                        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
                    }
                }
                else
                {
                    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_OPA_SURF2);
                }
                break;

            case 2:
                if (arg3)
                {
                    if (arg3 >= 2)
                    {
                        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2);
                    }
                    else
                    {
                        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_XLU_SURF2);
                    }
                }
                else
                {
                    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_XLU_SURF2);
                }
                break;

            case 3:
                if (arg3)
                {
                    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_TEX_EDGE2);
                }
                else
                {
                    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_TEX_EDGE2);
                }
                break;

            case 4:
                if (arg3)
                {
                    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_ZB_CLD_SURF2);
                }
                else
                {
                    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_CLD_SURF2);
                }
                break;
        }
    }

    *gdlptr = gdl;
}


/**
 * Perfect Dark texSelect.
 * NTSC address 0x7F076D68.
*/
void texSelect(Gfx **gdlptr, struct sImageTableEntry *tconfig, u32 arg2, s32 arg3, u32 ulst)
{    
	Gfx *gdl;
    struct tex *tex;
    s32 tile;

    texDebugRecordFireSelect(tconfig, arg2, arg3, ulst);

    gdl = *gdlptr;

    if (tconfig == NULL)
    {
        texSetRenderMode(&gdl, arg2, 1, arg3);

        if (arg3 >= 2)
        {
            gSPTextureL(gdl++, 0xffff, 0xffff, 0, arg3, G_TX_RENDERTILE, G_ON);
        }
        else
        {
            gSPTextureL(gdl++, 0xffff, 0xffff, 0, 0, G_TX_RENDERTILE, G_ON);
        }

        gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    }
    else
    {
        s32 width;
        s32 height;
        
		u8 format;
		u8 depth;
	        s32 stack_padding;
			s32 lutmode;
			s32 depth2 = G_IM_SIZ_16b;
			s32 lrs = 0;
	        s32 sp138 = 0;
			s32 line = 0;
#ifdef NATIVE_PORT
		        u32 texture_index = texResolveNativeImageIndex(tconfig->index);
#endif

		        u16* aa;

	        tex = NULL;
        
        width = tconfig->width;
        height = tconfig->height;
        
	        if ((u32) tconfig->index < NUM_TEXTURES)
	        {
#ifdef NATIVE_PORT
	            /* On PC, tconfig->index is the texture enum value directly.
	             * texLoadFromTextureNum takes the number and loads via g_Textures[]. */
	            texLoadFromTextureNum(texture_index, NULL);
#else
	            texLoad((s32 *)tconfig, NULL);
#endif
	        }
#ifdef NATIVE_PORT
	        else if (texture_index < NUM_TEXTURES)
	        {
	            texLoadFromTextureNum(texture_index, NULL);
	        }
#endif

	#ifdef NATIVE_PORT
	        /* On PC, tconfig->index IS the texture number — use it directly.
	         * On N64, the index is a ROM address and (PHYS_TO_K0(addr))[-4]
	         * reads the texture number from ROM metadata preceding the data. */
	        tex = texFindInPool(texture_index, NULL);
	        if (texDebugTraceSelectEnabled()) {
	            int table_index;
	            const char *table_name = texDebugImageTableName(tconfig, &table_index);
	            fprintf(stderr,
	                    "[TEXSELECT] gt=%d entry=%p src=%s[%d] tag=%s img=%u resolved_img=%u "
	                    "table_wh=%ux%u table_level=%u "
	                    "table_fmt=%u table_depth=%u arg2=%u arg3=%d ulst=%u "
	                    "tex=%p texnum=%u tex_wh=%ux%u tex_maxlod=%u tex_fmt=%u tex_lutmode=%u "
	                    "tex_depth=%u special=%u data=%p\n",
	                    g_GlobalTimer,
	                    (void *)tconfig,
	                    table_name,
	                    table_index,
	                    g_TexDebugSourceTag != NULL ? g_TexDebugSourceTag : "-",
	                    tconfig->index,
	                    texture_index,
	                    tconfig->width,
                    tconfig->height,
                    tconfig->level,
                    tconfig->format,
                    tconfig->depth,
                    arg2,
                    arg3,
                    ulst,
                    (void *)tex,
                    tex != NULL ? tex->texturenum : 0,
                    tex != NULL ? tex->width : 0,
                    tex != NULL ? tex->height : 0,
                    tex != NULL ? tex->maxlod : 0,
                    tex != NULL ? tex->gbiformat : 0,
                    tex != NULL ? tex->lutmodeindex : 0,
                    tex != NULL ? tex->depth : 0,
                    tex != NULL ? tex->unk0c_02 : 0,
                    tex != NULL ? tex->data : NULL);
            fflush(stderr);
        }
#else
        aa = PHYS_TO_K0(tconfig->index);
        tex = texFindInPool((aa)[-4], NULL);
#endif

#ifdef NATIVE_PORT
        texDebugCensusReinterp(tconfig, tex, texture_index);
#endif

#ifdef NATIVE_PORT
        /* Static game textures must reach fast3d as IMAGESEG-style tokens so
         * the renderer uses the native-word RGBA decoder and stable cache key.
         * Non-static effect-frame entries keep the older pointer/raw fallback. */
        uintptr_t texture_image = texNativeTextureImageForSettimg(texture_index,
                                                                  tex,
                                                                  tconfig);
#endif

        if (tconfig->level == 0)
        {
            if (tex != NULL)
            {
                format = tex->gbiformat;
                depth = tex->depth;
                lutmode = tex->lutmodeindex << G_MDSFT_TEXTLUT;                
            }
            else
            {
                format = tconfig->format;
                depth = tconfig->depth;
            }

            switch (depth)
            {
                default:
                    break;
                case G_IM_SIZ_32b:
                    depth2 = G_IM_SIZ_32b;
                    lrs = sub_GAME_7F076928(width, height, 1) - 1;
                    sp138 = ceil1000(width);
                    line = (s32) (width + 3) >> 2;
                    break;
                case G_IM_SIZ_16b:
                    depth2 = G_IM_SIZ_16b;
                    lrs = sub_GAME_7F0768B8(width, height, 1) - 1;
                    sp138 = ceil2000(width);
                    line = (s32) (width + 3) >> 2;
                    break;
                case G_IM_SIZ_8b:
                    depth2 = G_IM_SIZ_16b;
                    lrs = sub_GAME_7F076848(width, height, 1) - 1;
                    sp138 = ceil4000(width);
                    line = (s32) (width + 7) >> 3;
                    break;
                case G_IM_SIZ_4b:
                    depth2 = G_IM_SIZ_16b;
                    lrs = sub_GAME_7F0767D8(width, height, 1) - 1;
                    sp138 = ceil8000(width);
                    line = (s32) (width + 0xF) >> 4;
                    break;
            }

            texSetRenderMode(&gdl, arg2, 1, arg3);

            if (arg3 >= 2)
            {
                gSPTextureL(gdl++, 0xffff, 0xffff, 0, arg3, G_TX_RENDERTILE, G_ON);
            }
            else
            {
                gSPTextureL(gdl++, 0xffff, 0xffff, 0, 0, G_TX_RENDERTILE, G_ON);
            }
    
            gDPSetTextureLOD(gdl++, G_TL_TILE);

            switch (format)
            {
                case G_IM_FMT_RGBA:
                    gDPSetCombineMode(gdl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
                break;

                case G_IM_FMT_IA:
                    gDPSetCombineMode(gdl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
                break;

                case G_IM_FMT_I:
                    gDPSetCombineMode(gdl++, G_CC_MODULATEI, G_CC_MODULATEI);
                break;

                case G_IM_FMT_CI:
                    switch (lutmode)
                    {
                        case G_TT_RGBA16:
                            gDPSetCombineMode(gdl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
                        break;

                        case G_TT_IA16:
                            gDPSetCombineMode(gdl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
                        break;
                    }
                break;
            }

            gDPSetTextureImage(gdl++, format, depth2, 1,
#ifdef NATIVE_PORT
                texture_image
#else
                tconfig->index
#endif
            );

            gDPSetTile(gdl++, format, depth2, 0, 0, G_TX_LOADTILE, 0,
                tconfig->flagsT, G_TX_NOMASK, G_TX_NOLOD,
                tconfig->flagsS, G_TX_NOMASK, G_TX_NOLOD);

            gDPLoadSync(gdl++);

            gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, lrs, sp138);
            
            gDPPipeSync(gdl++);

            if (format == G_IM_FMT_CI)
            {
                u32 a3 = lrs + 1;
				u32 t0 = (0x3ff - tex->unk0a) < a3 ? (0x3ff - tex->unk0a) : 0;

				a3 -= t0;
                
                gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 0x0100, G_TX_LOADTILE, 0,
                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD,
                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
                gDPLoadSync(gdl++);

                gDPLoadTLUT07(gdl++, a3, t0, tex->unk0a + a3, t0);
                gDPPipeSync(gdl++);

                gDPSetTextureLUT(gdl++, lutmode);
            }
            else
            {
                gDPSetTextureLUT(gdl++, G_TT_NONE);
            }

            gDPSetTile(gdl++, format, depth, line, 0, G_TX_RENDERTILE, 0,
                tconfig->flagsT, is_less_than_certain_power_of_2(height), G_TX_NOLOD,
                tconfig->flagsS, is_less_than_certain_power_of_2(width), G_TX_NOLOD);

            gDPSetTileSize(gdl++, G_TX_RENDERTILE, ulst, ulst, (((width - 1) << 2) + ulst), (((height - 1) << 2) + ulst));
        }
        else
        {
            s32 tmem; // spd0
			s32 lod;
			u8 format;
			u8 depth;
			s32 lutmode;
			s32 depth2;
			s32 lrs;

            tmem = 0;
            lod = (s32)tconfig->level;

            if (tex != NULL)
            {
                format = tex->gbiformat;
                depth = tex->depth;
                lutmode = tex->lutmodeindex << G_MDSFT_TEXTLUT;
            }
            else
            {
                format = tconfig->format;
                depth = tconfig->depth;
            }

            if ((tex != NULL) && (tex->unk0c_02))
            {
                texGetDepthAndSize(tex, &depth2, &lrs);
            }
            else
            {
                switch (depth)
                {
                    case G_IM_SIZ_32b:
                        depth2 = G_IM_SIZ_32b;
                        lrs = sub_GAME_7F076928(width, height, lod) - 1;
                    break;
                    
                    case G_IM_SIZ_16b:
                        depth2 = G_IM_SIZ_16b;
                        lrs = sub_GAME_7F0768B8(width, height, lod) - 1;
                    break;
                    
                    case G_IM_SIZ_8b:
                        depth2 = G_IM_SIZ_16b;
                        lrs = sub_GAME_7F076848(width, height, lod) - 1;
                    break;
                    
                    case G_IM_SIZ_4b:
                        depth2 = G_IM_SIZ_16b;
                        lrs = sub_GAME_7F0767D8(width, height, lod) - 1;
                    break;
                }
            }

            texSetRenderMode(&gdl, arg2, 2, arg3);

            if (arg3 >= 2)
            {
                gSPTextureL(gdl++, 0xffff, 0xffff, lod - 1, arg3, G_TX_RENDERTILE, G_ON);
            }
            else
            {
                gSPTexture(gdl++, 0xffff, 0xffff, lod - 1, G_TX_RENDERTILE, G_ON);
            }

            gDPSetTextureLOD(gdl++, G_TL_LOD);

            // line 417
            switch (format)
            {
                case G_IM_FMT_RGBA:
                    gDPSetCombineMode(gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);
                break;

                case G_IM_FMT_IA:
                    gDPSetCombineMode(gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);
                break;

                case G_IM_FMT_I:
                    gDPSetCombineMode(gdl++, G_CC_TRILERP, G_CC_MODULATEI2);
                break;

                case G_IM_FMT_CI:
                    switch (lutmode)
                    {
                        case G_TT_RGBA16:
                            gDPSetCombineMode(gdl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
                        break;

                        case G_TT_IA16:
                            gDPSetCombineMode(gdl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
                        break;
                    }
                break;
            }

            gDPSetTextureImage(gdl++, format, depth2, 1,
#ifdef NATIVE_PORT
                texture_image
#else
                tconfig->index
#endif
            );

            gDPSetTile(gdl++, format, depth2, 0, 0, G_TX_LOADTILE, 0,
                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD,
                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
            
            gDPLoadSync(gdl++);

            gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, lrs, 0);

            gDPPipeSync(gdl++);

            if (format == 2)
            {
                u32 a2 = lrs + 1;
				u32 a3 = (0x3ff - tex->unk0a) < a2 ? (0x3ff - tex->unk0a) : 0;

				a2 -= a3;
                
                gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 0x0100, G_TX_LOADTILE, 0,
                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD,
                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
                gDPLoadSync(gdl++);

                gDPLoadTLUT07(gdl++, a2, a3, tex->unk0a + a2, a3);
                gDPPipeSync(gdl++);

                gDPSetTextureLUT(gdl++, lutmode);
            }
            else
            {
                gDPSetTextureLUT(gdl++, G_TT_NONE);
            }
    
            for (tile = 0; tile < lod; tile++)
            {
                s32 line = 0;

                if (tile > 0)
                {
                    if ((tex != NULL) && (tex->unk0c_02))
                    {
                        width = texGetWidthAtLod(tex, tile);
                        height = texGetHeightAtLod(tex, tile);
                    }
                    else
                    {
                        if (width >= 2)
                        {
                            width >>= 1;
                        }
                        
                        if (height >= 2)
                        {
                            height >>= 1;
                        }
                    }
                }

                switch (depth)
                {
                    default:
                        break;
                    case G_IM_SIZ_32b:
                        line = (s32) (width + 3) / 4;
                        break;
                    case G_IM_SIZ_16b:
                        line = (s32) (width + 3) / 4;
                        break;
                    case G_IM_SIZ_8b:
                        line = (s32) (width + 7) / 8;
                        break;
                    case G_IM_SIZ_4b:
                        line = (s32) (width + 0xF) / 16;
                        break;
                }
                
                gDPSetTile(gdl++, format, depth, line, tmem, tile, 0,
                    tconfig->flagsT, is_less_than_certain_power_of_2(height), tile,
                    tconfig->flagsS, is_less_than_certain_power_of_2(width), tile);

                gDPSetTileSize(gdl++, tile, ulst, ulst, (((width - 1) << 2) + ulst), (((height - 1) << 2) + ulst));

                tmem += line * height;
            }
        }
        
    }

    *gdlptr = gdl;
}



/**
 * Unreferenced.
*/
void sub_GAME_7F077BB8(s32 arg0, s32 arg1, s32 arg2, s32 arg3)
{
    return;
}
