#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "titan_theme_v4_runtime.h"

static char *read_all(const char *path)
{
    FILE *f=fopen(path,"rb"); long n; char *p;
    assert(f); assert(fseek(f,0,SEEK_END)==0); n=ftell(f); assert(n>0); rewind(f);
    p=(char*)malloc((size_t)n+1u); assert(p); assert(fread(p,1,(size_t)n,f)==(size_t)n); p[n]='\0'; fclose(f); return p;
}

int main(int argc,char **argv)
{
    TitanThemeV4Scene scene; TitanThemeV4FrameInput in; TitanThemeV4Telemetry t0,t1,t2; TitanThemeV4EvaluatedLayer a,b;
    char *manifest; assert(argc==2); manifest=read_all(argv[1]);
    assert(TitanThemeV4_ParseCompiled(manifest,&scene)); free(manifest);
    assert(scene.layer_count==14u); assert(scene.event_count==2u); assert(scene.declared_bytes==27072u);
    memset(&in,0,sizeof(in)); in.context=TITAN_THEME_V4_CONTEXT_HOME; in.viewport_width=64.0f; in.viewport_height=48.0f; in.navigation_x=0.5f; in.navigation_y=-0.25f; in.idle_drift=0.4f;
    in.elapsed_ms=1000u; TitanThemeV4_SetDegradation(TITAN_THEME_V4_DEGRADE_NONE); TitanThemeV4_EvaluateScene(&scene,&in,&t0);
    assert(t0.visible_layers>=9u); assert(t0.animated_layers>=8u); assert(t0.static_layers>=2u);
    assert(TitanThemeV4_EvaluateLayer(&scene.layers[1],&in,&a));
    in.elapsed_ms=4000u; assert(TitanThemeV4_EvaluateLayer(&scene.layers[1],&in,&b)); assert(a.x!=b.x);
    in.elapsed_ms=14700u; TitanThemeV4_EvaluateScene(&scene,&in,&t1); assert(t1.rare_events_active>=1u);
    in.top_transition=1.0f; in.context=TITAN_THEME_V4_CONTEXT_TOP; TitanThemeV4_EvaluateScene(&scene,&in,&t2); assert(t2.visible_layers>0u); assert(TitanThemeV4_EvaluateLayer(&scene.layers[7],&in,&a) && !a.visible); assert(TitanThemeV4_EvaluateLayer(&scene.layers[10],&in,&b) && b.visible);
    TitanThemeV4_SetDegradation(TITAN_THEME_V4_DEGRADE_ENHANCED); TitanThemeV4_EvaluateScene(&scene,&in,&t2); assert(t2.degradation_state==TITAN_THEME_V4_DEGRADE_ENHANCED); assert(t2.visible_layers<t0.visible_layers);
    puts("theme-v4-flagship: OK 14-layer/independent-timeline/rare-event/top-transition/degradation");
    return 0;
}
