#ifndef _TEXTRELATED_H_
#define _TEXTRELATED_H_
#include <ultra64.h>
#include "bondtypes.h"

enum TEXT_ORIENTATION {
    ROT_NORMAL = 0,
    ROT_90CW
};

#ifdef NATIVE_PORT
extern uintptr_t ptrFontBankGothic;
extern uintptr_t ptrFontBankGothicChars;
extern uintptr_t ptrFontZurichBold;
extern uintptr_t ptrFontZurichBoldChars;
extern uintptr_t ptrFontBankGothicRaw;
extern uintptr_t ptrFontZurichBoldRaw;
#else
extern s32 ptrFontBankGothic;
extern s32 ptrFontBankGothicChars;
extern s32 ptrFontZurichBold;
extern s32 ptrFontZurichBoldChars;
#endif

void textrelatedInit_REMOVED(void);
void load_font_tables(void);

struct fontchar {
    u32 index;
    s32 baseline;
    u32 height;
    u32 width;
    s32 kerningindex;
    u8 *pixeldata;
};

struct font {
	s32 kerning[13 * 13];
	struct fontchar chars[94]; // can be 135 in PAL
};

Gfx * microcode_constructor_related_to_menus(Gfx *, s32, s32, s32, s32, s32);
void textMeasure(s32 *textheight, s32 *textwidth, char *text, struct fontchar *font1, struct font *font2, s32 lineheight);

Gfx *microcode_constructor(Gfx *gdl);
#ifdef NATIVE_PORT
Gfx *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, uintptr_t second_font_table, uintptr_t first_font_table, s32 arg6, s16 view_x, s16 view_y, s32 arg9, s32 arga);
Gfx *textRenderGlow(Gfx *gdl, s32 *x, s32 *y, s8 *text, uintptr_t second_font_table, uintptr_t first_font_table, s32 arg6, u32 arg7, s16 view_x, s16 view_y, s32 arga, s32 argb);
#else
Gfx *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, s32 second_font_table, s32 first_font_table, s32 arg6, s16 view_x, s16 view_y, s32 arg9, s32 arga);
Gfx *textRenderGlow(Gfx *gdl, s32 *x, s32 *y, s8 *text, s32 second_font_table, s32 first_font_table, s32 arg6, u32 arg7, s16 view_x, s16 view_y, s32 arga, s32 argb);
#endif

Gfx *draw_blackbox_to_screen(Gfx *glist, s32 ulx, s32 uly, s32 lrx, s32 lry);
Gfx *combiner_bayer_lod_perspective(Gfx *gdl);
void setTextSpacingInverted(s32 spacing);
void setTextWordWrap(s32 flag);
void sub_GAME_7F0AEB64(s32 arg0, s8 *arg1, s8 *arg2, struct fontchar *arg3, struct font *arg4);
void setTextOverlapCorrection(s32 flag);

#endif
