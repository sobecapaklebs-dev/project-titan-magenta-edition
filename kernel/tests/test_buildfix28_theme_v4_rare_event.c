#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "titan_theme_v4_runtime.h"
static const char m[]=
"format=titan-scene-v4\nversion=1\nlayer_count=1\nvram_budget=524288\nee_ram_budget=1048576\nreserved_headroom=262144\nevent_count=1\nevent0_layer=0\nevent0_interval_ms=10000\nevent0_duration_ms=1000\nevent0_phase_ms=0\n"
"layer0_asset=event.rgb565\nlayer0_width=16\nlayer0_height=16\nlayer0_z=0\nlayer0_scale_x_milli=1000\nlayer0_scale_y_milli=1000\nlayer0_alpha=255\nlayer0_priority=2\nlayer0_context=3\nlayer0_mode=0\nlayer0_bytes=512\n";
int main(void){TitanThemeV4Runtime r;TitanThemeV4FrameInput i;TitanThemeV4Telemetry t;assert(TitanThemeV4_ParseManifest(&r,m));memset(&i,0,sizeof(i));i.context=3;i.viewport_width=640;i.viewport_height=448;i.delta_ms=500;TitanThemeV4_Update(&r,&i);TitanThemeV4_GetTelemetry(&r,&t);assert(t.visible_layers==1&&t.rare_events_active==1);i.delta_ms=1000;TitanThemeV4_Update(&r,&i);TitanThemeV4_GetTelemetry(&r,&t);assert(t.visible_layers==0&&t.rare_events_active==0);i.delta_ms=8500;TitanThemeV4_Update(&r,&i);TitanThemeV4_GetTelemetry(&r,&t);assert(t.visible_layers==1&&t.rare_events_active==1);puts("theme-v4-rare-event: OK deterministic bounded schedule");return 0;}
