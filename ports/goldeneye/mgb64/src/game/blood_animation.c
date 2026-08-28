#include <ultra64.h>
#include <fr.h>
#include "dyn.h"
#include "title.h"
#include "blood_decrypt.h"
#include "player.h"
#include <PR/os.h>
#ifdef NATIVE_PORT
#include "boot.h"
#include "decompress.h"
#include "rom_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#define BLOOD_IMAGE_BYTES 0x1E00
#define BLOOD_ANIMATION_BYTES 2524

u8 die_blood_image_1[BLOOD_ANIMATION_BYTES] = {0}; /* MGB64: ROM-derived texture data removed; supply from your own ROM */

u8 die_blood_image_end = 0;

#ifdef NATIVE_PORT
#define BLOOD_CSEGMENT_DECOMPRESSED_BYTES 0x3c550U
#define BLOOD_ANIMATION_VADDR_START 0x8002bb30U

/*
 * The original game stores the evolving gunbarrel blood mask in dyn memory.
 * On the native renderer, that transient arena can be recycled aggressively
 * enough that long-running frontend captures occasionally hand Metal a stale
 * texture address. Keep the authored image generation path intact, but copy
 * the current 96x80 I4 payload into a stable upload buffer before SETTIMG.
 */
static u8 g_BloodOverlayUploadBuffer[BLOOD_IMAGE_BYTES];
static u8 g_BloodAnimationData[BLOOD_ANIMATION_BYTES];
static s32 g_BloodAnimationLoadState = 0;
static s32 g_BloodAnimationFrameCount = 0;
static s32 g_BloodTraceEnabled = -1;
static s32 g_BloodTraceFrame = 0;

static s32 bloodTraceEnabled(void)
{
   if (g_BloodTraceEnabled < 0) {
      const char *env = getenv("GE007_TRACE_BLOOD_ANIM");
      g_BloodTraceEnabled = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
   }

   return g_BloodTraceEnabled != 0;
}

static s32 bloodValidateFrame(const u8 *stream, s32 size, s32 offset)
{
   s32 pos = offset;
   s32 rows = 0x60;
   s32 leading;

   if (stream == NULL || pos >= size) {
      return -1;
   }

   leading = stream[pos++];
   if (leading > 0x50) {
      return -1;
   }

   while (rows > 0) {
      s32 cmd;

      if (pos >= size) {
         return -1;
      }

      cmd = stream[pos++];

      if (cmd == 0xff) {
         s32 row_total = 0;

         while (TRUE) {
            s32 run;

            if (pos >= size) {
               return -1;
            }

            run = stream[pos++];
            if (run == 0xff) {
               break;
            }

            row_total += run;
            if (row_total > 0x50) {
               return -1;
            }
         }

         rows--;
      } else {
         s32 filled = leading + (cmd & 0x1f);
         s32 repeat = (cmd >> 5) + 1;

         if (filled > 0x50 || repeat > rows) {
            return -1;
         }

         rows -= repeat;
      }
   }

   return pos;
}

static s32 bloodValidateAnimationStream(const u8 *stream, s32 size)
{
   s32 offset = 0;
   s32 frames = 0;

   while (offset < size) {
      s32 next = bloodValidateFrame(stream, size, offset);

      if (next <= offset || next > size) {
         return 0;
      }

      frames++;
      offset = next;
   }

   return offset == size ? frames : 0;
}

