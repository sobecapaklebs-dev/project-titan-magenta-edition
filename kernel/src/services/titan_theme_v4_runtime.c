#include "titan_theme_v4_runtime.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clamp_i(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
static float clamp_f(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
static int starts(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static TitanThemeV4Degradation g_theme_v4_degradation = TITAN_THEME_V4_DEGRADE_NONE;


static void sort_layers(TitanThemeV4Layer *layers, uint32_t count)
{
    uint32_t i;
    for (i = 1u; i < count; i++)
    {
        TitanThemeV4Layer key = layers[i];
        uint32_t j = i;
        while (j > 0u &&
              (layers[j - 1u].z > key.z ||
              (layers[j - 1u].z == key.z &&
               layers[j - 1u].declaration_order > key.declaration_order)))
        {
            layers[j] = layers[j - 1u];
            j--;
        }
        layers[j] = key;
    }
}

static void copy_text(char *dst,unsigned int cap,const char *src){
    unsigned int n=0u;if(!dst||!cap)return;while(src&&src[n]&&n+1u<cap){dst[n]=src[n];n++;}dst[n]='\0';
}

void TitanThemeV4_Init(TitanThemeV4Runtime *r){
    if (!r) return;
    memset(r,0,sizeof(*r));
    r->vram_budget=512u*1024u;
    r->ee_ram_budget=2u*1024u*1024u;
    r->reserved_headroom=256u*1024u;
}

static int parse_u32(const char *v,uint32_t *out){char *e=0;unsigned long n;if(!v||!out)return 0;n=strtoul(v,&e,10);if(!e||*e!='\0'||n>0xfffffffful)return 0;*out=(uint32_t)n;return 1;}
static int parse_i32(const char *v,int32_t *out){char *e=0;long n;if(!v||!out)return 0;n=strtol(v,&e,10);if(!e||*e!='\0'||n<-2147483647l-1l||n>2147483647l)return 0;*out=(int32_t)n;return 1;}
static int layer_key(const char *key,uint32_t *idx,const char **field){char *e=0;unsigned long n;if(!starts(key,"layer"))return 0;n=strtoul(key+5,&e,10);if(e==key+5||!e||*e!='_'||n>=TITAN_THEME_V4_MAX_LAYERS)return 0;*idx=(uint32_t)n;*field=e+1;return 1;}

int TitanThemeV4_ParseManifest(TitanThemeV4Runtime *r,const char *text){
    char line[512];const char *p=text;uint32_t declared=0u,seen=0u,total=0u;
    if (!r || !text) return 0;
    TitanThemeV4_Init(r);
    while(*p){size_t n=0;const char *eq;char *key,*val;while(p[n]&&p[n]!='\n'&&n+1u<sizeof(line)){line[n]=p[n];n++;}line[n]='\0';p+=n;if(*p=='\n')p++;if(!n||line[0]=='#')continue;eq=strchr(line,'=');if(!eq)return 0;line[eq-line]='\0';key=line;val=line+(eq-line)+1;
        if(strcmp(key,"format")==0){if(strcmp(val,"titan-scene-v4")!=0)return 0;seen|=1u;}
        else if(strcmp(key,"version")==0){if(strcmp(val,"1")!=0)return 0;seen|=2u;}
        else if(strcmp(key,"layer_count")==0){if(!parse_u32(val,&declared)||declared<1u||declared>TITAN_THEME_V4_MAX_LAYERS)return 0;seen|=4u;}
        else if(strcmp(key,"vram_budget")==0){if(!parse_u32(val,&r->vram_budget))return 0;}
        else if(strcmp(key,"ee_ram_budget")==0){if(!parse_u32(val,&r->ee_ram_budget))return 0;}
        else if(strcmp(key,"reserved_headroom")==0){if(!parse_u32(val,&r->reserved_headroom))return 0;}
        else if(strcmp(key,"event_count")==0){if(!parse_u32(val,&r->event_count)||r->event_count>TITAN_THEME_V4_MAX_EVENTS)return 0;}
        else if(starts(key,"event")) {
            char *e=0; unsigned long ei=strtoul(key+5,&e,10); uint32_t u;
            if(e==key+5||!e||*e!='_'||ei>=TITAN_THEME_V4_MAX_EVENTS)return 0;
            if(strcmp(e+1,"layer")==0){if(!parse_u32(val,&u)||u>=TITAN_THEME_V4_MAX_LAYERS)return 0;r->events[ei].layer_declaration_order=(uint8_t)u;}
            else if(strcmp(e+1,"interval_ms")==0){if(!parse_u32(val,&r->events[ei].interval_ms))return 0;}
            else if(strcmp(e+1,"duration_ms")==0){if(!parse_u32(val,&r->events[ei].duration_ms))return 0;}
            else if(strcmp(e+1,"phase_ms")==0){if(!parse_u32(val,&r->events[ei].phase_ms))return 0;}
        }
        else {uint32_t i;const char *f;TitanThemeV4Layer *l;uint32_t u;int32_t si;if(!layer_key(key,&i,&f))continue;l=&r->layers[i];l->declaration_order=i;
            if(strcmp(f,"asset")==0)copy_text(l->asset,sizeof(l->asset),val);
            else if(strcmp(f,"width")==0){if(!parse_u32(val,&u)||u<1u||u>256u)return 0;l->width=(uint16_t)u;}
            else if(strcmp(f,"height")==0){if(!parse_u32(val,&u)||u<1u||u>128u)return 0;l->height=(uint16_t)u;}
            else if(strcmp(f,"z")==0){if(!parse_i32(val,&si)||si<-32768||si>32767)return 0;l->z=(int16_t)si;}
            else if(strcmp(f,"x_milli")==0){if(!parse_i32(val,&l->x_milli))return 0;}
            else if(strcmp(f,"y_milli")==0){if(!parse_i32(val,&l->y_milli))return 0;}
            else if(strcmp(f,"scale_x_milli")==0){if(!parse_i32(val,&l->scale_x_milli))return 0;}
            else if(strcmp(f,"scale_y_milli")==0){if(!parse_i32(val,&l->scale_y_milli))return 0;}
            else if(strcmp(f,"rotation_mdeg")==0){if(!parse_i32(val,&l->rotation_mdeg))return 0;}
            else if(strcmp(f,"parallax_x_milli")==0){if(!parse_i32(val,&l->parallax_x_milli))return 0;}
            else if(strcmp(f,"parallax_y_milli")==0){if(!parse_i32(val,&l->parallax_y_milli))return 0;}
            else if(strcmp(f,"priority")==0){if(!parse_u32(val,&u)||u>2u)return 0;l->priority=(uint8_t)u;}
            else if(strcmp(f,"context")==0){if(!parse_u32(val,&u)||u<1u||u>3u)return 0;l->context=(uint8_t)u;}
            else if(strcmp(f,"mode")==0){if(!parse_u32(val,&u)||u>3u)return 0;l->mode=(uint8_t)u;}
            else if(strcmp(f,"phase_ms")==0){if(!parse_u32(val,&l->phase_ms))return 0;}
            else if(strcmp(f,"duration_ms")==0){if(!parse_u32(val,&l->duration_ms))return 0;}
            else if(strcmp(f,"anim_x_milli")==0){if(!parse_i32(val,&l->anim_x_milli))return 0;}
            else if(strcmp(f,"anim_y_milli")==0){if(!parse_i32(val,&l->anim_y_milli))return 0;}
            else if(strcmp(f,"anim_scale_x_milli")==0){if(!parse_i32(val,&l->anim_scale_x_milli))return 0;}
            else if(strcmp(f,"anim_scale_y_milli")==0){if(!parse_i32(val,&l->anim_scale_y_milli))return 0;}
            else if(strcmp(f,"anim_rotation_mdeg")==0){if(!parse_i32(val,&l->anim_rotation_mdeg))return 0;}
            else if(strcmp(f,"anim_alpha")==0){if(!parse_i32(val,&si)||si<-255||si>255)return 0;l->anim_alpha=(int16_t)si;}
            else if(strcmp(f,"top_x_milli")==0){if(!parse_i32(val,&l->top_x_milli))return 0;}
            else if(strcmp(f,"top_y_milli")==0){if(!parse_i32(val,&l->top_y_milli))return 0;}
            else if(strcmp(f,"top_scale_x_milli")==0){if(!parse_i32(val,&l->top_scale_x_milli))return 0;}
            else if(strcmp(f,"top_scale_y_milli")==0){if(!parse_i32(val,&l->top_scale_y_milli))return 0;}
            else if(strcmp(f,"top_rotation_mdeg")==0){if(!parse_i32(val,&l->top_rotation_mdeg))return 0;}
            else if(strcmp(f,"top_alpha_delta")==0){if(!parse_i32(val,&si)||si<-255||si>255)return 0;l->top_alpha_delta=(int16_t)si;}
            else if(strcmp(f,"alpha")==0){if(!parse_u32(val,&u)||u>255u)return 0;l->alpha=(uint8_t)u;}
            else if(strcmp(f,"bytes")==0){if(!parse_u32(val,&l->declared_bytes))return 0;}
        }
    }
    if ((seen&7u)!=7u) return 0;
    r->layer_count=declared;
    for(uint32_t i=0;i<r->layer_count;i++){TitanThemeV4Layer *l=&r->layers[i];if(!l->asset[0]||!l->width||!l->height||!l->context||l->declared_bytes!=(uint32_t)l->width*l->height*2u)return 0;if(!l->scale_x_milli)l->scale_x_milli=1000;if(!l->scale_y_milli)l->scale_y_milli=1000;if(l->mode!=TITAN_THEME_V4_STATIC&&l->duration_ms<40u)l->duration_ms=1000u;total+=l->declared_bytes;}
    for (uint32_t e=0u;e<r->event_count;e++) {
        if (r->events[e].layer_declaration_order >= declared ||
            r->events[e].interval_ms < 1000u || r->events[e].interval_ms > 600000u ||
            r->events[e].duration_ms < 40u || r->events[e].duration_ms >= r->events[e].interval_ms)
            return 0;
    }
    if(total>r->ee_ram_budget||r->vram_budget<64u*1024u||r->vram_budget+r->reserved_headroom>2u*1024u*1024u) return 0;
    sort_layers(r->layers, r->layer_count);
    r->telemetry.declared_ee_ram=total;
    r->telemetry.vram_budget=r->vram_budget;
    r->telemetry.ee_ram_budget=r->ee_ram_budget;
    r->telemetry.reserved_headroom=r->reserved_headroom;
    return 1;
}

static float triangle_phase(uint32_t t,uint32_t duration,uint8_t mode){float p;if(duration<1u)return 0.0f;p=(float)(t%duration)/(float)duration;if(mode==TITAN_THEME_V4_PINGPONG)return p<0.5f?p*2.0f:(1.0f-p)*2.0f;if(mode==TITAN_THEME_V4_ONCE){p=(float)t/(float)duration;return clamp_f(p,0.0f,1.0f);}return p;}
static int suppressed(const TitanThemeV4Runtime*r,const TitanThemeV4Layer*l){if(r->degradation>=TITAN_THEME_V4_DEGRADE_DECORATIVE&&l->priority==TITAN_THEME_V4_DECORATIVE)return 1;if(r->degradation>=TITAN_THEME_V4_DEGRADE_ENHANCED&&l->priority==TITAN_THEME_V4_ENHANCED)return 1;if(r->degradation>=TITAN_THEME_V4_DEGRADE_SAFE&&l->priority!=TITAN_THEME_V4_ESSENTIAL)return 1;return 0;}

static int event_allows_layer(const TitanThemeV4Runtime *r, const TitanThemeV4Layer *layer, uint32_t elapsed, uint32_t *active_count)
{
    uint32_t e;
    for (e=0u;e<r->event_count;e++) {
        const TitanThemeV4RareEvent *event=&r->events[e];
        if (event->layer_declaration_order == layer->declaration_order) {
            uint32_t tick=(elapsed + event->phase_ms) % event->interval_ms;
            if (tick < event->duration_ms) { if(active_count)(*active_count)++; return 1; }
            return 0;
        }
    }
    return 1;
}

void TitanThemeV4_Update(TitanThemeV4Runtime*r,const TitanThemeV4FrameInput*in){
    if (!r || !in) return;
    r->elapsed_ms+=in->delta_ms;
    r->degradation=in->pressure_percent>=92u?TITAN_THEME_V4_DEGRADE_SAFE:(in->pressure_percent>=82u?TITAN_THEME_V4_DEGRADE_ENHANCED:(in->pressure_percent>=68u?TITAN_THEME_V4_DEGRADE_DECORATIVE:TITAN_THEME_V4_DEGRADE_NONE));memset(&r->telemetry.active_layers,0,sizeof(r->telemetry)-offsetof(TitanThemeV4Telemetry,active_layers));r->telemetry.declared_ee_ram=0u;r->telemetry.vram_budget=r->vram_budget;r->telemetry.ee_ram_budget=r->ee_ram_budget;r->telemetry.reserved_headroom=r->reserved_headroom;r->telemetry.degradation_state=(uint32_t)r->degradation;
    for(uint32_t i=0;i<r->layer_count;i++){TitanThemeV4Layer*l=&r->layers[i];TitanThemeV4ResolvedLayer*o=&r->resolved[i];float phase=0.0f,px,py,sx,sy,rot;int a;memset(o,0,sizeof(*o));o->source=l;r->telemetry.active_layers++;r->telemetry.declared_ee_ram+=l->declared_bytes;if((in->context!=TITAN_THEME_V4_CONTEXT_BOTH && !(l->context&in->context))||suppressed(r,l)){r->telemetry.skipped_invisible++;r->telemetry.skipped_layers++;continue;}if(!event_allows_layer(r,l,r->elapsed_ms,&r->telemetry.rare_events_active)){r->telemetry.skipped_invisible++;r->telemetry.skipped_layers++;continue;}if(l->mode!=TITAN_THEME_V4_STATIC){phase=triangle_phase(r->elapsed_ms+l->phase_ms,l->duration_ms,l->mode);r->telemetry.animated_layers++;r->telemetry.transform_updates++;}else{r->telemetry.static_layers++;r->telemetry.transform_skips++;o->static_fast_path=1u;}
        px=(float)l->parallax_x_milli/1000.0f*(in->navigation_x*0.55f+in->top_transition*0.30f+in->idle_drift*0.15f);py=(float)l->parallax_y_milli/1000.0f*(in->navigation_y*0.55f+in->top_transition*0.30f+in->idle_drift*0.15f);
        o->x=(float)l->x_milli/1000.0f+px+((float)l->anim_x_milli/1000.0f)*phase+((float)l->top_x_milli/1000.0f)*in->top_transition;
        o->y=(float)l->y_milli/1000.0f+py+((float)l->anim_y_milli/1000.0f)*phase+((float)l->top_y_milli/1000.0f)*in->top_transition;
        sx=(float)(l->scale_x_milli+l->anim_scale_x_milli*phase+l->top_scale_x_milli*in->top_transition)/1000.0f;
        sy=(float)(l->scale_y_milli+l->anim_scale_y_milli*phase+l->top_scale_y_milli*in->top_transition)/1000.0f;
        o->scale_x=clamp_f(sx,0.05f,8.0f);o->scale_y=clamp_f(sy,0.05f,8.0f);
        rot=(float)(l->rotation_mdeg+l->anim_rotation_mdeg*phase+l->top_rotation_mdeg*in->top_transition)/1000.0f;o->rotation_degrees=rot;
        a=(int)l->alpha+(int)((float)l->anim_alpha*phase)+(int)((float)l->top_alpha_delta*in->top_transition);
        if(l->context==TITAN_THEME_V4_CONTEXT_HOME)a=(int)((float)a*(1.0f-in->top_transition));
        else if(l->context==TITAN_THEME_V4_CONTEXT_TOP)a=(int)((float)a*in->top_transition);
        o->alpha=(unsigned char)clamp_i(a,0,255);
        {float w=(float)l->width*o->scale_x,h=(float)l->height*o->scale_y;if(o->x+w<0.0f||o->y+h<0.0f||o->x>=in->viewport_width||o->y>=in->viewport_height){r->telemetry.skipped_offscreen++;r->telemetry.skipped_layers++;continue;}}
        if(!o->alpha){r->telemetry.skipped_invisible++;r->telemetry.skipped_layers++;continue;}o->visible=1u;r->telemetry.visible_layers++;r->telemetry.draw_count++;
    }
}

const TitanThemeV4ResolvedLayer *TitanThemeV4_GetResolved(const TitanThemeV4Runtime*r,uint32_t i){return r&&i<r->layer_count?&r->resolved[i]:0;}
void TitanThemeV4_GetTelemetry(const TitanThemeV4Runtime*r,TitanThemeV4Telemetry*out){if(r&&out)*out=r->telemetry;}


void TitanThemeV4_SetDegradation(TitanThemeV4Degradation degradation)
{
    if (degradation < TITAN_THEME_V4_DEGRADE_NONE) degradation = TITAN_THEME_V4_DEGRADE_NONE;
    if (degradation > TITAN_THEME_V4_DEGRADE_SAFE) degradation = TITAN_THEME_V4_DEGRADE_SAFE;
    g_theme_v4_degradation = degradation;
}

int TitanThemeV4_ParseCompiled(const char *text, TitanThemeV4Scene *scene)
{
    TitanThemeV4Runtime runtime;
    uint32_t i;
    if (!scene || !TitanThemeV4_ParseManifest(&runtime, text)) return 0;
    memset(scene, 0, sizeof(*scene));
    scene->layer_count = runtime.layer_count;
    scene->vram_budget = runtime.vram_budget;
    scene->ee_ram_budget = runtime.ee_ram_budget;
    scene->reserved_headroom = runtime.reserved_headroom;
    scene->event_count = runtime.event_count;
    for (i = 0u; i < runtime.event_count; i++) scene->events[i] = runtime.events[i];
    for (i = 0u; i < runtime.layer_count; i++)
    {
        const TitanThemeV4Layer *source = &runtime.layers[i];
        TitanThemeV4SceneLayer *dest = &scene->layers[i];
        dest->compiled = *source;
        dest->x = (float)source->x_milli / 1000.0f;
        dest->y = (float)source->y_milli / 1000.0f;
        dest->scale_x = (float)source->scale_x_milli / 1000.0f;
        dest->scale_y = (float)source->scale_y_milli / 1000.0f;
        dest->rotation = (float)source->rotation_mdeg / 1000.0f;
        dest->declaration_order = source->declaration_order;
        dest->z = source->z;
        scene->declared_bytes += source->declared_bytes;
    }
    return 1;
}

int TitanThemeV4_EvaluateLayer(
    const TitanThemeV4SceneLayer *layer,
    const TitanThemeV4FrameInput *input,
    TitanThemeV4EvaluatedLayer *out)
{
    TitanThemeV4Runtime runtime;
    TitanThemeV4FrameInput local;
    const TitanThemeV4ResolvedLayer *resolved;
    if (!layer || !input || !out) return 0;
    TitanThemeV4_Init(&runtime);
    runtime.layer_count = 1u;
    runtime.layers[0] = layer->compiled;
    runtime.degradation = g_theme_v4_degradation;
    local = *input;
    if (local.elapsed_ms) runtime.elapsed_ms = local.elapsed_ms - (local.delta_ms <= local.elapsed_ms ? local.delta_ms : 0u);
    if (local.navigation_x == 0.0f) local.navigation_x = local.navigation;
    if (local.viewport_width <= 0.0f) local.viewport_width = 640.0f;
    if (local.viewport_height <= 0.0f) local.viewport_height = 448.0f;
    /* Preserve explicitly selected degradation instead of deriving pressure. */
    local.pressure_percent = g_theme_v4_degradation == TITAN_THEME_V4_DEGRADE_SAFE ? 95u :
        (g_theme_v4_degradation == TITAN_THEME_V4_DEGRADE_ENHANCED ? 85u :
        (g_theme_v4_degradation == TITAN_THEME_V4_DEGRADE_DECORATIVE ? 70u : 0u));
    TitanThemeV4_Update(&runtime, &local);
    resolved = &runtime.resolved[0];
    out->x = resolved->x;
    out->y = resolved->y;
    out->scale_x = resolved->scale_x;
    out->scale_y = resolved->scale_y;
    out->rotation = resolved->rotation_degrees;
    out->alpha = resolved->alpha;
    out->visible = resolved->visible;
    out->static_fast_path = resolved->static_fast_path;
    return 1;
}

void TitanThemeV4_EvaluateScene(
    const TitanThemeV4Scene *scene,
    const TitanThemeV4FrameInput *input,
    TitanThemeV4Telemetry *telemetry)
{
    TitanThemeV4Runtime runtime;
    TitanThemeV4FrameInput local;
    uint32_t i;
    if (!telemetry) return;
    memset(telemetry, 0, sizeof(*telemetry));
    if (!scene || !input) return;
    TitanThemeV4_Init(&runtime);
    runtime.layer_count = scene->layer_count;
    runtime.vram_budget = scene->vram_budget;
    runtime.ee_ram_budget = scene->ee_ram_budget;
    runtime.reserved_headroom = scene->reserved_headroom;
    runtime.event_count = scene->event_count;
    for (i = 0u; i < scene->event_count; i++) runtime.events[i] = scene->events[i];
    for (i = 0u; i < scene->layer_count; i++) runtime.layers[i] = scene->layers[i].compiled;
    local = *input;
    if (local.elapsed_ms) runtime.elapsed_ms = local.elapsed_ms - (local.delta_ms <= local.elapsed_ms ? local.delta_ms : 0u);
    if (local.navigation_x == 0.0f) local.navigation_x = local.navigation;
    if (local.viewport_width <= 0.0f) local.viewport_width = 640.0f;
    if (local.viewport_height <= 0.0f) local.viewport_height = 448.0f;
    local.pressure_percent = g_theme_v4_degradation == TITAN_THEME_V4_DEGRADE_SAFE ? 95u :
        (g_theme_v4_degradation == TITAN_THEME_V4_DEGRADE_ENHANCED ? 85u :
        (g_theme_v4_degradation == TITAN_THEME_V4_DEGRADE_DECORATIVE ? 70u : 0u));
    TitanThemeV4_Update(&runtime, &local);
    *telemetry = runtime.telemetry;
}
