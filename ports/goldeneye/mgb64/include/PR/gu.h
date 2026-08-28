#ifdef NATIVE_PORT
/* The native port uses src/platform/platform_os.h for GU helpers. */
#else
#ifndef _GU_H_
#define _GU_H_

/*
 * Clean-room declarations for libultra-style graphics utility helpers used by
 * the N64 matching target.
 */

#include <PR/mbi.h>
#include <PR/sptask.h>
#include <PR/ultratypes.h>

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define M_PI 3.14159265358979323846
#define M_DTOR (3.14159265358979323846 / 180.0)

#define FTOFIX32(x) (long)((x) * (float)0x00010000)
#define FIX32TOF(x) ((float)(x) / (float)0x00010000)
#define FTOFRAC8(x) ((int)MIN(((x) * (128.0)), 127.0) & 0xff)

#define FILTER_WRAP 0
#define FILTER_CLAMP 1

#define RAND(x) (guRandom() % (x))

typedef struct Image {
    unsigned char *base;
    int fmt;
    int siz;
    int xsize;
    int ysize;
    int lsize;
    int addr;
    int w;
    int h;
    int s;
    int t;
} Image;

typedef struct PositionalLight {
    float col[3];
    float pos[3];
    float a1;
    float a2;
} PositionalLight;

int guLoadTextureBlockMipMap(Gfx **glist, unsigned char *tbuf, Image *im, unsigned char startTile,
    unsigned char pal, unsigned char cms, unsigned char cmt, unsigned char masks, unsigned char maskt,
    unsigned char shifts, unsigned char shiftt, unsigned char cfs, unsigned char cft);

int guGetDPLoadTextureTileSz(int ult, int lrt);
void guDPLoadTextureTile(Gfx *glistp, void *timg, int texl_fmt, int texl_size, int img_width, int img_height,
    int uls, int ult, int lrs, int lrt, int palette, int cms, int cmt, int masks, int maskt, int shifts,
    int shiftt);

void guMtxIdent(Mtx *m);
void guMtxIdentF(float mf[4][4]);
void guOrtho(Mtx *m, float l, float r, float b, float t, float n, float f, float scale);
void guOrthoF(float mf[4][4], float l, float r, float b, float t, float n, float f, float scale);
void guFrustum(Mtx *m, float l, float r, float b, float t, float n, float f, float scale);
void guFrustumF(float mf[4][4], float l, float r, float b, float t, float n, float f, float scale);
void guPerspective(Mtx *m, u16 *perspNorm, float fovy, float aspect, float near, float far, float scale);
void guPerspectiveF(float mf[4][4], u16 *perspNorm, float fovy, float aspect, float near, float far, float scale);
void guLookAt(Mtx *m, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp,
    float yUp, float zUp);
void guLookAtF(float mf[4][4], float xEye, float yEye, float zEye, float xAt, float yAt, float zAt,
    float xUp, float yUp, float zUp);
void guLookAtReflect(Mtx *m, LookAt *l, float xEye, float yEye, float zEye, float xAt, float yAt,
    float zAt, float xUp, float yUp, float zUp);
void guLookAtReflectF(float mf[4][4], LookAt *l, float xEye, float yEye, float zEye, float xAt,
    float yAt, float zAt, float xUp, float yUp, float zUp);
void guLookAtHilite(Mtx *m, LookAt *l, Hilite *h, float xEye, float yEye, float zEye, float xAt,
    float yAt, float zAt, float xUp, float yUp, float zUp, float xl1, float yl1, float zl1, float xl2,
    float yl2, float zl2, int twidth, int theight);
void guLookAtHiliteF(float mf[4][4], LookAt *l, Hilite *h, float xEye, float yEye, float zEye,
    float xAt, float yAt, float zAt, float xUp, float yUp, float zUp, float xl1, float yl1, float zl1,
    float xl2, float yl2, float zl2, int twidth, int theight);
void guLookAtStereo(Mtx *m, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt,
    float xUp, float yUp, float zUp, float eyedist);
