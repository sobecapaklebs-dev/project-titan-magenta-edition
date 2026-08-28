#include "titan_engine_overlay.h"

#ifndef TITAN_ENGINE_OVERLAY_HOST
#define TITAN_GIF_STAT   (*(volatile u32 *)0x10003020)
#define TITAN_D2_CHCR    (*(volatile u32 *)0x1000A000)
#define TITAN_GIF_FIFO   ((volatile u128 *)0x10006000)
#define TITAN_GS_PMODE   (*(volatile u64 *)0x12000000)
#define TITAN_GS_DISPFB1 (*(volatile u64 *)0x12000070)
#define TITAN_GS_DISPLAY1 (*(volatile u64 *)0x12000080)
#define TITAN_GS_DISPFB2 (*(volatile u64 *)0x12000090)
#define TITAN_GS_DISPLAY2 (*(volatile u64 *)0x120000A0)

#define GS_REG_PRIM       0x00u
#define GS_REG_RGBAQ      0x01u
#define GS_REG_XYZ2       0x05u
#define GS_REG_XYOFFSET_2 0x19u
#define GS_REG_SCISSOR_2  0x41u
#define GS_REG_ALPHA_2    0x43u
#define GS_REG_TEST_2     0x48u
#define GS_REG_FRAME_2    0x4Du
#define GS_GIF_AD         0x0Eu

static u64 gs_giftag(unsigned int nloop)
{
    return (u64)nloop | ((u64)1 << 15) | ((u64)1 << 60);
}
static u64 gs_frame(u32 fbp, u32 fbw, u32 psm) { return (u64)fbp | ((u64)fbw << 16) | ((u64)psm << 24); }
static u64 gs_xyoffset(u32 x, u32 y) { return (u64)x | ((u64)y << 32); }
static u64 gs_scissor(u32 x0,u32 x1,u32 y0,u32 y1) { return (u64)x0 | ((u64)x1<<16) | ((u64)y0<<32) | ((u64)y1<<48); }
static u64 gs_rgba(u8 r,u8 g,u8 b,u8 a) { return (u64)r | ((u64)g<<8) | ((u64)b<<16) | ((u64)a<<24) | ((u64)0x3f800000u<<32); }
static u64 gs_xyz(u32 x,u32 y) { return (u64)x | ((u64)y<<16); }

static int gif_idle(void)
{
    unsigned int n;
    for (n=0;n<200000u;n++) {
        u32 s=TITAN_GIF_STAT;
        if ((TITAN_D2_CHCR & 0x100u)==0u && (s & ((1u<<9)|(3u<<10)|(1u<<6)))==0u) return 1;
    }
    return 0;
}
static void gif_qword(u64 lo,u64 hi)
{
    union { u128 q; u64 d[2]; } value;
    value.d[0]=lo; value.d[1]=hi;
    *TITAN_GIF_FIFO=value.q;
}
static void gif_ad(u64 data,u64 reg) { gif_qword(data,reg); }
static int begin_packet(unsigned int pairs)
{
    if (!gif_idle()) return 0;
    gif_qword(gs_giftag(pairs),GS_GIF_AD);
    return 1;
}
static void sprite(u32 x0,u32 y0,u32 x1,u32 y1,u8 r,u8 g,u8 b,u8 a)
{
    const u64 prim=6u | ((u64)1<<6) | ((u64)1<<9);
    if (!begin_packet(4)) return;
    gif_ad(prim,GS_REG_PRIM);
    gif_ad(gs_rgba(r,g,b,a),GS_REG_RGBAQ);
    gif_ad(gs_xyz((2048u+x0)<<4,(2048u+y0)<<4),GS_REG_XYZ2);
    gif_ad(gs_xyz((2048u+x1)<<4,(2048u+y1)<<4),GS_REG_XYZ2);
}