static s32 bloodAnimationEnsureLoaded(void)
{
   u8 *csegment;
   u8 *compressed;
   u8 *huftbuf;
   uintptr_t cdata_start = get_cdataSegmentRomStart();
   uintptr_t cdata_end = get_cdataSegmentRomEnd();
   uintptr_t csegment_start = get_csegmentSegmentStart();
   uintptr_t cdata_size;
   uintptr_t animation_offset;
   u32 decompressed;
   s32 frames;

   if (g_BloodAnimationLoadState != 0) {
      return g_BloodAnimationLoadState > 0;
   }

   if (cdata_end < cdata_start ||
       BLOOD_ANIMATION_VADDR_START < csegment_start) {
      fprintf(stderr,
              "[BLOOD] invalid cdata/csegment bounds for blood animation "
              "(cdata=0x%lx..0x%lx csegment=0x%lx anim=0x%x)\n",
              (unsigned long)cdata_start,
              (unsigned long)cdata_end,
              (unsigned long)csegment_start,
              BLOOD_ANIMATION_VADDR_START);
      g_BloodAnimationLoadState = -1;
      return FALSE;
   }

   cdata_size = cdata_end - cdata_start;
   animation_offset = BLOOD_ANIMATION_VADDR_START - csegment_start;

   if (g_romSize < cdata_start + cdata_size) {
      fprintf(stderr,
              "[BLOOD] missing ROM data for blood animation cdata load "
              "(rom=%p size=0x%x need=0x%lx)\n",
              (void *)g_romData,
              g_romSize,
              (unsigned long)(cdata_start + cdata_size));
      g_BloodAnimationLoadState = -1;
      return FALSE;
   }

   compressed = (u8 *)malloc(cdata_size);
   csegment = (u8 *)calloc(1, BLOOD_CSEGMENT_DECOMPRESSED_BYTES + 0x1000);
   huftbuf = (u8 *)calloc(1, 0x4200);

   if (compressed == NULL || csegment == NULL || huftbuf == NULL ||
       !platformRomRead((u32)cdata_start, compressed, (u32)cdata_size)) {
      fprintf(stderr, "[BLOOD] allocation failed while loading blood animation\n");
      free(compressed);
      free(csegment);
      free(huftbuf);
      g_BloodAnimationLoadState = -1;
      return FALSE;
   }

   decompressed = decompressdata(compressed,
                                 csegment,
                                 (struct huft *)huftbuf);
   free(compressed);

   if (decompressed < animation_offset + BLOOD_ANIMATION_BYTES) {
      fprintf(stderr,
              "[BLOOD] cdata inflate too small for blood animation "
              "(got=0x%x need=0x%lx)\n",
              decompressed,
              (unsigned long)(animation_offset + BLOOD_ANIMATION_BYTES));
      free(csegment);
      free(huftbuf);
      g_BloodAnimationLoadState = -1;
      return FALSE;
   }

   memcpy(g_BloodAnimationData,
          csegment + animation_offset,
          BLOOD_ANIMATION_BYTES);

   frames = bloodValidateAnimationStream(g_BloodAnimationData,
                                         BLOOD_ANIMATION_BYTES);
   if (frames <= 0) {
      fprintf(stderr,
              "[BLOOD] ROM blood animation stream failed validation "
              "(offset=0x%lx size=0x%x)\n",
              (unsigned long)animation_offset,
              BLOOD_ANIMATION_BYTES);
      free(csegment);
      free(huftbuf);
      g_BloodAnimationLoadState = -1;
      return FALSE;
   }

   g_BloodAnimationFrameCount = frames;
   g_BloodAnimationLoadState = 1;

   if (bloodTraceEnabled()) {
      fprintf(stderr,
              "[BLOOD] loaded ROM-backed blood animation frames=%d "
              "cdata_offset=0x%lx csegment_offset=0x%lx size=0x%x\n",
              g_BloodAnimationFrameCount,
              (unsigned long)cdata_start,
              (unsigned long)animation_offset,
              BLOOD_ANIMATION_BYTES);
   }

   free(csegment);
   free(huftbuf);
   return TRUE;
}

static u8 *bloodAnimationStartPtr(void)
{
   return bloodAnimationEnsureLoaded() ? g_BloodAnimationData : NULL;
}

static u8 *bloodGetOverlayTexturePtr(void)
{
   u8 *src = g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx];

   if (src == NULL) {
      return NULL;
   }

   memcpy(g_BloodOverlayUploadBuffer, src, BLOOD_IMAGE_BYTES);
   return g_BloodOverlayUploadBuffer;
}

static unsigned long bloodTraceOffset(u8 *ptr, u8 *start, u8 *end)
{
   uintptr_t p;
   uintptr_t s;
   uintptr_t e;

   if (ptr == NULL || start == NULL || end == NULL) {
      return 0UL;
   }

   p = (uintptr_t)ptr;
   s = (uintptr_t)start;
   e = (uintptr_t)end;

   return (p >= s && p <= e) ? (unsigned long)(p - s) : 0UL;
}