void guLookAtStereoF(float mf[4][4], float xEye, float yEye, float zEye, float xAt, float yAt,
    float zAt, float xUp, float yUp, float zUp, float eyedist);
void guRotate(Mtx *m, float a, float x, float y, float z);
void guRotateF(float mf[4][4], float a, float x, float y, float z);
void guRotateRPY(Mtx *m, float r, float p, float y);
void guRotateRPYF(float mf[4][4], float r, float p, float h);
void guAlign(Mtx *m, float a, float x, float y, float z);
void guAlignF(float mf[4][4], float a, float x, float y, float z);
void guScale(Mtx *m, float x, float y, float z);
void guScaleF(float mf[4][4], float x, float y, float z);
void guTranslate(Mtx *m, float x, float y, float z);
void guTranslateF(float mf[4][4], float x, float y, float z);
void guPosition(Mtx *m, float r, float p, float h, float s, float x, float y, float z);
void guPositionF(float mf[4][4], float r, float p, float h, float s, float x, float y, float z);
void guMtxF2L(float mf[4][4], Mtx *m);
void guMtxL2F(float mf[4][4], Mtx *m);
void guMtxCatF(float m[4][4], float n[4][4], float r[4][4]);
void guMtxCatL(Mtx *m, Mtx *n, Mtx *res);
void guMtxXFMF(float mf[4][4], float x, float y, float z, float *ox, float *oy, float *oz);
void guMtxXFML(Mtx *m, float x, float y, float z, float *ox, float *oy, float *oz);

void guNormalize(float *x, float *y, float *z);

void guPosLight(PositionalLight *pl, Light *l, float xOb, float yOb, float zOb);
void guPosLightHilite(PositionalLight *pl1, PositionalLight *pl2, Light *l1, Light *l2, LookAt *l,
    Hilite *h, float xEye, float yEye, float zEye, float xOb, float yOb, float zOb, float xUp,
    float yUp, float zUp, int twidth, int theight);
int guRandom(void);

float sinf(float angle);
float cosf(float angle);
signed short sins(unsigned short angle);
signed short coss(unsigned short angle);
float sqrtf(float value);

#define GU_PARSERDP_VERBOSE 1
#define GU_PARSERDP_PRAREA 2
#define GU_PARSERDP_PRHISTO 4
#define GU_PARSERDP_DUMPONLY 32

void guParseRdpDL(u64 *rdp_dl, u64 nbytes, u8 flags);
void guParseString(char *StringPointer, u64 nbytes);

#define GU_BLINKRDP_HILITE 1
#define GU_BLINKRDP_EXTRACT 2

void guBlinkRdpDL(u64 *rdp_dl_in, u64 nbytes_in, u64 *rdp_dl_out, u64 *nbytes_out, u32 x, u32 y,
    u32 radius, u8 red, u8 green, u8 blue, u8 flags);

#define GU_PARSEGBI_ROWMAJOR 1
#define GU_PARSEGBI_NONEST 2
#define GU_PARSEGBI_FLTMTX 4
#define GU_PARSEGBI_SHOWDMA 8
#define GU_PARSEGBI_ALLMTX 16
#define GU_PARSEGBI_DUMPONLY 32

void guParseGbiDL(u64 *gbi_dl, u32 nbytes, u8 flags);
void guDumpGbiDL(OSTask *tp, u8 flags);

#define GU_PARSE_GBI_TYPE 1
#define GU_PARSE_RDP_TYPE 2
#define GU_PARSE_READY 3
#define GU_PARSE_MEM_BLOCK 4
#define GU_PARSE_ABI_TYPE 5
#define GU_PARSE_STRING_TYPE 6

typedef struct guDLPrintCB {
    int dataSize;
    int dlType;
    int flags;
    u32 paddr;
} guDLPrintCB;

void guSprite2DInit(uSprite *SpritePointer, void *SourceImagePointer, void *TlutPointer, int Stride,
    int SubImageWidth, int SubImageHeight, int SourceImageType, int SourceImageBitSize,
    int SourceImageOffsetS, int SourceImageOffsetT);

#endif
#endif