/* 3x5 uppercase/digit glyph rows, low three bits used. */
static const u8 glyphs[37][5] = {
 {7,5,5,5,7}, /* 0 */ {2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
 {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
 {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},
 {3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
 {5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,7,3},{6,5,6,5,5},
 {3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
 {5,5,2,2,2},{7,1,2,4,7},{0,0,0,0,0}
};
static const u8 *glyph(char c)
{
    if (c>='0'&&c<='9') return glyphs[c-'0'];
    if (c>='a'&&c<='z') c=(char)(c-'a'+'A');
    if (c>='A'&&c<='Z') return glyphs[10+c-'A'];
    return glyphs[36];
}
static void text(const char *s,u32 x,u32 y,u8 r,u8 g,u8 b)
{
    while (*s) {
        const u8 *gph=glyph(*s++); unsigned row;
        for (row=0;row<5;row++) { unsigned col=0; while(col<3) {
            if (!(gph[row]&(1u<<(2-col)))) { col++; continue; }
            unsigned start=col; while(col<3 && (gph[row]&(1u<<(2-col)))) col++;
            sprite(x+start*3,y+row*3,x+col*3,y+row*3+3,r,g,b,0x80);
        }}
        x+=12;
    }
}
static int setup_context2(u32 *width,u32 *height)
{
    u64 pm=TITAN_GS_PMODE;
    u64 dfb=(pm&1u)?TITAN_GS_DISPFB1:TITAN_GS_DISPFB2;
    u64 dsp=(pm&1u)?TITAN_GS_DISPLAY1:TITAN_GS_DISPLAY2;
    u32 fbp=(u32)(dfb&0x1ffu), fbw=(u32)((dfb>>9)&0x3fu), psm=(u32)((dfb>>15)&0x1fu);
    u32 magh=(u32)((dsp>>23)&0x0fu)+1u, magv=(u32)((dsp>>27)&0x03u)+1u;
    u32 w=((u32)((dsp>>32)&0xfffu)+1u)/magh, h=((u32)((dsp>>44)&0x7ffu)+1u)/magv;
    if (!fbw || !w || !h || (psm!=0u && psm!=1u && psm!=2u && psm!=10u)) return 0;
    if (w>fbw*64u) w=fbw*64u;
    if (!begin_packet(5)) return 0;
    gif_ad(gs_frame(fbp,fbw,psm),GS_REG_FRAME_2);
    gif_ad(gs_xyoffset(2048u<<4,2048u<<4),GS_REG_XYOFFSET_2);
    gif_ad(gs_scissor(0,w-1,0,h-1),GS_REG_SCISSOR_2);
    gif_ad(0u,GS_REG_TEST_2); /* alpha/Z tests disabled for overlay sprites */
    gif_ad((u64)1u<<2 | (u64)1u<<6,GS_REG_ALPHA_2); /* Cs*As + Cd*(1-As) */
    *width=w; *height=h; return 1;
}
#endif

void TitanEngineOverlay_Init(TitanEngineOverlay *o,u32 caps,const char *title)
{
    unsigned int i;
    if (!o) return;
    for (i=0;i<(unsigned int)sizeof(*o);++i) ((u8 *)o)[i]=0;
    o->capabilities=caps;
    if (title) {
        for (i=0;i+1u<(unsigned int)sizeof(o->title_id) && title[i];++i) o->title_id[i]=title[i];
        o->title_id[i]='\0';
    }
}
int TitanEngineOverlay_Open(TitanEngineOverlay *o)
{
    if (!o || o->open || !(o->capabilities&TITAN_ENGINE_OVERLAY_CAP_RESUME) || !(o->capabilities&TITAN_ENGINE_OVERLAY_CAP_CONTEXT2_SAFE)) return 0;
    o->open=1; o->selection=0; o->previous_buttons=0; return 1;
}
TitanEngineOverlayAction TitanEngineOverlay_Update(TitanEngineOverlay *o,u32 buttons)
{
    u32 pressed;
    if (!o || !o->open) return TITAN_ENGINE_OVERLAY_NONE;
    pressed=buttons & ~o->previous_buttons; o->previous_buttons=buttons;
    if (pressed&TITAN_ENGINE_BTN_UP) o->selection=0;
    if (pressed&TITAN_ENGINE_BTN_DOWN) o->selection=(o->capabilities&TITAN_ENGINE_OVERLAY_CAP_RETURN)?1u:0u;
    if (pressed&TITAN_ENGINE_BTN_CIRCLE) { TitanEngineOverlay_Close(o); return TITAN_ENGINE_OVERLAY_RESUME; }
    if (pressed&TITAN_ENGINE_BTN_CROSS) {
        if (o->selection==0u) { TitanEngineOverlay_Close(o); return TITAN_ENGINE_OVERLAY_RESUME; }
        if (o->capabilities&TITAN_ENGINE_OVERLAY_CAP_RETURN) { o->open=0; return TITAN_ENGINE_OVERLAY_RETURN; }
    }
    return TITAN_ENGINE_OVERLAY_NONE;
}
void TitanEngineOverlay_Close(TitanEngineOverlay *o) { if(o){o->open=0;o->previous_buttons=0;} }
int TitanEngineOverlay_Render(TitanEngineOverlay *o)
{
#ifdef TITAN_ENGINE_OVERLAY_HOST
    if (!o || !o->open) return 0;
    o->render_count++;
    return 1;
#else
    u32 w,h,panel_w,panel_h,x,y;
    if (!o || !o->open || !(o->capabilities&TITAN_ENGINE_OVERLAY_CAP_CONTEXT2_SAFE)) return 0;
    if (!setup_context2(&w,&h)) return 0;
    panel_w=w>560u?520u:w-32u; panel_h=h>220u?180u:h-24u; x=(w-panel_w)/2u; y=(h-panel_h)/2u;
    sprite(x,y,x+panel_w,y+panel_h,8,8,14,0x70);
    sprite(x+12,y+12,x+panel_w-12,y+14,190,0,80,0x80);
    text("PROJECT TITAN",x+28,y+28,255,255,255);
    text(o->title_id,x+28,y+52,180,180,190);
    if(o->selection==0) sprite(x+18,y+82,x+panel_w-18,y+108,120,0,50,0x70);
    text("RESUME",x+30,y+88,255,255,255);
    if(o->selection==1) sprite(x+18,y+118,x+panel_w-18,y+144,120,0,50,0x70);
    text("RETURN TO TITAN",x+30,y+124,255,255,255);
    o->render_count++; return 1;
#endif
}
