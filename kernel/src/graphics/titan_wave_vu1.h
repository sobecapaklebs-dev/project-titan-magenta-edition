#ifndef PROJECT_TITAN_WAVE_VU1_H
#define PROJECT_TITAN_WAVE_VU1_H

/*
 * Seven VU1 strips join eight spline rows into one compact XMB-style sheet.
 * Sixty-five horizontal samples preserve smooth curves at both 480i and 720p
 * while keeping each PATH1 pass safely inside VU1 data memory.
 */
#define TITAN_WAVE_VU1_POINT_COUNT 65u
#define TITAN_WAVE_VU1_LAYER_COUNT 7u

#define TITAN_WAVE_TEXTURE_ALLOCATION_NAME "wave-texture"
#define TITAN_WAVE_TEXTURE_WIDTH 128u
#define TITAN_WAVE_TEXTURE_HEIGHT 32u
#define TITAN_WAVE_TEXTURE_BYTES \
    (TITAN_WAVE_TEXTURE_WIDTH * TITAN_WAVE_TEXTURE_HEIGHT * 4u)

#define TITAN_WAVE_BLEND_ALPHA 0u
#define TITAN_WAVE_BLEND_ADDITIVE 1u

typedef struct TitanWaveVu1Point
{
    float x;
    float y;
    float normal_x;
    float normal_y;
} TitanWaveVu1Point;

typedef struct TitanWaveVu1Layer
{
    const TitanWaveVu1Point *points;
    float half_width;
    float texture_u_scale;
    float fresnel_bias;
    float fresnel_scale;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
    unsigned char glow_phase;
    unsigned char blend_mode;

    /* Optional VU1 procedural carrier. The EE supplies only phase/frequency
     * scalars; VU1 evaluates the per-point sine fields through EFU ESIN and
     * deforms the center line immediately before rail extrusion/XGKICK. */
    float procedural_phase;
    float procedural_frequency;
    float procedural_amplitude;
} TitanWaveVu1Layer;

/* Uploads the resident 16 KiB texture, VU1 program and immutable PATH1 state. */
void TitanWaveVu1_Initialize(void);
/* Re-uploads the resident wave texture and immutable VU1 state after a
 * temporary Titan Game Runtime GS-VRAM takeover. Existing packet objects are
 * reused; no heap/packet growth occurs on repeated game enter/exit cycles. */
int TitanWaveVu1_RebindVideoResources(void);

/*
 * Draws one texture-mapped triangle strip per layer.  A caller may supply a
 * conventional centerline + normal ribbon, or encode two adjacent mesh rows
 * as center=(row0+row1)/2 and normal=(row1-row0)/2 with half_width=1.
 */
int TitanWaveVu1_Draw(
    const TitanWaveVu1Layer *layers,
    unsigned int layer_count
);

int TitanWaveVu1_IsAvailable(void);

#endif