static void bloodTraceDecodedFrame(s32 arg0, u8 *cur, u8 *next, u8 *end,
                                   u8 *packed, u8 marker, s32 finished)
{
   u8 *start;
   s32 nonzero = 0;
   s32 max_nibble = 0;

   if (!bloodTraceEnabled()) {
      return;
   }

   if (packed != NULL) {
      s32 i;

      for (i = 0; i < BLOOD_IMAGE_BYTES; i++) {
         s32 hi = packed[i] >> 4;
         s32 lo = packed[i] & 0xf;

         if (hi != 0) {
            nonzero++;
         }
         if (lo != 0) {
            nonzero++;
         }
         if (hi > max_nibble) {
            max_nibble = hi;
         }
         if (lo > max_nibble) {
            max_nibble = lo;
         }
      }
   }

   start = bloodAnimationStartPtr();
   fprintf(stderr,
           "[BLOOD-ANIM] frame=%d arg=%d cur=0x%lx next=0x%lx end=0x%lx "
           "finished=%d marker=%u nonzero=%d max=%d total_frames=%d\n",
           g_BloodTraceFrame++,
           arg0,
           bloodTraceOffset(cur, start, end),
           bloodTraceOffset(next, start, end),
           bloodTraceOffset(end, start, end),
           finished,
           marker,
           nonzero,
           max_nibble,
           g_BloodAnimationFrameCount);
}
#else
static u8 *bloodAnimationStartPtr(void)
{
   return die_blood_image_1;
}

static u8 *bloodGetOverlayTexturePtr(void)
{
   return g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx];
}
#endif

static s32 bloodPtrInAnimationRange(u8 *ptr, u8 *start, u8 *end)
{
   uintptr_t p;
   uintptr_t s;
   uintptr_t e;

   if (ptr == NULL || start == NULL || end == NULL) {
      return FALSE;
   }

   p = (uintptr_t)ptr;
   s = (uintptr_t)start;
   e = (uintptr_t)end;

   return p >= s && p < e;
}

static s32 bloodPtrAtOrPastAnimationEnd(u8 *ptr, u8 *end)
{
   if (ptr == NULL || end == NULL) {
      return end == NULL;
   }

   return (uintptr_t)ptr >= (uintptr_t)end;
}

Gfx *insert_imageDL(Gfx *gdl) {
   gDPSetCycleType(gdl++, G_CYC_FILL);
   gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, viGetX(), osVirtualToPhysical(viGetFrameBuf2()));
   gDPSetFillColor(gdl++, ((GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1)));
   gDPFillRectangle(gdl++, 0, 0, (viGetX() - 1), (viGetY() - 1));

   return gdl;
}

Gfx *sub_GAME_7F01C1A4(Gfx *gdl) {
   gSPMatrix(gdl++, osVirtualToPhysical(matrixBufferGunbarrel0), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
   gSPMatrix(gdl++, osVirtualToPhysical(&matrixBufferRareLogo2[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
   gDPPipeSync(gdl++);
   gDPSetCycleType(gdl++, G_CYC_1CYCLE);
   gDPSetRenderMode(gdl++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
   gSPSetGeometryMode(gdl++, (G_SHADE | G_SHADING_SMOOTH));

   return gdl;
}

s32 die_blood_image_routine(s32 arg0) {
   u8 sp37;
   u8* temp_v0_2;
   u8 *start = bloodAnimationStartPtr();
   u8 *end = start != NULL ? start + BLOOD_ANIMATION_BYTES : NULL;
   s32 finished;

   if (start == NULL || end == NULL) {
      g_CurrentPlayer->bloodImgCur = NULL;
      g_CurrentPlayer->bloodImgNxt = NULL;
      return TRUE;
   }

   if (arg0 == 0) {
      g_CurrentPlayer->bloodImgCur = start;
#ifdef NATIVE_PORT
      g_BloodTraceFrame = 0;
#endif
   } else if (arg0 == 1) {
      if (bloodPtrInAnimationRange(g_CurrentPlayer->bloodImgNxt, start, end)) {
         g_CurrentPlayer->bloodImgCur = g_CurrentPlayer->bloodImgNxt;
      } else if (bloodPtrAtOrPastAnimationEnd(g_CurrentPlayer->bloodImgNxt, end)) {
         return TRUE;
      }
   }
   if (!bloodPtrInAnimationRange(g_CurrentPlayer->bloodImgCur, start, end)) {
      g_CurrentPlayer->bloodImgCur = start;
   }

   g_CurrentPlayer->bloodImgIdx = (1 - g_CurrentPlayer->bloodImgIdx);
   g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx] = dynAllocate(BLOOD_IMAGE_BYTES);
   temp_v0_2 = dynAllocate(BLOOD_IMAGE_BYTES);
#ifdef NATIVE_PORT
   if (g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx] == NULL || temp_v0_2 == NULL) {
      /* dyn overflow: skip this blood-frame decode rather than write into an
       * aliased/NULL buffer. Leave the buffer NULL so the overlay draw skips. */
      g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx] = NULL;
      return FALSE;
   }
#endif
   g_CurrentPlayer->bloodImgNxt = decrypt_bleeding_animation_data(g_CurrentPlayer->bloodImgCur, 0x50, 0x60, temp_v0_2, &sp37);
   sub_GAME_7F01D16C(temp_v0_2, 0x50, 0x60, g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx]);
   sub_GAME_7F01D02C(g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx], 0x50, g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx]);
   sub_GAME_7F01CEEC(g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx], 0x50, g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx]);
   sub_GAME_7F01CC94(g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx], BLOOD_IMAGE_BYTES, g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx]);

   finished = bloodPtrAtOrPastAnimationEnd(g_CurrentPlayer->bloodImgNxt, end);
