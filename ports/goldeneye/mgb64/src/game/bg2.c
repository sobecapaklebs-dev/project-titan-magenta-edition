#include <ultra64.h>
#include <PR/os.h>
#include <PR/gbi.h>
#include <bondconstants.h>
#include <fr.h>
#include <memp.h>
#include "bg.h"
#include "bondview.h"
#include "debugmenu_handler.h"
#include "decompress.h"
#include "fog.h"
#include "lvl.h"
#include "math_ceil.h"
#include "matrixmath.h"
#include "player.h"
#include "explosions.h"
#include "unk_0BC530.h"

// new file, per EU

/**
 * Unreferenced.
*/
void sub_GAME_7F0BA5C0(Gfx *arg0, Gfx *arg1)
{
    Gfx *var_v0;
    Gfx *var_v1;

    var_v0 = arg0;

    while (var_v0 < arg1)
    {
        for (var_v1 = DL_LUT_PRIMARY_ADDFOG; var_v1->words.w0 != 0; var_v1 += 2)
        {
            if ((var_v1->words.w0 == var_v0->words.w0) && (var_v1->words.w1 == var_v0->words.w1))
            {
                *var_v0 = *(var_v1+1);
            }
        }

        var_v0++;
    }
}



void bgLoadFromDynamicCCRMLUT(Gfx *arg0, Gfx *arg1, enum CCRMLUT arg2)
{
#ifdef NATIVE_PORT
    /* PC: Room DL data is stored as raw big-endian N64 commands (8 bytes each).
     * The LUT entries are native-endian Gfx structs (16 bytes each on 64-bit).
     * We walk the raw bytes and compare the lower 32 bits of LUT w0/w1
     * against the big-endian u32 words in the room DL data. */
    static s32 D_80044DB0 = 0;
    static int ccrm_log = 0;
    int local_replacements = 0;
    int local_commands = 0;
    u8 *p = (u8 *)arg0;
    u8 *end = (u8 *)arg1;

    while ((end != NULL && p < end) || (end == NULL && p[0] != (u8)G_ENDDL)) {
        /* Read the N64 DL command as two big-endian u32 words */
        u32 dl_w0 = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        u32 dl_w1 = ((u32)p[4] << 24) | ((u32)p[5] << 16) | ((u32)p[6] << 8) | p[7];

        Gfx *lut = ptrDynamic_CC_RM_LUT[(s32)arg2];
        for (; (u32)lut->words.w0 != 0; lut += 2) {
            u32 lut_w0 = (u32)lut[0].words.w0;
            u32 lut_w1 = (u32)lut[0].words.w1;
            if (lut_w0 == dl_w0 && lut_w1 == dl_w1) {
                /* Replace with the next LUT entry (big-endian write) */
                u32 rep_w0 = (u32)lut[1].words.w0;
                u32 rep_w1 = (u32)lut[1].words.w1;
                if (getenv("GE007_VERBOSE") && ccrm_log < 3) {
                    printf("[CCRM_MATCH] lut=%d cmd#%d: 0x%08X_%08X → 0x%08X_%08X\n",
                           (int)arg2, local_commands, dl_w0, dl_w1, rep_w0, rep_w1);
                    fflush(stdout);
                }
                p[0] = (u8)(rep_w0 >> 24); p[1] = (u8)(rep_w0 >> 16);
                p[2] = (u8)(rep_w0 >>  8); p[3] = (u8)(rep_w0);
                p[4] = (u8)(rep_w1 >> 24); p[5] = (u8)(rep_w1 >> 16);
                p[6] = (u8)(rep_w1 >>  8); p[7] = (u8)(rep_w1);
                D_80044DB0++;
                local_replacements++;
            }
        }
        local_commands++;
        p += 8; /* N64 DL commands are 8 bytes each */
    }
    if (getenv("GE007_VERBOSE") && ccrm_log < 5) {
        printf("[CCRM_LUT] index=%d commands=%d replacements=%d total_global=%d\n",
               (int)arg2, local_commands, local_replacements, D_80044DB0);
        /* Log first LUT entry for comparison */
        Gfx *first_lut = ptrDynamic_CC_RM_LUT[(s32)arg2];
        printf("  LUT[0]: w0=0x%08X w1=0x%08X (search)  sizeof(Gfx)=%lu\n",
               (u32)first_lut[0].words.w0, (u32)first_lut[0].words.w1,
               (unsigned long)sizeof(Gfx));
        printf("  LUT[1]: w0=0x%08X w1=0x%08X (replace)\n",
               (u32)first_lut[1].words.w0, (u32)first_lut[1].words.w1);
        fflush(stdout);
        ccrm_log++;
    }
#else
    Gfx *var_v0;
    Gfx *var_v1;

    static s32 D_80044DB0 = 0;

    var_v0 = arg0;

    while (((arg1 != NULL) && (var_v0 < arg1)) || ((arg1 == NULL) && (((s8*)var_v0)[0] != (s8)G_ENDDL)))
    {
        for (var_v1 = ptrDynamic_CC_RM_LUT[(s32)arg2]; var_v1->words.w0 != 0; var_v1 += 2)
        {
            if ((var_v1->words.w0 == var_v0->words.w0) && (var_v1->words.w1 == var_v0->words.w1))
            {
                D_80044DB0 += 1;

                *var_v0 = *(var_v1+1);
            }
        }

        var_v0++;
    }
#endif
}
