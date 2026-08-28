#include "titan_ui_layout_guard.h"
#include "titan_animation.h"
#include <string.h>

#define TITAN_UI_TEXT_CACHE_CAPACITY 64u
#define TITAN_UI_TEXT_HEIGHT_FACTOR 20.0f

typedef struct TitanUiTextCacheEntry { uint32_t hash; uint32_t scale_bits; float width; uint8_t valid; } TitanUiTextCacheEntry;
static TitanUiTextCacheEntry g_text_cache[TITAN_UI_TEXT_CACHE_CAPACITY];
static unsigned int g_text_cache_cursor;

static uint32_t HashText(const char *s){uint32_t h=2166136261u; if(!s)return h; while(*s){h^=(unsigned char)*s++;h*=16777619u;}return h;}
static uint32_t FloatBits(float v){uint32_t u; memcpy(&u,&v,sizeof(u));return u;}

float TitanUiFocus_Step(float current, int target, float rate){return TitanAnimation_Approach(current,(float)target,rate);}
unsigned int TitanUiFocus_UniqueIndex(int target, unsigned int count){if(count==0u)return 0u; if(target<0)return 0u; return (unsigned int)target<count?(unsigned int)target:count-1u;}
int TitanUiRect_Overlaps(const TitanUiRect *a,const TitanUiRect *b){if(!a||!b)return 0;return a->x < b->x+b->width && a->x+a->width > b->x && a->y < b->y+b->height && a->y+a->height > b->y;}
TitanUiRect TitanUiLayout_PlaceAnchored(float cw,float ch,float w,float h,float m,TitanUiAnchor a){TitanUiRect r={m,m,w,h};if(a==TITAN_UI_ANCHOR_TOP_RIGHT||a==TITAN_UI_ANCHOR_BOTTOM_RIGHT)r.x=cw-m-w;if(a==TITAN_UI_ANCHOR_BOTTOM_LEFT||a==TITAN_UI_ANCHOR_BOTTOM_RIGHT)r.y=ch-m-h;return r;}
TitanUiAnchor TitanUiLayout_SelectSafeAnchor(float cw,float ch,float w,float h,float m,const TitanUiRect *e,unsigned int n){static const TitanUiAnchor order[]={TITAN_UI_ANCHOR_BOTTOM_RIGHT,TITAN_UI_ANCHOR_TOP_RIGHT,TITAN_UI_ANCHOR_BOTTOM_LEFT,TITAN_UI_ANCHOR_TOP_LEFT};unsigned int i,j,best=0,best_hits=~0u;for(i=0;i<4;i++){unsigned int hits=0;TitanUiRect r=TitanUiLayout_PlaceAnchored(cw,ch,w,h,m,order[i]);for(j=0;j<n;j++)hits+=(unsigned int)TitanUiRect_Overlaps(&r,&e[j]);if(hits<best_hits){best_hits=hits;best=i;if(hits==0u)break;}}return order[best];}
float TitanUiText_MeasureCached(float scale,const char *text,float (*measure)(float,const char *)){uint32_t h=HashText(text),sb=FloatBits(scale);unsigned int i;if(!measure)return 0.0f;for(i=0;i<TITAN_UI_TEXT_CACHE_CAPACITY;i++)if(g_text_cache[i].valid&&g_text_cache[i].hash==h&&g_text_cache[i].scale_bits==sb)return g_text_cache[i].width;i=g_text_cache_cursor++%TITAN_UI_TEXT_CACHE_CAPACITY;g_text_cache[i].hash=h;g_text_cache[i].scale_bits=sb;g_text_cache[i].width=measure(scale,text?text:"");g_text_cache[i].valid=1u;return g_text_cache[i].width;}
void TitanUiText_ResetMetricsCache(void){memset(g_text_cache,0,sizeof(g_text_cache));g_text_cache_cursor=0u;}