#ifdef NATIVE_PORT
   bloodTraceDecodedFrame(arg0,
                          g_CurrentPlayer->bloodImgCur,
                          g_CurrentPlayer->bloodImgNxt,
                          end,
                          g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx],
                          sp37,
                          finished);
#endif
   return finished;
}

Gfx *gunbarrelBloodOverlayDL(Gfx *gdl) {
   u8 *blood_texture = bloodGetOverlayTexturePtr();

   if (blood_texture == NULL) {
      return gdl;
   }

   gDPSetTextureLUT(gdl++, G_TT_NONE);
   gDPSetTextureFilter(gdl++, G_TF_BILERP); 

   gdl = sub_GAME_7F01C1A4(gdl);

   gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
   gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
   gDPSetCombineMode(gdl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM)
   gDPSetColorDither(gdl++, G_CD_MAGICSQ);
   gDPSetPrimColor(gdl++, 0, 0, 0x96, 0x00, 0x00, 0xB4);
   gDPSetTexturePersp(gdl++, G_TP_NONE);
   gDPLoadTextureBlock_4b(gdl++, OS_K0_TO_PHYSICAL(blood_texture), G_IM_FMT_I, 96, 80, 0, (G_TX_NOMIRROR | G_TX_CLAMP), (G_TX_NOMIRROR | G_TX_CLAMP), G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
   gSPTextureRectangle(gdl++, 0, 0, ((viGetX() * 4) - 1), ((viGetY() * 4) - 1), G_TX_RENDERTILE, 0, 0, 0x18000 / viGetX(), 0x14000 / viGetY());
   
   return gdl;
}

Gfx *gameplayBloodOverlayDL(Gfx *gdl) {
   u8 *blood_texture = bloodGetOverlayTexturePtr();

   if (blood_texture == NULL) {
      return gdl;
   }

   gDPSetTextureLUT(gdl++, G_TT_NONE);
   gDPSetTextureFilter(gdl++, G_TF_BILERP);
   gDPSetCycleType(gdl++, G_CYC_1CYCLE);
   gSPSetGeometryMode(gdl++, (G_SHADE | G_SHADING_SMOOTH));
   gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
   gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
   gDPSetCombineMode(gdl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
   gDPSetColorDither(gdl++, G_CD_MAGICSQ);
   gDPSetPrimColor(gdl++, 0, 0, 0x96, 0x00, 0x00, 0xB4);
   gDPSetTexturePersp(gdl++, G_TP_NONE);
   gDPLoadTextureBlock_4b(gdl++, OS_K0_TO_PHYSICAL(blood_texture), G_IM_FMT_I, 96, 80, 0, (G_TX_NOMIRROR | G_TX_CLAMP), (G_TX_NOMIRROR | G_TX_CLAMP), G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
   gSPTextureRectangle(gdl++, (viGetViewLeft() * 4), (viGetViewTop() * 4), (((viGetViewLeft() + viGetViewWidth()) * 4) - 1), (((viGetViewTop() + viGetViewHeight()) * 4) - 1), G_TX_RENDERTILE, 0, 0, (0x18000 / viGetViewWidth()), (0x14000 / viGetViewHeight()));
   gDPPipeSync(gdl++);
   gDPSetColorDither(gdl++, G_CD_BAYER);
   gDPSetTexturePersp(gdl++, G_TP_PERSP);

   return gdl;
}

Gfx *sub_GAME_7F01CA18(Gfx *gdl) {
	gdl = sub_GAME_7F01C1A4(gdl);

	gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
	gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
	gDPSetPrimColor(gdl++, 0, 0, 150, 0, 0, 180);
	gDPSetColorDither(gdl++, G_CD_MAGICSQ);
	gDPFillRectangle(gdl++, 0, 0, viGetX(), viGetY());

   return gdl;
}
