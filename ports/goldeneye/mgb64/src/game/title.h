#ifndef _INTRO_LOGOS_H_
#define _INTRO_LOGOS_H_
#include <ultra64.h>

extern struct s_display_list_something *barrelDisplayListPtr;
extern Gfx *gunbarrelgfxListPointer;
extern Mtx *matrixBufferRareLogo0;
extern Mtx *matrixBufferGunbarrel0;
extern Mtx *matrixBufferRareLogo1;
extern Mtx *matrixBufferRareLogo2;
extern Mtx *matrixBufferGunbarrel1;
extern Mtx *matrixBufferIntroBackdrop;
extern Mtx *matrixBufferIntroBond;
extern f32 x;
extern f32 y;
extern f32 g_TitleX;
extern f32 g_TitleY;
extern f32 titleTransitionX;
extern f32 titleTransitionY;
extern f32 D_8002A89C;
extern s16 word_CODE_bss_80069584;
extern u8 *dword_CODE_bss_80069588;
extern u8 *dword_CODE_bss_8006958C;
extern uintptr_t virtualaddress; /* pointer-sized on 64-bit PC */
extern s32 dword_CODE_bss_80069594;

extern u32 D_8002A7D0;

struct FolderSelect;

Gfx *sub_GAME_7F007CC8(Gfx *gdl, s32 arg1, struct FolderSelect *arg2, struct FolderSelect *arg3);
#endif
