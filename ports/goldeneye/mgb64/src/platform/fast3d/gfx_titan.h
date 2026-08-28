#ifndef MGB64_GFX_TITAN_H
#define MGB64_GFX_TITAN_H

#include "gfx_rendering_api.h"
#include "../../../../titan_ge_platform.h"

void gfx_titan_bind(TitanGePlatform *platform);
void gfx_titan_set_clear_color(float red, float green, float blue);
extern struct GfxRenderingAPI gfx_titan_api;

#endif
