#ifndef PROJECT_TITAN_THEME_SCENE_GRAPH_H
#define PROJECT_TITAN_THEME_SCENE_GRAPH_H
#include <stdint.h>
#define TITAN_THEME_SCENE_MAX_LAYERS 32u

typedef enum TitanThemeLayerType { TITAN_THEME_LAYER_BACKGROUND=0,TITAN_THEME_LAYER_MIDGROUND,TITAN_THEME_LAYER_FOREGROUND,TITAN_THEME_LAYER_SPRITE,TITAN_THEME_LAYER_LIGHT,TITAN_THEME_LAYER_PARTICLE,TITAN_THEME_LAYER_RIBBON,TITAN_THEME_LAYER_DECORATION,TITAN_THEME_LAYER_OVERLAY } TitanThemeLayerType;
typedef enum TitanThemeDirtyFlags { TITAN_THEME_DIRTY_NONE=0,TITAN_THEME_DIRTY_TRANSFORM=1u<<0,TITAN_THEME_DIRTY_FRAME=1u<<1,TITAN_THEME_DIRTY_ALPHA=1u<<2,TITAN_THEME_DIRTY_VISIBILITY=1u<<3,TITAN_THEME_DIRTY_ASSET=1u<<4,TITAN_THEME_DIRTY_CONTEXT=1u<<5 } TitanThemeDirtyFlags;
typedef struct TitanThemeCompiledLayer { TitanThemeLayerType type; int z_order; unsigned int declaration_order; float x,y,scale_x,scale_y,rotation,alpha,parallax; uint32_t asset_id; uint32_t dirty_flags; uint8_t visible; uint8_t context_mask; uint8_t blend_mode; uint8_t reserved; } TitanThemeCompiledLayer;
typedef struct TitanThemeCompiledScene { TitanThemeCompiledLayer layers[TITAN_THEME_SCENE_MAX_LAYERS]; unsigned int layer_count; } TitanThemeCompiledScene;
void TitanThemeScene_Init(TitanThemeCompiledScene *scene);
int TitanThemeScene_AddLayer(TitanThemeCompiledScene *scene,const TitanThemeCompiledLayer *layer);
void TitanThemeScene_StableSort(TitanThemeCompiledScene *scene);
void TitanThemeScene_MarkDirty(TitanThemeCompiledLayer *layer,uint32_t flags);
void TitanThemeScene_ClearDirty(TitanThemeCompiledLayer *layer,uint32_t flags);
#define TITAN_THEME_MAX_DECLARED_FRAMES 128u
#define TITAN_THEME_MAX_RESIDENT_FRAMES 3u
#define TITAN_THEME_FRAME_DURATION_MIN_MS 40u
#define TITAN_THEME_FRAME_DURATION_MAX_MS 2000u
typedef enum TitanThemeTimelineMode { TITAN_THEME_TIMELINE_LOOP=0,TITAN_THEME_TIMELINE_PINGPONG,TITAN_THEME_TIMELINE_ONCE,TITAN_THEME_TIMELINE_HOLD,TITAN_THEME_TIMELINE_RANDOM_START,TITAN_THEME_TIMELINE_RANDOM_SEQUENCE } TitanThemeTimelineMode;
typedef struct TitanThemeFrameDesc { uint32_t asset_id; uint16_t duration_ms; uint16_t reserved; } TitanThemeFrameDesc;
typedef struct TitanThemeTimeline { TitanThemeFrameDesc frames[TITAN_THEME_MAX_DECLARED_FRAMES]; unsigned int declared_count; unsigned int current_index; int direction; TitanThemeTimelineMode mode; uint32_t elapsed_ms; uint32_t deterministic_seed; } TitanThemeTimeline;
typedef struct TitanThemeResidency { int current; int next; int recent; unsigned int resident_count; } TitanThemeResidency;
int TitanThemeTimeline_Validate(const TitanThemeTimeline *timeline);
void TitanThemeTimeline_Reset(TitanThemeTimeline *timeline,uint32_t seed);
void TitanThemeTimeline_Update(TitanThemeTimeline *timeline,uint32_t delta_ms);
void TitanThemeTimeline_PlanResidency(const TitanThemeTimeline *timeline,TitanThemeResidency *residency);

#define TITAN_THEME_STREAM_QUEUE_CAPACITY 16u
typedef struct TitanThemeBudget { uint32_t vram_budget,ram_budget,prefetch_budget,upload_budget,reserved_headroom; } TitanThemeBudget;
typedef struct TitanThemeStreamRequest { uint32_t asset_id; uint32_t bytes; uint8_t priority; uint8_t valid; uint16_t reserved; } TitanThemeStreamRequest;
typedef struct TitanThemeStreamQueue { TitanThemeStreamRequest requests[TITAN_THEME_STREAM_QUEUE_CAPACITY]; unsigned int read_index,write_index,count; } TitanThemeStreamQueue;
typedef struct TitanThemeAtlasRegion { uint16_t x,y,width,height; uint16_t atlas_width,atlas_height; uint32_t asset_id; uint32_t shared_clut_id; } TitanThemeAtlasRegion;
int TitanThemeBudget_CanReserveVram(const TitanThemeBudget *budget,uint32_t current_used,uint32_t request_bytes,uint32_t vram_total);
void TitanThemeStream_Init(TitanThemeStreamQueue *queue);
int TitanThemeStream_Request(TitanThemeStreamQueue *queue,uint32_t asset_id,uint32_t bytes,uint8_t priority);
int TitanThemeStream_Pop(TitanThemeStreamQueue *queue,TitanThemeStreamRequest *request);
int TitanThemeAtlas_ValidateRegion(const TitanThemeAtlasRegion *region);

typedef enum TitanThemeQualityTier { TITAN_THEME_QUALITY_FULL=0,TITAN_THEME_QUALITY_BALANCED,TITAN_THEME_QUALITY_REDUCED } TitanThemeQualityTier;
typedef struct TitanThemeDynamicBudget { unsigned int max_particles,spawn_budget,update_budget,draw_budget; TitanThemeQualityTier quality; } TitanThemeDynamicBudget;
typedef struct TitanThemeParallaxInput { float navigation,focus,transition,idle; } TitanThemeParallaxInput;
float TitanThemeParallax_Offset(const TitanThemeCompiledLayer *layer,const TitanThemeParallaxInput *input);
void TitanThemeDynamic_ApplyPressure(TitanThemeDynamicBudget *budget,unsigned int pressure_percent);
#endif
