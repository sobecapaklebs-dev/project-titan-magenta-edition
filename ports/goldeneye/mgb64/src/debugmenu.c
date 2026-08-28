#include <ultra64.h>
#include "debugmenu.h"
#include "random.h"
#include "vi.h"
#include "game/dyn.h"


#ifdef LEFTOVERDEBUG

// This is a 128 x 21 sprite of characers in greyscale at one byte per pixel.
// Each character is 4 x 7 pixels. There's 3 rows with 32 characters per row.
u32 g_DebugMenuTexture[682] = {0}; /* MGB64: ROM-derived font data removed; supply from your own ROM */

s32 g_DebugMenuTextStartX = 5;
s32 g_DebugMenuTextStartY = 1;
s32 g_DebugMenuTextCurrentX = 24;
s32 g_DebugMenuTextCurrentY = 16;
Gfx g_DebugMenuTextureDisplayList[] = {
    gsDPPipeSync(),
    gsDPSetCycleType(G_CYC_1CYCLE),
    gsDPSetColorDither(G_CD_DISABLE),
    gsDPSetRenderMode(IM_RD | CVG_DST_FULL | ZMODE_OPA | FORCE_BL | GBL_c1(G_BL_CLR_MEM, G_BL_A_IN, G_BL_CLR_IN, G_BL_1), IM_RD | CVG_DST_FULL | ZMODE_OPA | FORCE_BL | GBL_c2(G_BL_CLR_MEM, G_BL_A_IN, G_BL_CLR_IN, G_BL_1)),
    gsDPSetCombineLERP(PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT),
    gsDPSetTexturePersp(G_TP_NONE),
    //gsDPSetTextureLUT(G_TT_NONE),
    gsDPSetAlphaCompare(G_AC_NONE),
    gsDPLoadTextureBlock(&g_DebugMenuTexture, G_IM_FMT_IA, G_IM_SIZ_8b, 128, 21, 0, (G_TX_NOMIRROR | G_TX_WRAP), (G_TX_NOMIRROR | G_TX_WRAP), G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsDPLoadSync(),
    gsSPEndDisplayList()
};
character g_DebugMenuTextBuffer[80][35] = {0};
Gfx g_DHudFgGbiPtrs[32] = {0};
Gfx g_DHudBgGbiPtrs[32] = {0};
s32 g_DebugMenuCurrentColorIndex = 0;

#define ANSI_COLOR_CODE_FG_BLACK   "\x1B[30m"
#define ANSI_COLOR_CODE_FG_RED     "\x1B[31m"
#define ANSI_COLOR_CODE_FG_GREEN   "\x1B[32m"
#define ANSI_COLOR_CODE_FG_YELLOW  "\x1B[33m"
#define ANSI_COLOR_CODE_FG_BLUE    "\x1B[34m"
#define ANSI_COLOR_CODE_FG_MAGENTA "\x1B[35m"
#define ANSI_COLOR_CODE_FG_CYAN    "\x1B[36m"
#define ANSI_COLOR_CODE_FG_WHITE   "\x1B[37m"

#define ANSI_COLOR_CODE_BG_BLACK   "\x1B[40m"
#define ANSI_COLOR_CODE_BG_RED     "\x1B[41m"
#define ANSI_COLOR_CODE_BG_GREEN   "\x1B[42m"
#define ANSI_COLOR_CODE_BG_YELLOW  "\x1B[43m"
#define ANSI_COLOR_CODE_BG_BLUE    "\x1B[44m"
#define ANSI_COLOR_CODE_BG_MAGENTA "\x1B[45m"
#define ANSI_COLOR_CODE_BG_CYAN    "\x1B[46m"
#define ANSI_COLOR_CODE_BG_WHITE   "\x1B[47m"

const char *g_DebugMenuUnusedStrings[] = {
    ANSI_COLOR_CODE_FG_RED     ANSI_COLOR_CODE_BG_BLACK,
    ANSI_COLOR_CODE_FG_WHITE   ANSI_COLOR_CODE_BG_BLACK,
    ANSI_COLOR_CODE_FG_GREEN   ANSI_COLOR_CODE_BG_BLACK,
    ANSI_COLOR_CODE_FG_YELLOW  ANSI_COLOR_CODE_BG_BLACK,
    ANSI_COLOR_CODE_FG_BLUE    ANSI_COLOR_CODE_BG_BLACK,
    ANSI_COLOR_CODE_FG_MAGENTA ANSI_COLOR_CODE_BG_BLACK,
    ANSI_COLOR_CODE_FG_CYAN    ANSI_COLOR_CODE_BG_BLACK,

    ANSI_COLOR_CODE_FG_WHITE   ANSI_COLOR_CODE_BG_BLUE,
    ANSI_COLOR_CODE_FG_RED     ANSI_COLOR_CODE_BG_BLUE,
    ANSI_COLOR_CODE_FG_GREEN   ANSI_COLOR_CODE_BG_BLUE,
    ANSI_COLOR_CODE_FG_YELLOW  ANSI_COLOR_CODE_BG_BLUE,
    ANSI_COLOR_CODE_FG_BLACK   ANSI_COLOR_CODE_BG_BLUE,
    ANSI_COLOR_CODE_FG_MAGENTA ANSI_COLOR_CODE_BG_BLUE,
    ANSI_COLOR_CODE_FG_CYAN    ANSI_COLOR_CODE_BG_BLUE,

    ANSI_COLOR_CODE_FG_WHITE   ANSI_COLOR_CODE_BG_RED,
    ANSI_COLOR_CODE_FG_BLACK   ANSI_COLOR_CODE_BG_RED,
    ANSI_COLOR_CODE_FG_GREEN   ANSI_COLOR_CODE_BG_RED,
    ANSI_COLOR_CODE_FG_YELLOW  ANSI_COLOR_CODE_BG_RED,
    ANSI_COLOR_CODE_FG_BLUE    ANSI_COLOR_CODE_BG_RED,
    ANSI_COLOR_CODE_FG_MAGENTA ANSI_COLOR_CODE_BG_RED,
    ANSI_COLOR_CODE_FG_CYAN    ANSI_COLOR_CODE_BG_RED,

    ANSI_COLOR_CODE_FG_WHITE   ANSI_COLOR_CODE_BG_MAGENTA,
    ANSI_COLOR_CODE_FG_RED     ANSI_COLOR_CODE_BG_MAGENTA,
    ANSI_COLOR_CODE_FG_GREEN   ANSI_COLOR_CODE_BG_MAGENTA,
    ANSI_COLOR_CODE_FG_YELLOW  ANSI_COLOR_CODE_BG_MAGENTA,
    ANSI_COLOR_CODE_FG_BLUE    ANSI_COLOR_CODE_BG_MAGENTA,
    ANSI_COLOR_CODE_FG_BLACK   ANSI_COLOR_CODE_BG_MAGENTA,
    ANSI_COLOR_CODE_FG_CYAN    ANSI_COLOR_CODE_BG_MAGENTA,

    ANSI_COLOR_CODE_FG_WHITE   ANSI_COLOR_CODE_BG_GREEN,
    ANSI_COLOR_CODE_FG_RED     ANSI_COLOR_CODE_BG_GREEN,
    ANSI_COLOR_CODE_FG_BLACK   ANSI_COLOR_CODE_BG_GREEN,
    ANSI_COLOR_CODE_FG_YELLOW  ANSI_COLOR_CODE_BG_GREEN
};
Gfx g_DebugMenuEndDisplayList = gsSPEndDisplayList();
Gfx g_DebugMenuNoOp = gsDPNoOp();
Gfx g_DebugMenuPrimitiveColor = gsDPSetPrimColor(0, 0, 255, 255, 255, 0);
Gfx g_DebugMenuEnvironmentColor = gsDPSetEnvColor(0, 0, 0, 0);
u32 g_DebugMenuPercentage = 0xFF; // Static?
#endif

/**
 * Removed
 */
#ifdef LEFTOVERDEBUG
u32 debmenu7000AD80(s32 arg0, s32 arg1) {
    // Removed
    return 0;
}
#endif

/**
 * Removed
 */
#ifdef LEFTOVERDEBUG
u32 debmenu7000AD90(s32 arg0, s32 arg1) {
    // Removed
    return 0;
}
#endif

/**
 * Removed
 */
#ifdef LEFTOVERDEBUG
void debmenu7000ADA0(void) {
    // Removed
}
#endif

/**
 * Removed
 */
void debmenu7000ADA8(void) {
    // Removed
}

void debmenuInit(void) {
    #ifdef LEFTOVERDEBUG
    debmenuReset();
    #endif
}

#ifdef LEFTOVERDEBUG
void debmenuWriteCharAtPos(s32 x, s32 y, unsigned char c) {
    s32 i;
    for (i = 0; i < 32; i++) {
        if ((g_DebugMenuPrimitiveColor.words.w1 == g_DHudFgGbiPtrs[i].words.w1) &&
            (g_DebugMenuEnvironmentColor.words.w1 == g_DHudBgGbiPtrs[i].words.w1)) {
            goto end;
        }
    }
    g_DHudFgGbiPtrs[g_DebugMenuCurrentColorIndex] = g_DebugMenuPrimitiveColor;
    g_DHudBgGbiPtrs[g_DebugMenuCurrentColorIndex] = g_DebugMenuEnvironmentColor;
    g_DebugMenuCurrentColorIndex = ((g_DebugMenuCurrentColorIndex + 1) % 32);
    i = g_DebugMenuCurrentColorIndex;
end:
    g_DebugMenuTextBuffer[x][y].chr = c;
    g_DebugMenuTextBuffer[x][y].color = i;
}
#endif

void debmenuResetPosition(void) {
    #ifdef LEFTOVERDEBUG
    g_DebugMenuTextCurrentX = g_DebugMenuTextStartX;
    g_DebugMenuTextCurrentY = g_DebugMenuTextStartY;
    #endif
}

void debmenuReset(void) {
    #ifdef LEFTOVERDEBUG
    s32 x;
    s32 y;
    for (y = 0; y < 35; y++) {
        for (x = 0; x < 80; x++) {
            debmenuWriteCharAtPos(x, y, '\0');
        }
    }
    debmenuResetPosition();
    debmenu7000ADA0();
    g_DebugMenuCurrentColorIndex = 0;
    #endif
}

/**
 * Removed.
 * Called from debmenu7000AF98
 */
#ifdef LEFTOVERDEBUG
void debmenu7000AF84(s32 x1, s32 y1, s32 x2, s32 y2) {
    // Removed
}
#endif


void debmenu7000AF98(s32 height)
{
#ifdef LEFTOVERDEBUG
    s32 x;
    s32 y;

    for (y = 34, height += y - 1; y--; height-- )
    {
        if ((height >= 0) && (height < 35))
        {
            for ( x = 0; x != 80; x++)
            {
                debmenu7000AF84(x, height, x, y);
            }
        }
        else
        {
            for (x = 0; x != 80; x++)
            {
                debmenuWriteCharAtPos(x, y, 0);
            }
        }
    }
#endif
}


/*
* Address: 0x7000b040
*/
void debmenuSetPos(s32 x, s32 y) {
    #ifdef LEFTOVERDEBUG
    x += g_DebugMenuTextStartX;
    y += g_DebugMenuTextStartY;
    g_DebugMenuTextCurrentX = x;
    g_DebugMenuTextCurrentY = y;
    #endif
}

void debmenuSetFgColour(s32 r, s32 g, s32 b, s32 a) {
    #ifdef LEFTOVERDEBUG
    g_DebugMenuPrimitiveColor.words.w1 = ((r << 24) | (g << 16) | (b << 8) | (255 - a));
    #endif
}

void debmenuSetEnvColor(s32 r, s32 g, s32 b, s32 a) {
    #ifdef LEFTOVERDEBUG
    g_DebugMenuEnvironmentColor.words.w1 = ((r << 24) | (g << 16) | (b << 8) | (255 - a));
    #endif
}

void debmenuWriteChar(unsigned char c) {
    #ifdef LEFTOVERDEBUG
    s32 width = ((viGetX() - 13) / 4);
    s32 height = ((viGetY() - 10) / 7);
    if ((c == '\0') || ((c >= ' ') && (c <= '~'))) {
        debmenuWriteCharAtPos(g_DebugMenuTextCurrentX, g_DebugMenuTextCurrentY, c);
    }
    g_DebugMenuTextCurrentX++;
    if ((c == '\r') || (c == '\n') || (g_DebugMenuTextCurrentX >= width)) {
        g_DebugMenuTextCurrentX = g_DebugMenuTextStartX;
        g_DebugMenuTextCurrentY++;
        if (g_DebugMenuTextCurrentY >= height) {
            g_DebugMenuTextCurrentY = g_DebugMenuTextStartY;
        }
    }
    #endif
}

void debmenuSetPositionAndWriteChar(s32 x, s32 y, unsigned char c)
{
    #ifdef LEFTOVERDEBUG
    debmenuSetPos(x, y);
    debmenuWriteChar(c);
    #endif
}

void debmenuPrintString(const unsigned char *str) {
    #ifdef LEFTOVERDEBUG
    while (*str != '\0') {
        debmenuWriteChar(*str++);
    }
    #endif
}

void debmenuSetPositionAndWriteString(s32 x, s32 y, const unsigned char *str) {
    #ifdef LEFTOVERDEBUG
    debmenuSetPos(x, y);
    while (*str != '\0') {
        debmenuWriteChar(*str++);
    }
    #endif
}


/*
* Address: 0x7000b27c
*/
Gfx *debmenuDraw(Gfx *gdl)
{
#if defined(LEFTOVERDEBUG)

	s32 y;
    s32 x;
	s32 appliedpaletteindex;
	s32 needed;
	s32 available;
	Gfx *gdl2;

	// Calculate how much space is needed in the display list
	// based on the number of characters to draw and the number
	// of times the colours will be changed.
	gdl2 = gdl;
	appliedpaletteindex = -1;

	for (y = 0; y < 35; y++)
    {
		for (x = 0; x < 80; x++)
        {
			u32 c = g_DebugMenuTextBuffer[x][y].chr;
			s32 paletteindex = g_DebugMenuTextBuffer[x][y].color;

			if (c != '\0')
            {
				if (paletteindex != appliedpaletteindex)
                {
					gdl2 += 2;
					appliedpaletteindex = paletteindex;
				}

				if (1)
                {
				    gdl2 += 3;
                }
			}
		}
	}

	// Make sure there'll be a least 256 GBI commands free (2KB)
	available = dynGetFreeGfx(gdl) - 256 * sizeof(Gfx);
	needed = (u32)((uintptr_t)gdl2 - (uintptr_t)gdl);

	if (needed <= 0) { // shouldn't be possible
		return gdl;
	}

    if(1)
	{
		s32 appliedpaletteindex = -1;

		// Write a "g_DebugMenuPercentage" (out of 255) into a global variable
		// which shows how much of the displaylist will be committed,
		// provided 2KB is kept free.
		if (available <= 0)
        {
			// There's already less than 2KB free in the display list
			g_DebugMenuPercentage = 0;
		}
        else if (needed > available)
        {
			// The display list would end with less than 2KB free,
			// so calculate the g_DebugMenuPercentage
			g_DebugMenuPercentage = available * 255 / needed;
		}
        else
        {
			// The display list would end with at least 2KB free,
			// so the displaylist can be committed in full
			g_DebugMenuPercentage = 256;
		}

		gSPDisplayList(gdl++, g_DebugMenuTextureDisplayList);

        // Build the display list for real this time.
		// Regardless of the availability checks above, just stop when
		// there's less than 1KB of free space... sort of. It still writes
		// the colour change commands, but the debug HUD doesn't exactly
		// draw rainbows so it's no big deal.
		for (y = 0; y < 35; y++)
        {
            s32 x;
			for (x = 0; x < 80; x++)
            {
				u32 c = g_DebugMenuTextBuffer[x][y].chr;
				s32 paletteindex = g_DebugMenuTextBuffer[x][y].color;

                if(x == 80);

                if (c != '\0')
                {
                    if (paletteindex != appliedpaletteindex)
                    {
                        *gdl = g_DHudFgGbiPtrs[paletteindex];
                        gdl++;
						*gdl = g_DHudBgGbiPtrs[paletteindex];
                        gdl++;
						appliedpaletteindex = paletteindex;
					}

#ifndef DEBUGMENU
                    if ((randomGetNext() & 0xFF) < g_DebugMenuPercentage )
#else
                    if(1)
#endif
                    {
				    	if (dynGetFreeGfx(gdl) >= 1024)
                        {

				    		gSPTextureRectangle(gdl++,
                                // Screen coords to draw at
                                x * 4 * 4,
                                y * 7 * 4,
                                x * 4 * 4 + 4 * 4,
                                y * 7 * 4 + 7 * 4,
                                G_TX_RENDERTILE,
                                // Sprite X and Y positions
                                ((c - ' ') % 32) * 4 * 32,
                                ((s32)(c - ' ') >> 5) * 7 * 32,
                                1024,
                                1024);
				    	}
                    }
				}

                if (appliedpaletteindex == 1);
                if(y);
			}
		}
	}

#endif // defined(LEFTOVERDEBUG)

    return gdl;
}
