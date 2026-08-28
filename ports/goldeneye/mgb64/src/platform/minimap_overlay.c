#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>

#ifdef PROJECT_TITAN_TGR
#include "sdl_titan_compat.h"
#else
#include <SDL.h>
#endif
#ifdef MGB64_PORTMASTER_GLES
#include <GLES3/gl32.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <glad/glad.h>
#endif

#include "gfx_pc.h"
#include "minimap.h"
#include "minimap_overlay.h"
#include "vi.h"

#define MINIMAP_OVERLAY_MAX_VERTICES 65536
#define MINIMAP_OVERLAY_PI 3.14159265358979323846f
#define MINIMAP_OVERLAY_FLOOR_WINDOW_Y 900.0f
#define MINIMAP_OVERLAY_LOCAL_RADIUS_WORLD 4500.0f
#define MINIMAP_OVERLAY_LOCAL_CONTEXT_RADIUS_WORLD 1700.0f
#define MINIMAP_OVERLAY_LOCAL_RADIUS_MIN_WORLD 1200.0f
#define MINIMAP_OVERLAY_LOCAL_RADIUS_PAD 1.28f

typedef struct MinimapOverlayVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
} MinimapOverlayVertex;

#ifdef __APPLE__
extern int gfx_metal_draw_minimap_overlay(const void *vertices,
                                          size_t vertex_count,
                                          int fb_width,
                                          int fb_height,
                                          int scissor_enabled,
                                          int scissor_x,
                                          int scissor_y,
                                          int scissor_w,
                                          int scissor_h);
#endif
#ifdef MGB64_WEBGPU_BACKEND
extern int gfx_webgpu_draw_minimap_overlay(const void *vertices,
                                           size_t vertex_count,
                                           int fb_width,
                                           int fb_height,
                                           int scissor_enabled,
                                           int scissor_x,
                                           int scissor_y,
                                           int scissor_w,
                                           int scissor_h);
#endif

typedef struct MinimapOverlayLayout {
    float x;
    float y;
    float w;
    float h;
    float pad;
    float scale;
    float screen_cx;
    float screen_cy;
    float world_cx;
    float world_cz;
    float world_x_min;
    float world_z_min;
    float world_x_max;
    float world_z_max;
    float rotate_sin;
    float rotate_cos;
    u8 local_mode;
    u8 rotate_mode;
} MinimapOverlayLayout;

typedef struct MinimapOverlayGlState {
    GLint program;
    GLint vao;
    GLint array_buffer;
    GLint draw_fbo;
    GLint read_fbo;
    GLint viewport[4];
    GLint scissor_box[4];
    GLint blend_src_rgb;
    GLint blend_dst_rgb;
    GLint blend_src_alpha;
    GLint blend_dst_alpha;
    GLint blend_eq_rgb;
    GLint blend_eq_alpha;
    GLboolean blend;
    GLboolean depth_test;
    GLboolean scissor;
    GLboolean cull_face;
    GLboolean sample_alpha_to_coverage;
    GLboolean depth_mask;
} MinimapOverlayGlState;

static GLuint s_minimap_program;
static GLuint s_minimap_vbo;
static GLuint s_minimap_vao;
static GLint s_minimap_screen_location = -1;
static int s_minimap_shader_failed;
static MinimapOverlayVertex s_minimap_vertices[MINIMAP_OVERLAY_MAX_VERTICES];
static size_t s_minimap_vertex_count;
static u32 s_minimap_trace_draw_calls;
static u32 s_minimap_trace_vertices_flushed;
static int s_minimap_overlay_metal_backend;
static int s_minimap_overlay_webgpu_backend;
static int s_minimap_overlay_submit_failed;
static int s_minimap_overlay_fb_width;
static int s_minimap_overlay_fb_height;
static int s_minimap_overlay_scissor_enabled;
static int s_minimap_overlay_scissor_x;
static int s_minimap_overlay_scissor_y;
static int s_minimap_overlay_scissor_w;
static int s_minimap_overlay_scissor_h;

typedef struct MinimapOverlayTraceSummary {
    const char *status;
    u32 queued_frames;
    u32 drawn_frames;
    u32 layout_failures;
    u32 objective_pins;
    u32 enemy_pins;
    u32 draw_calls;
    u32 vertices_flushed;
    int fb_width;
    int fb_height;
    u32 cache_ready;
    u32 cache_poly_count;
    u32 cache_overflow_count;
} MinimapOverlayTraceSummary;

static const char *minimap_overlay_dump_path(void)
{
    /* PERF-021: this is consulted on every overlay update path — including the
     * early-outs taken when the minimap is off — so latch the getenv() once. The
     * environment is immutable per run and getenv() returns a process-lifetime
     * pointer, so the resolved value is safe to cache. */
    static const char *cached = NULL;
    static int resolved = 0;

    if (!resolved) {
        const char *path = getenv("GE007_MINIMAP_OVERLAY_DUMP");
        if (path == NULL || path[0] == '\0' || (path[0] == '0' && path[1] == '\0')) {
            path = NULL;
        }
        cached = path;
        resolved = 1;
    }

    return cached;
}

static void minimap_overlay_dump_summary(const MinimapOverlayTraceSummary *summary)
{
    const char *path = minimap_overlay_dump_path();
    FILE *file;

    if (path == NULL || summary == NULL) {
        return;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return;
    }

    fprintf(file, "{\n");
    fprintf(file, "  \"status\": \"%s\",\n", summary->status != NULL ? summary->status : "unknown");
    fprintf(file, "  \"queued_frames\": %u,\n", summary->queued_frames);
    fprintf(file, "  \"drawn_frames\": %u,\n", summary->drawn_frames);
    fprintf(file, "  \"layout_failures\": %u,\n", summary->layout_failures);
    fprintf(file, "  \"objective_pins\": %u,\n", summary->objective_pins);
    fprintf(file, "  \"enemy_pins\": %u,\n", summary->enemy_pins);
    fprintf(file, "  \"draw_calls\": %u,\n", summary->draw_calls);
    fprintf(file, "  \"vertices_flushed\": %u,\n", summary->vertices_flushed);
    fprintf(file, "  \"fb_width\": %d,\n", summary->fb_width);
    fprintf(file, "  \"fb_height\": %d,\n", summary->fb_height);
    fprintf(file, "  \"cache_ready\": %u,\n", summary->cache_ready);
    fprintf(file, "  \"cache_poly_count\": %u,\n", summary->cache_poly_count);
    fprintf(file, "  \"cache_overflow_count\": %u\n", summary->cache_overflow_count);
    fprintf(file, "}\n");
    fclose(file);
}

static float minimap_overlay_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float minimap_overlay_minf(float a, float b)
{
    return a < b ? a : b;
}

static float minimap_overlay_maxf(float a, float b)
{
    return a > b ? a : b;
}

static int minimap_overlay_finite2(float x, float y)
{
    return __builtin_isfinite(x) && __builtin_isfinite(y);
}

static GLuint minimap_overlay_compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint success = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        char error_log[1024];
        GLsizei length = 0;

        glGetShaderInfoLog(shader, sizeof(error_log), &length, error_log);
        fprintf(stderr,
                "[minimap] overlay shader compile failed: %.*s\n",
                (int)length,
                error_log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static int minimap_overlay_ensure_program(void)
{
    static const char *vs_source =
#ifdef MGB64_PORTMASTER_GLES
        "#version 320 es\n"
        "precision mediump float;\n"
#else
        "#version 330 core\n"
#endif
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec4 aColor;\n"
        "uniform vec2 uScreen;\n"
        "out vec4 vColor;\n"
        "void main() {\n"
        "    vec2 ndc = vec2((aPos.x / uScreen.x) * 2.0 - 1.0,\n"
        "                    1.0 - (aPos.y / uScreen.y) * 2.0);\n"
        "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
        "    vColor = aColor;\n"
        "}\n";
    static const char *fs_source =
#ifdef MGB64_PORTMASTER_GLES
        "#version 320 es\n"
        "precision mediump float;\n"
#else
        "#version 330 core\n"
#endif
        "in vec4 vColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = vColor;\n"
        "}\n";
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLint success = GL_FALSE;

    if (s_minimap_program != 0) {
        return 1;
    }
    if (s_minimap_shader_failed) {
        return 0;
    }

    vertex_shader = minimap_overlay_compile_shader(GL_VERTEX_SHADER, vs_source);
    if (vertex_shader == 0) {
        s_minimap_shader_failed = 1;
        return 0;
    }

    fragment_shader = minimap_overlay_compile_shader(GL_FRAGMENT_SHADER, fs_source);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        s_minimap_shader_failed = 1;
        return 0;
    }

    s_minimap_program = glCreateProgram();
    glAttachShader(s_minimap_program, vertex_shader);
    glAttachShader(s_minimap_program, fragment_shader);
    glLinkProgram(s_minimap_program);
    glGetProgramiv(s_minimap_program, GL_LINK_STATUS, &success);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    if (success != GL_TRUE) {
        char error_log[1024];
        GLsizei length = 0;

        glGetProgramInfoLog(s_minimap_program, sizeof(error_log), &length, error_log);
        fprintf(stderr,
                "[minimap] overlay shader link failed: %.*s\n",
                (int)length,
                error_log);
        glDeleteProgram(s_minimap_program);
        s_minimap_program = 0;
        s_minimap_shader_failed = 1;
        return 0;
    }

    s_minimap_screen_location = glGetUniformLocation(s_minimap_program, "uScreen");

    glGenVertexArrays(1, &s_minimap_vao);
    glGenBuffers(1, &s_minimap_vbo);
    glBindVertexArray(s_minimap_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_minimap_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(MinimapOverlayVertex),
                          (void *)offsetof(MinimapOverlayVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(MinimapOverlayVertex),
                          (void *)offsetof(MinimapOverlayVertex, r));

    return 1;
}

static void minimap_overlay_flush(void)
{
    if (s_minimap_vertex_count == 0) {
        return;
    }

    if (s_minimap_overlay_metal_backend) {
#ifdef __APPLE__
        if (!gfx_metal_draw_minimap_overlay(s_minimap_vertices,
                                            s_minimap_vertex_count,
                                            s_minimap_overlay_fb_width,
                                            s_minimap_overlay_fb_height,
                                            s_minimap_overlay_scissor_enabled,
                                            s_minimap_overlay_scissor_x,
                                            s_minimap_overlay_scissor_y,
                                            s_minimap_overlay_scissor_w,
                                            s_minimap_overlay_scissor_h)) {
            s_minimap_overlay_submit_failed = 1;
        } else {
            s_minimap_trace_draw_calls++;
            s_minimap_trace_vertices_flushed += (u32)s_minimap_vertex_count;
        }
#else
        s_minimap_overlay_submit_failed = 1;
#endif
        s_minimap_vertex_count = 0;
        return;
    }

    if (s_minimap_overlay_webgpu_backend) {
#ifdef MGB64_WEBGPU_BACKEND
        if (!gfx_webgpu_draw_minimap_overlay(s_minimap_vertices,
                                             s_minimap_vertex_count,
                                             s_minimap_overlay_fb_width,
                                             s_minimap_overlay_fb_height,
                                             s_minimap_overlay_scissor_enabled,
                                             s_minimap_overlay_scissor_x,
                                             s_minimap_overlay_scissor_y,
                                             s_minimap_overlay_scissor_w,
                                             s_minimap_overlay_scissor_h)) {
            s_minimap_overlay_submit_failed = 1;
        } else {
            s_minimap_trace_draw_calls++;
            s_minimap_trace_vertices_flushed += (u32)s_minimap_vertex_count;
        }
#else
        s_minimap_overlay_submit_failed = 1;
#endif
        s_minimap_vertex_count = 0;
        return;
    }

    glBindVertexArray(s_minimap_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_minimap_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(s_minimap_vertex_count * sizeof(s_minimap_vertices[0])),
                 s_minimap_vertices,
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)s_minimap_vertex_count);
    s_minimap_trace_draw_calls++;
    s_minimap_trace_vertices_flushed += (u32)s_minimap_vertex_count;
    s_minimap_vertex_count = 0;
}

static void minimap_overlay_disable_scissor(void)
{
    minimap_overlay_flush();
    if (s_minimap_overlay_metal_backend || s_minimap_overlay_webgpu_backend) {
        /* No GL context on Metal/WebGPU — scissor is state-only, applied by the
         * backend draw hook. */
        s_minimap_overlay_scissor_enabled = 0;
        return;
    }
    glDisable(GL_SCISSOR_TEST);
}

static void minimap_overlay_push_vertex(float x,
                                        float y,
                                        float r,
                                        float g,
                                        float b,
                                        float a)
{
    MinimapOverlayVertex *vertex;

    if (s_minimap_vertex_count >= MINIMAP_OVERLAY_MAX_VERTICES) {
        minimap_overlay_flush();
    }

    vertex = &s_minimap_vertices[s_minimap_vertex_count++];
    vertex->x = x;
    vertex->y = y;
    vertex->r = r;
    vertex->g = g;
    vertex->b = b;
    vertex->a = minimap_overlay_clampf(a, 0.0f, 1.0f);
}

static void minimap_overlay_push_tri(float x0,
                                     float y0,
                                     float x1,
                                     float y1,
                                     float x2,
                                     float y2,
                                     float r,
                                     float g,
                                     float b,
                                     float a)
{
    if (s_minimap_vertex_count + 3 > MINIMAP_OVERLAY_MAX_VERTICES) {
        minimap_overlay_flush();
    }

    minimap_overlay_push_vertex(x0, y0, r, g, b, a);
    minimap_overlay_push_vertex(x1, y1, r, g, b, a);
    minimap_overlay_push_vertex(x2, y2, r, g, b, a);
}

static void minimap_overlay_push_rect(float x,
                                      float y,
                                      float w,
                                      float h,
                                      float r,
                                      float g,
                                      float b,
                                      float a)
{
    minimap_overlay_push_tri(x, y, x + w, y, x + w, y + h, r, g, b, a);
    minimap_overlay_push_tri(x, y, x + w, y + h, x, y + h, r, g, b, a);
}

static void minimap_overlay_push_diamond(float cx,
                                         float cy,
                                         float radius,
                                         float r,
                                         float g,
                                         float b,
                                         float a)
{
    minimap_overlay_push_tri(cx, cy - radius, cx + radius, cy, cx, cy + radius, r, g, b, a);
    minimap_overlay_push_tri(cx, cy - radius, cx, cy + radius, cx - radius, cy, r, g, b, a);
}

static void minimap_overlay_push_line(float x0,
                                      float y0,
                                      float x1,
                                      float y1,
                                      float thickness,
                                      float r,
                                      float g,
                                      float b,
                                      float a)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len_sq = dx * dx + dy * dy;
    float len;
    float nx;
    float ny;

    if (len_sq <= 0.0001f
        || !minimap_overlay_finite2(x0, y0)
        || !minimap_overlay_finite2(x1, y1)) {
        return;
    }

    len = sqrtf(len_sq);
    nx = (-dy / len) * thickness * 0.5f;
    ny = (dx / len) * thickness * 0.5f;

    minimap_overlay_push_tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, r, g, b, a);
    minimap_overlay_push_tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, r, g, b, a);
}

static int minimap_overlay_point_in_panel(const MinimapOverlayLayout *layout, float x, float y)
{
    return x >= layout->x
        && y >= layout->y
        && x <= layout->x + layout->w
        && y <= layout->y + layout->h;
}

static void minimap_overlay_content_rect(const MinimapOverlayLayout *layout,
                                         float *x,
                                         float *y,
                                         float *w,
                                         float *h)
{
    float inset = layout->pad * 0.74f;

    *x = layout->x + inset;
    *y = layout->y + inset;
    *w = layout->w - inset * 2.0f;
    *h = layout->h - inset * 2.0f;
}

static int minimap_overlay_point_in_content(const MinimapOverlayLayout *layout, float px, float py)
{
    float x;
    float y;
    float w;
    float h;

    minimap_overlay_content_rect(layout, &x, &y, &w, &h);
    return px >= x && py >= y && px <= x + w && py <= y + h;
}

static void minimap_overlay_project(const MinimapOverlayLayout *layout,
                                    float world_x,
                                    float world_z,
                                    float *screen_x,
                                    float *screen_y);

static int minimap_overlay_poly_visible(const MinimapOverlayLayout *layout, const MinimapPoly *poly)
{
    if (!layout->rotate_mode) {
        return poly->x_max >= layout->world_x_min
            && poly->x_min <= layout->world_x_max
            && poly->z_max >= layout->world_z_min
            && poly->z_min <= layout->world_z_max;
    }

    {
        float content_x;
        float content_y;
        float content_w;
        float content_h;
        float sx[4];
        float sy[4];
        float screen_x_min = 3.402823466e+38f;
        float screen_y_min = 3.402823466e+38f;
        float screen_x_max = -3.402823466e+38f;
        float screen_y_max = -3.402823466e+38f;
        float slack = minimap_overlay_maxf(2.0f, layout->w * 0.012f);
        int i;

        minimap_overlay_content_rect(layout, &content_x, &content_y, &content_w, &content_h);
        minimap_overlay_project(layout, poly->x_min, poly->z_min, &sx[0], &sy[0]);
        minimap_overlay_project(layout, poly->x_max, poly->z_min, &sx[1], &sy[1]);
        minimap_overlay_project(layout, poly->x_max, poly->z_max, &sx[2], &sy[2]);
        minimap_overlay_project(layout, poly->x_min, poly->z_max, &sx[3], &sy[3]);

        for (i = 0; i < 4; i++) {
            if (!minimap_overlay_finite2(sx[i], sy[i])) {
                return 0;
            }
            if (sx[i] < screen_x_min) screen_x_min = sx[i];
            if (sy[i] < screen_y_min) screen_y_min = sy[i];
            if (sx[i] > screen_x_max) screen_x_max = sx[i];
            if (sy[i] > screen_y_max) screen_y_max = sy[i];
        }

        return screen_x_max >= content_x - slack
            && screen_x_min <= content_x + content_w + slack
            && screen_y_max >= content_y - slack
            && screen_y_min <= content_y + content_h + slack;
    }
}

static void minimap_overlay_project(const MinimapOverlayLayout *layout,
                                    float world_x,
                                    float world_z,
                                    float *screen_x,
                                    float *screen_y)
{
    float dx = world_x - layout->world_cx;
    float dy = -(world_z - layout->world_cz);

    if (layout->rotate_mode) {
        float rotated_x = dx * layout->rotate_cos - dy * layout->rotate_sin;
        float rotated_y = dx * layout->rotate_sin + dy * layout->rotate_cos;

        dx = rotated_x;
        dy = rotated_y;
    }

    *screen_x = layout->screen_cx + dx * layout->scale;
    *screen_y = layout->screen_cy + dy * layout->scale;
}

static void minimap_overlay_set_scissor_rect(float x,
                                             float y,
                                             float w,
                                             float h,
                                             int fb_width,
                                             int fb_height)
{
    int x0 = (int)floorf(x);
    int y0 = (int)floorf((float)fb_height - (y + h));
    int x1 = (int)ceilf(x + w);
    int y1 = (int)ceilf((float)fb_height - y);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb_width) x1 = fb_width;
    if (y1 > fb_height) y1 = fb_height;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;

    minimap_overlay_flush();
    if (s_minimap_overlay_metal_backend || s_minimap_overlay_webgpu_backend) {
        s_minimap_overlay_scissor_enabled = 1;
        s_minimap_overlay_scissor_x = x0;
        s_minimap_overlay_scissor_y = y0;
        s_minimap_overlay_scissor_w = x1 - x0;
        s_minimap_overlay_scissor_h = y1 - y0;
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, y0, x1 - x0, y1 - y0);
}

static void minimap_overlay_set_panel_scissor(const MinimapOverlayLayout *layout,
                                             int fb_width,
                                             int fb_height)
{
    minimap_overlay_set_scissor_rect(layout->x,
                                     layout->y,
                                     layout->w,
                                     layout->h,
                                     fb_width,
                                     fb_height);
}

static void minimap_overlay_set_content_scissor(const MinimapOverlayLayout *layout,
                                               int fb_width,
                                               int fb_height)
{
    float x;
    float y;
    float w;
    float h;

    minimap_overlay_content_rect(layout, &x, &y, &w, &h);
    minimap_overlay_set_scissor_rect(x,
                                     y,
                                     w,
                                     h,
                                     fb_width,
                                     fb_height);
}

static void minimap_overlay_clamp_to_content_edge(const MinimapOverlayLayout *layout,
                                                  float target_x,
                                                  float target_y,
                                                  float *edge_x,
                                                  float *edge_y,
                                                  float *dir_x,
                                                  float *dir_y)
{
    float x;
    float y;
    float w;
    float h;
    float cx;
    float cy;
    float dx;
    float dy;
    float t = 1.0f;

    minimap_overlay_content_rect(layout, &x, &y, &w, &h);
    cx = layout->screen_cx;
    cy = layout->screen_cy;
    dx = target_x - cx;
    dy = target_y - cy;

    if (fabsf(dx) <= 0.0001f && fabsf(dy) <= 0.0001f) {
        dx = 0.0f;
        dy = -1.0f;
    }

    if (dx > 0.0f) {
        t = minimap_overlay_minf(t, (x + w - cx) / dx);
    } else if (dx < 0.0f) {
        t = minimap_overlay_minf(t, (x - cx) / dx);
    }
    if (dy > 0.0f) {
        t = minimap_overlay_minf(t, (y + h - cy) / dy);
    } else if (dy < 0.0f) {
        t = minimap_overlay_minf(t, (y - cy) / dy);
    }

    *edge_x = minimap_overlay_clampf(cx + dx * t, x, x + w);
    *edge_y = minimap_overlay_clampf(cy + dy * t, y, y + h);

    {
        float len = sqrtf(dx * dx + dy * dy);
        if (len <= 0.0001f) {
            *dir_x = 0.0f;
            *dir_y = -1.0f;
        } else {
            *dir_x = dx / len;
            *dir_y = dy / len;
        }
    }
}

static float minimap_overlay_poly_focus_alpha(const MinimapOverlayLayout *layout,
                                              const MinimapPoly *poly)
{
    float radius;
    float center_x;
    float center_z;
    float dx;
    float dz;
    float distance;

    if (!layout->local_mode) {
        return 1.0f;
    }

    radius = (layout->world_x_max - layout->world_x_min) * 0.5f;
    if (radius <= 1.0f) {
        return 1.0f;
    }

    center_x = (poly->x_min + poly->x_max) * 0.5f;
    center_z = (poly->z_min + poly->z_max) * 0.5f;
    dx = center_x - layout->world_cx;
    dz = center_z - layout->world_cz;
    distance = sqrtf(dx * dx + dz * dz) / radius;

    return minimap_overlay_clampf(1.12f - distance * 0.84f, 0.32f, 1.0f);
}

static int minimap_overlay_context_bbox(const MinimapLevelCache *cache,
                                        const MinimapFrame *frame,
                                        float *x_min,
                                        float *z_min,
                                        float *x_max,
                                        float *z_max)
{
    u32 poly_index;
    u32 count = 0;

    *x_min = cache->x_max;
    *z_min = cache->z_max;
    *x_max = cache->x_min;
    *z_max = cache->z_min;

    for (poly_index = 0; poly_index < cache->poly_count; poly_index++) {
        const MinimapPoly *poly = &cache->polys[poly_index];
        int current_room = (poly->room == (u8)frame->player_room);
        int same_floor = fabsf(poly->y_avg - frame->player_y) <= MINIMAP_OVERLAY_FLOOR_WINDOW_Y;

        if (!current_room && !same_floor) {
            continue;
        }
        if (!minimap_overlay_finite2(poly->x_min, poly->z_min)
            || !minimap_overlay_finite2(poly->x_max, poly->z_max)) {
            continue;
        }

        if (poly->x_min < *x_min) *x_min = poly->x_min;
        if (poly->z_min < *z_min) *z_min = poly->z_min;
        if (poly->x_max > *x_max) *x_max = poly->x_max;
        if (poly->z_max > *z_max) *z_max = poly->z_max;
        count++;
    }

    return count >= 3 && *x_max > *x_min && *z_max > *z_min;
}

static int minimap_overlay_local_bbox(const MinimapLevelCache *cache,
                                      const MinimapFrame *frame,
                                      float search_radius,
                                      float *x_min,
                                      float *z_min,
                                      float *x_max,
                                      float *z_max)
{
    u32 poly_index;
    u32 count = 0;
    float search_x_min = frame->player_x - search_radius;
    float search_z_min = frame->player_z - search_radius;
    float search_x_max = frame->player_x + search_radius;
    float search_z_max = frame->player_z + search_radius;

    *x_min = cache->x_max;
    *z_min = cache->z_max;
    *x_max = cache->x_min;
    *z_max = cache->z_min;

    for (poly_index = 0; poly_index < cache->poly_count; poly_index++) {
        const MinimapPoly *poly = &cache->polys[poly_index];
        int current_room = (poly->room == (u8)frame->player_room);
        int same_floor = fabsf(poly->y_avg - frame->player_y) <= MINIMAP_OVERLAY_FLOOR_WINDOW_Y;

        if (!current_room && !same_floor) {
            continue;
        }
        if (poly->x_max < search_x_min
            || poly->x_min > search_x_max
            || poly->z_max < search_z_min
            || poly->z_min > search_z_max) {
            continue;
        }
        if (!minimap_overlay_finite2(poly->x_min, poly->z_min)
            || !minimap_overlay_finite2(poly->x_max, poly->z_max)) {
            continue;
        }

        if (poly->x_min < *x_min) *x_min = poly->x_min;
        if (poly->z_min < *z_min) *z_min = poly->z_min;
        if (poly->x_max > *x_max) *x_max = poly->x_max;
        if (poly->z_max > *z_max) *z_max = poly->z_max;
        count++;
    }

    return count > 0 && *x_max > *x_min && *z_max > *z_min;
}

static int minimap_overlay_view_rect(const MinimapFrame *frame,
                                     float logical_w,
                                     float logical_h,
                                     int fb_width,
                                     int fb_height,
                                     float *rect_x,
                                     float *rect_y,
                                     float *rect_w,
                                     float *rect_h)
{
    float left;
    float top;
    float right;
    float bottom;
    float scale_x;
    float scale_y;

    if (frame == NULL
        || logical_w <= 0.0f
        || logical_h <= 0.0f
        || fb_width <= 0
        || fb_height <= 0) {
        return 0;
    }

    left = (float)frame->view_left;
    top = (float)frame->view_top;
    right = left + (float)frame->view_width;
    bottom = top + (float)frame->view_height;

    if (left < 0.0f) left = 0.0f;
    if (top < 0.0f) top = 0.0f;
    if (right > logical_w) right = logical_w;
    if (bottom > logical_h) bottom = logical_h;

    if (right <= left || bottom <= top) {
        left = 0.0f;
        top = 0.0f;
        right = logical_w;
        bottom = logical_h;
    }

    scale_x = (float)fb_width / logical_w;
    scale_y = (float)fb_height / logical_h;
    *rect_x = left * scale_x;
    *rect_y = top * scale_y;
    *rect_w = (right - left) * scale_x;
    *rect_h = (bottom - top) * scale_y;

    return *rect_w > 0.0f && *rect_h > 0.0f;
}

static int minimap_overlay_build_layout(const MinimapLevelCache *cache,
                                        const MinimapFrame *frame,
                                        int fb_width,
                                        int fb_height,
                                        MinimapOverlayLayout *layout)
{
    float logical_w = (float)viGetX();
    float logical_h = (float)viGetY();
    float scale_x;
    float scale_y;
    float scale_min;
    float view_x;
    float view_y;
    float view_width;
    float view_height;
    float min_view;
    float size_setting;
    float size_logical;
    float size_px;
    float max_size_px;
    float margin_px;
    float map_x_min;
    float map_z_min;
    float map_x_max;
    float map_z_max;
    float map_w;
    float map_h;
    float draw_w;
    float draw_h;

    if (logical_w <= 0.0f) {
        logical_w = 320.0f;
    }
    if (logical_h <= 0.0f) {
        logical_h = 240.0f;
    }

    scale_x = (float)fb_width / logical_w;
    scale_y = (float)fb_height / logical_h;
    scale_min = minimap_overlay_minf(scale_x, scale_y);

    if (!minimap_overlay_view_rect(frame,
                                   logical_w,
                                   logical_h,
                                   fb_width,
                                   fb_height,
                                   &view_x,
                                   &view_y,
                                   &view_width,
                                   &view_height)) {
        return 0;
    }
    min_view = minimap_overlay_minf((float)frame->view_width, (float)frame->view_height);
    if (min_view <= 0.0f || view_width <= 0.0f || view_height <= 0.0f) {
        return 0;
    }

    size_setting = minimap_overlay_clampf(g_pcMinimapSize, 0.5f, 2.0f);
    size_logical = minimap_overlay_clampf(min_view * 0.255f * size_setting,
                                          46.0f,
                                          minimap_overlay_maxf(46.0f, min_view - 6.0f));
    size_px = size_logical * scale_min;
    margin_px = 2.0f * scale_min;
    max_size_px = minimap_overlay_minf(view_width, view_height) - margin_px * 2.0f;
    if (max_size_px <= 0.0f) {
        return 0;
    }
    if (size_px > max_size_px) {
        size_px = max_size_px;
    }

    layout->w = size_px;
    layout->h = size_px;
    layout->x = view_x + view_width - margin_px - layout->w;
    layout->y = view_y + margin_px;

    if (layout->x < view_x + margin_px) {
        layout->x = view_x + margin_px;
    }
    if (layout->y + layout->h > view_y + view_height - margin_px) {
        layout->y = view_y + view_height - margin_px - layout->h;
    }

    layout->local_mode = frame->mode == 1 ? 0 : 1;
    layout->rotate_mode = frame->mode == 2 ? 1 : 0;
    if (layout->rotate_mode) {
        float theta = frame->player_theta_deg * (MINIMAP_OVERLAY_PI / 180.0f);

        layout->rotate_sin = sinf(theta);
        layout->rotate_cos = cosf(theta);
    } else {
        layout->rotate_sin = 0.0f;
        layout->rotate_cos = 1.0f;
    }
    if (layout->local_mode) {
        float radius = MINIMAP_OVERLAY_LOCAL_RADIUS_WORLD;
        float local_x_min;
        float local_z_min;
        float local_x_max;
        float local_z_max;

        if (minimap_overlay_local_bbox(cache,
                                       frame,
                                       MINIMAP_OVERLAY_LOCAL_CONTEXT_RADIUS_WORLD,
                                       &local_x_min,
                                       &local_z_min,
                                       &local_x_max,
                                       &local_z_max)) {
            float dx = minimap_overlay_maxf(fabsf(local_x_min - frame->player_x),
                                            fabsf(local_x_max - frame->player_x));
            float dz = minimap_overlay_maxf(fabsf(local_z_min - frame->player_z),
                                            fabsf(local_z_max - frame->player_z));

            radius = minimap_overlay_clampf(minimap_overlay_maxf(dx, dz) * MINIMAP_OVERLAY_LOCAL_RADIUS_PAD,
                                            MINIMAP_OVERLAY_LOCAL_RADIUS_MIN_WORLD,
                                            MINIMAP_OVERLAY_LOCAL_RADIUS_WORLD);
        }
        map_x_min = frame->player_x - radius;
        map_z_min = frame->player_z - radius;
        map_x_max = frame->player_x + radius;
        map_z_max = frame->player_z + radius;
    } else {
        if (!minimap_overlay_context_bbox(cache, frame, &map_x_min, &map_z_min, &map_x_max, &map_z_max)) {
            map_x_min = cache->x_min;
            map_z_min = cache->z_min;
            map_x_max = cache->x_max;
            map_z_max = cache->z_max;
        }
    }

    map_w = map_x_max - map_x_min;
    map_h = map_z_max - map_z_min;
    if (map_w <= 0.0f || map_h <= 0.0f) {
        return 0;
    }

    layout->pad = minimap_overlay_maxf(5.0f * scale_min, layout->w * 0.060f);
    draw_w = layout->w - layout->pad * 2.0f;
    draw_h = layout->h - layout->pad * 2.0f;
    if (draw_w <= 0.0f || draw_h <= 0.0f) {
        return 0;
    }

    layout->scale = minimap_overlay_minf(draw_w / map_w, draw_h / map_h);
    layout->screen_cx = layout->x + layout->w * 0.5f;
    layout->screen_cy = layout->y + layout->h * 0.5f;
    layout->world_cx = (map_x_min + map_x_max) * 0.5f;
    layout->world_cz = (map_z_min + map_z_max) * 0.5f;
    layout->world_x_min = map_x_min;
    layout->world_z_min = map_z_min;
    layout->world_x_max = map_x_max;
    layout->world_z_max = map_z_max;

    return layout->scale > 0.0f;
}

static void minimap_overlay_draw_shadow(const MinimapOverlayLayout *layout, float opacity)
{
    float shadow = minimap_overlay_maxf(2.0f, layout->w * 0.025f);

    minimap_overlay_push_rect(layout->x + shadow,
                              layout->y + shadow,
                              layout->w,
                              layout->h,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.30f * opacity);
    minimap_overlay_push_rect(layout->x - shadow * 0.35f,
                              layout->y - shadow * 0.35f,
                              layout->w + shadow * 0.70f,
                              layout->h + shadow * 0.70f,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.16f * opacity);
}

static void minimap_overlay_draw_panel(const MinimapOverlayLayout *layout, float opacity)
{
    float border = minimap_overlay_maxf(1.0f, layout->w * 0.008f);
    float inner = minimap_overlay_maxf(2.0f, layout->w * 0.022f);

    minimap_overlay_push_rect(layout->x,
                              layout->y,
                              layout->w,
                              layout->h,
                              0.006f,
                              0.010f,
                              0.012f,
                              0.86f * opacity);
    minimap_overlay_push_rect(layout->x + inner,
                              layout->y + inner,
                              layout->w - inner * 2.0f,
                              layout->h - inner * 2.0f,
                              0.018f,
                              0.032f,
                              0.034f,
                              0.56f * opacity);
    minimap_overlay_push_rect(layout->x,
                              layout->y,
                              layout->w,
                              border,
                              0.78f,
                              0.95f,
                              0.92f,
                              0.58f * opacity);
    minimap_overlay_push_rect(layout->x,
                              layout->y + layout->h - border,
                              layout->w,
                              border,
                              0.78f,
                              0.95f,
                              0.92f,
                              0.22f * opacity);
    minimap_overlay_push_rect(layout->x,
                              layout->y,
                              border,
                              layout->h,
                              0.78f,
                              0.95f,
                              0.92f,
                              0.28f * opacity);
    minimap_overlay_push_rect(layout->x + layout->w - border,
                              layout->y,
                              border,
                              layout->h,
                              0.78f,
                              0.95f,
                              0.92f,
                              0.34f * opacity);
}

static void minimap_overlay_draw_edge_mask(const MinimapOverlayLayout *layout, float opacity)
{
    float x;
    float y;
    float w;
    float h;
    float edge = minimap_overlay_clampf(layout->w * 0.045f, 3.0f, 9.0f);

    minimap_overlay_content_rect(layout, &x, &y, &w, &h);
    if (w <= edge * 2.0f || h <= edge * 2.0f) {
        return;
    }

    minimap_overlay_push_rect(x, y, w, edge, 0.004f, 0.007f, 0.009f, 0.18f * opacity);
    minimap_overlay_push_rect(x, y + h - edge, w, edge, 0.004f, 0.007f, 0.009f, 0.15f * opacity);
    minimap_overlay_push_rect(x, y, edge, h, 0.004f, 0.007f, 0.009f, 0.13f * opacity);
    minimap_overlay_push_rect(x + w - edge, y, edge, h, 0.004f, 0.007f, 0.009f, 0.13f * opacity);
}

static void minimap_overlay_draw_polys(const MinimapLevelCache *cache,
                                       const MinimapFrame *frame,
                                       const MinimapOverlayLayout *layout,
                                       int current_room_pass,
                                       float opacity)
{
    u32 poly_index;

    for (poly_index = 0; poly_index < cache->poly_count; poly_index++) {
        const MinimapPoly *poly = &cache->polys[poly_index];
        float sx[MINIMAP_MAX_POLY_POINTS];
        float sy[MINIMAP_MAX_POLY_POINTS];
        float r;
        float g;
        float b;
        float a;
        float focus_alpha;
        int current_room = (poly->room == (u8)frame->player_room);
        int same_floor = fabsf(poly->y_avg - frame->player_y) <= MINIMAP_OVERLAY_FLOOR_WINDOW_Y;
        int i;

        if ((current_room_pass && !current_room) || (!current_room_pass && current_room)) {
            continue;
        }
        if (!current_room && !same_floor) {
            continue;
        }
        if (!minimap_overlay_poly_visible(layout, poly)) {
            continue;
        }
        if (poly->point_count < 3 || poly->point_count > MINIMAP_MAX_POLY_POINTS) {
            continue;
        }

        for (i = 0; i < poly->point_count; i++) {
            minimap_overlay_project(layout, poly->x[i], poly->z[i], &sx[i], &sy[i]);
            if (!minimap_overlay_finite2(sx[i], sy[i])) {
                break;
            }
        }
        if (i != poly->point_count) {
            continue;
        }

        focus_alpha = current_room ? 1.0f : minimap_overlay_poly_focus_alpha(layout, poly);
        if (current_room) {
            r = 0.06f;
            g = 0.74f;
            b = 0.68f;
            a = 0.42f * opacity;
        } else if (fabsf(poly->y_avg - frame->player_y) < 220.0f) {
            r = 0.58f;
            g = 0.70f;
            b = 0.72f;
            a = 0.20f * focus_alpha * opacity;
        } else {
            r = 0.28f;
            g = 0.34f;
            b = 0.36f;
            a = 0.105f * focus_alpha * opacity;
        }

        for (i = 1; i + 1 < poly->point_count; i++) {
            minimap_overlay_push_tri(sx[0], sy[0], sx[i], sy[i], sx[i + 1], sy[i + 1], r, g, b, a);
        }
    }
}

static void minimap_overlay_draw_objective_room_highlights(const MinimapLevelCache *cache,
                                                          const MinimapFrame *frame,
                                                          const MinimapOverlayLayout *layout,
                                                          float opacity)
{
    s16 rooms[MINIMAP_MAX_OBJECTIVE_PINS];
    u32 room_count = 0;
    u32 pin_index;
    u32 room_index;

    for (pin_index = 0; pin_index < frame->objective_count && pin_index < MINIMAP_MAX_OBJECTIVE_PINS; pin_index++) {
        const MinimapPin *pin = &frame->objectives[pin_index];
        u32 i;

        if (pin->kind != MINIMAP_PIN_OBJECTIVE
            || !(pin->flags & MINIMAP_PIN_FLAG_ROOM_TARGET)
            || pin->room < 0) {
            continue;
        }

        for (i = 0; i < room_count; i++) {
            if (rooms[i] == pin->room) {
                break;
            }
        }
        if (i == room_count && room_count < MINIMAP_MAX_OBJECTIVE_PINS) {
            rooms[room_count++] = pin->room;
        }
    }

    for (room_index = 0; room_index < room_count; room_index++) {
        s16 room = rooms[room_index];
        u32 poly_index;

        for (poly_index = 0; poly_index < cache->poly_count; poly_index++) {
            const MinimapPoly *poly = &cache->polys[poly_index];
            float sx[MINIMAP_MAX_POLY_POINTS];
            float sy[MINIMAP_MAX_POLY_POINTS];
            int i;

            if (poly->room != (u8)room
                || !minimap_overlay_poly_visible(layout, poly)
                || fabsf(poly->y_avg - frame->player_y) > MINIMAP_OVERLAY_FLOOR_WINDOW_Y
                || poly->point_count < 3
                || poly->point_count > MINIMAP_MAX_POLY_POINTS) {
                continue;
            }

            for (i = 0; i < poly->point_count; i++) {
                minimap_overlay_project(layout, poly->x[i], poly->z[i], &sx[i], &sy[i]);
                if (!minimap_overlay_finite2(sx[i], sy[i])) {
                    break;
                }
            }
            if (i != poly->point_count) {
                continue;
            }

            for (i = 1; i + 1 < poly->point_count; i++) {
                minimap_overlay_push_tri(sx[0],
                                         sy[0],
                                         sx[i],
                                         sy[i],
                                         sx[i + 1],
                                         sy[i + 1],
                                         1.0f,
                                         0.76f,
                                         0.22f,
                                         0.18f * opacity);
            }
        }
    }
}

static void minimap_overlay_draw_current_tile(const MinimapLevelCache *cache,
                                             const MinimapFrame *frame,
                                             const MinimapOverlayLayout *layout,
                                             float opacity)
{
    u32 poly_index;
    float thickness = minimap_overlay_clampf(layout->w * 0.010f, 1.0f, 2.1f);

    if (!frame->player_tile_valid) {
        return;
    }

    for (poly_index = 0; poly_index < cache->poly_count; poly_index++) {
        const MinimapPoly *poly = &cache->polys[poly_index];
        float sx[MINIMAP_MAX_POLY_POINTS];
        float sy[MINIMAP_MAX_POLY_POINTS];
        int i;

        if (poly->room != (u8)frame->player_room
            || poly->tile_id != frame->player_tile_id
            || !minimap_overlay_poly_visible(layout, poly)
            || poly->point_count < 3
            || poly->point_count > MINIMAP_MAX_POLY_POINTS) {
            continue;
        }

        for (i = 0; i < poly->point_count; i++) {
            minimap_overlay_project(layout, poly->x[i], poly->z[i], &sx[i], &sy[i]);
            if (!minimap_overlay_finite2(sx[i], sy[i])) {
                break;
            }
        }
        if (i != poly->point_count) {
            continue;
        }

        for (i = 1; i + 1 < poly->point_count; i++) {
            minimap_overlay_push_tri(sx[0],
                                     sy[0],
                                     sx[i],
                                     sy[i],
                                     sx[i + 1],
                                     sy[i + 1],
                                     0.46f,
                                     1.0f,
                                     0.88f,
                                     0.16f * opacity);
        }

        for (i = 0; i < poly->point_count; i++) {
            int next = (i + 1) % poly->point_count;
            minimap_overlay_push_line(sx[i],
                                      sy[i],
                                      sx[next],
                                      sy[next],
                                      thickness,
                                      0.86f,
                                      1.0f,
                                      0.94f,
                                      0.42f * opacity);
        }
    }
}

static void minimap_overlay_draw_current_room_edges(const MinimapLevelCache *cache,
                                                   const MinimapFrame *frame,
                                                   const MinimapOverlayLayout *layout,
                                                   float opacity)
{
    u32 poly_index;
    float thickness = minimap_overlay_clampf(layout->w * 0.012f, 1.1f, 2.8f);

    for (poly_index = 0; poly_index < cache->poly_count; poly_index++) {
        const MinimapPoly *poly = &cache->polys[poly_index];
        float sx[MINIMAP_MAX_POLY_POINTS];
        float sy[MINIMAP_MAX_POLY_POINTS];
        int i;

        if (poly->room != (u8)frame->player_room
            || !minimap_overlay_poly_visible(layout, poly)
            || poly->point_count < 3
            || poly->point_count > MINIMAP_MAX_POLY_POINTS) {
            continue;
        }

        for (i = 0; i < poly->point_count; i++) {
            minimap_overlay_project(layout, poly->x[i], poly->z[i], &sx[i], &sy[i]);
            if (!minimap_overlay_finite2(sx[i], sy[i])) {
                break;
            }
        }
        if (i != poly->point_count) {
            continue;
        }

        for (i = 0; i < poly->point_count; i++) {
            int next = (i + 1) % poly->point_count;
            minimap_overlay_push_line(sx[i],
                                      sy[i],
                                      sx[next],
                                      sy[next],
                                      thickness,
                                      0.76f,
                                      1.0f,
                                      0.90f,
                                      0.62f * opacity);
        }
    }
}

static void minimap_overlay_draw_floor_edges(const MinimapLevelCache *cache,
                                             const MinimapFrame *frame,
                                             const MinimapOverlayLayout *layout,
                                             float opacity)
{
    u32 poly_index;
    float thickness = minimap_overlay_clampf(layout->w * 0.0065f, 0.75f, 1.65f);

    for (poly_index = 0; poly_index < cache->poly_count; poly_index++) {
        const MinimapPoly *poly = &cache->polys[poly_index];
        float sx[MINIMAP_MAX_POLY_POINTS];
        float sy[MINIMAP_MAX_POLY_POINTS];
        float focus_alpha;
        int i;

        if (poly->room == (u8)frame->player_room
            || !minimap_overlay_poly_visible(layout, poly)
            || fabsf(poly->y_avg - frame->player_y) > MINIMAP_OVERLAY_FLOOR_WINDOW_Y
            || poly->point_count < 3
            || poly->point_count > MINIMAP_MAX_POLY_POINTS) {
            continue;
        }

        for (i = 0; i < poly->point_count; i++) {
            minimap_overlay_project(layout, poly->x[i], poly->z[i], &sx[i], &sy[i]);
            if (!minimap_overlay_finite2(sx[i], sy[i])) {
                break;
            }
        }
        if (i != poly->point_count) {
            continue;
        }

        focus_alpha = minimap_overlay_poly_focus_alpha(layout, poly);
        for (i = 0; i < poly->point_count; i++) {
            int next = (i + 1) % poly->point_count;
            minimap_overlay_push_line(sx[i],
                                      sy[i],
                                      sx[next],
                                      sy[next],
                                      thickness,
                                      0.58f,
                                      0.76f,
                                      0.78f,
                                      0.25f * focus_alpha * opacity);
        }
    }
}

static void minimap_overlay_push_chevron(float tip_x,
                                         float tip_y,
                                         float dir_x,
                                         float dir_y,
                                         float size,
                                         float r,
                                         float g,
                                         float b,
                                         float a)
{
    float normal_x = -dir_y;
    float normal_y = dir_x;
    float base_x = tip_x - dir_x * size;
    float base_y = tip_y - dir_y * size;
    float side = size * 0.58f;

    minimap_overlay_push_tri(tip_x,
                             tip_y,
                             base_x + normal_x * side,
                             base_y + normal_y * side,
                             base_x - normal_x * side,
                             base_y - normal_y * side,
                             r,
                             g,
                             b,
                             a);
}

static void minimap_overlay_draw_objective_pins(const MinimapFrame *frame,
                                               const MinimapOverlayLayout *layout,
                                               float opacity)
{
    float offscreen_x[MINIMAP_MAX_OBJECTIVE_PINS];
    float offscreen_y[MINIMAP_MAX_OBJECTIVE_PINS];
    u8 offscreen_icon[MINIMAP_MAX_OBJECTIVE_PINS];
    u32 offscreen_count = 0;
    u32 i;

    for (i = 0; i < frame->objective_count && i < MINIMAP_MAX_OBJECTIVE_PINS; i++) {
        const MinimapPin *pin = &frame->objectives[i];
        float cx;
        float cy;
        float r;
        float g;
        float b;
        float radius;
        float alpha;

        if (pin->kind != MINIMAP_PIN_OBJECTIVE) {
            continue;
        }

        minimap_overlay_project(layout, pin->x, pin->z, &cx, &cy);
        if (!minimap_overlay_finite2(cx, cy)) {
            continue;
        }

        alpha = ((float)pin->alpha / 255.0f) * opacity;
        radius = minimap_overlay_clampf(layout->w * 0.039f, 4.2f, 10.0f);
        if (pin->status == OBJECTIVESTATUS_FAILED || (pin->flags & MINIMAP_PIN_FLAG_FAILED)) {
            r = 1.0f;
            g = 0.24f;
            b = 0.18f;
            alpha *= 0.86f;
        } else if (pin->flags & MINIMAP_PIN_FLAG_DEPOSIT_TARGET) {
            r = 0.98f;
            g = 0.78f;
            b = 0.24f;
        } else {
            r = 1.0f;
            g = 0.88f;
            b = 0.30f;
        }

        if (!minimap_overlay_point_in_content(layout, cx, cy)) {
            float edge_x;
            float edge_y;
            float dir_x;
            float dir_y;

            minimap_overlay_clamp_to_content_edge(layout, cx, cy, &edge_x, &edge_y, &dir_x, &dir_y);
            {
                u32 existing;
                int merged = 0;
                float merge_dist = radius * 5.2f;

                for (existing = 0; existing < offscreen_count; existing++) {
                    float dx = offscreen_x[existing] - edge_x;
                    float dy = offscreen_y[existing] - edge_y;

                    if (offscreen_icon[existing] == pin->icon
                        && (dx * dx + dy * dy) <= merge_dist * merge_dist) {
                        merged = 1;
                        break;
                    }
                }
                if (merged) {
                    continue;
                }
                if (offscreen_count < MINIMAP_MAX_OBJECTIVE_PINS) {
                    offscreen_x[offscreen_count] = edge_x;
                    offscreen_y[offscreen_count] = edge_y;
                    offscreen_icon[offscreen_count] = pin->icon;
                    offscreen_count++;
                }
            }
            minimap_overlay_push_chevron(edge_x,
                                         edge_y,
                                         dir_x,
                                         dir_y,
                                         radius * 1.74f,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         0.44f * alpha);
            minimap_overlay_push_chevron(edge_x,
                                         edge_y,
                                         dir_x,
                                         dir_y,
                                         radius * 1.28f,
                                         r,
                                         g,
                                         b,
                                         0.88f * alpha);
            continue;
        }

        minimap_overlay_push_diamond(cx, cy, radius * 1.52f, 0.0f, 0.0f, 0.0f, 0.48f * alpha);
        minimap_overlay_push_diamond(cx, cy, radius * 1.12f, r, g, b, 0.92f * alpha);
        minimap_overlay_push_diamond(cx, cy, radius * 0.46f, 0.08f, 0.07f, 0.03f, 0.68f * alpha);
    }
}

static void minimap_overlay_draw_player(const MinimapFrame *frame,
                                        const MinimapOverlayLayout *layout,
                                        float opacity)
{
    float cx;
    float cy;
    float theta = frame->player_theta_deg * (MINIMAP_OVERLAY_PI / 180.0f);
    float dir_x = layout->rotate_mode ? 0.0f : -sinf(theta);
    float dir_y = layout->rotate_mode ? -1.0f : -cosf(theta);
    float right_x = layout->rotate_mode ? 1.0f : cosf(theta);
    float right_y = layout->rotate_mode ? 0.0f : -sinf(theta);
    float radius = minimap_overlay_clampf(layout->w * 0.052f, 5.0f, 14.0f);
    float back = radius * 0.66f;
    float side = radius * 0.58f;

    minimap_overlay_project(layout, frame->player_x, frame->player_z, &cx, &cy);
    if (!minimap_overlay_finite2(cx, cy)) {
        return;
    }
    if (!minimap_overlay_point_in_panel(layout, cx, cy)) {
        return;
    }

    minimap_overlay_push_tri(cx + dir_x * radius * 1.28f,
                             cy + dir_y * radius * 1.28f,
                             cx - dir_x * back * 1.15f + right_x * side * 1.18f,
                             cy - dir_y * back * 1.15f + right_y * side * 1.18f,
                             cx - dir_x * back * 1.15f - right_x * side * 1.18f,
                             cy - dir_y * back * 1.15f - right_y * side * 1.18f,
                             0.0f,
                             0.0f,
                             0.0f,
                             0.62f * opacity);
    minimap_overlay_push_tri(cx + dir_x * radius,
                             cy + dir_y * radius,
                             cx - dir_x * back + right_x * side,
                             cy - dir_y * back + right_y * side,
                             cx - dir_x * back - right_x * side,
                             cy - dir_y * back - right_y * side,
                             1.0f,
                             0.84f,
                             0.24f,
                             1.0f * opacity);
    minimap_overlay_push_diamond(cx,
                                 cy,
                                 radius * 0.34f,
                                 0.02f,
                                 0.025f,
                                 0.018f,
                                 0.92f * opacity);
}

static void minimap_overlay_draw_enemy_pins(const MinimapFrame *frame,
                                            const MinimapOverlayLayout *layout,
                                            float opacity)
{
    u32 i;

    for (i = 0; i < frame->enemy_count && i < MINIMAP_MAX_ENEMY_PINS; i++) {
        const MinimapPin *pin = &frame->enemies[i];
        float cx;
        float cy;
        float alpha;
        float radius;

        if (pin->kind != MINIMAP_PIN_ENEMY_FIRE) {
            continue;
        }

        minimap_overlay_project(layout, pin->x, pin->z, &cx, &cy);
        if (!minimap_overlay_finite2(cx, cy)) {
            continue;
        }
        if (!minimap_overlay_point_in_panel(layout, cx, cy)) {
            continue;
        }

        alpha = ((float)pin->alpha / 255.0f) * opacity;
        radius = minimap_overlay_clampf(layout->w * 0.034f, 4.0f, 10.0f);
        minimap_overlay_push_diamond(cx, cy, radius * 1.32f, 0.0f, 0.0f, 0.0f, 0.46f * alpha);
        minimap_overlay_push_diamond(cx, cy, radius, 1.0f, 0.15f, 0.11f, 0.84f * alpha);
        minimap_overlay_push_diamond(cx, cy, radius * 0.42f, 1.0f, 0.72f, 0.46f, 0.96f * alpha);
    }
}

static int minimap_overlay_draw_frame(const MinimapLevelCache *cache,
                                      const MinimapFrame *frame,
                                      int fb_width,
                                      int fb_height)
{
    MinimapOverlayLayout layout;
    float opacity;

    if (!frame->enabled || frame->view_width <= 0 || frame->view_height <= 0) {
        return 0;
    }

    opacity = minimap_overlay_clampf(g_pcMinimapOpacity, 0.0f, 1.0f);
    if (opacity <= 0.0f) {
        return 0;
    }

    if (!minimap_overlay_build_layout(cache, frame, fb_width, fb_height, &layout)) {
        return 0;
    }

    minimap_overlay_flush();
    minimap_overlay_disable_scissor();
    minimap_overlay_draw_shadow(&layout, opacity);
    minimap_overlay_set_panel_scissor(&layout, fb_width, fb_height);
    minimap_overlay_draw_panel(&layout, opacity);
    minimap_overlay_set_content_scissor(&layout, fb_width, fb_height);
    minimap_overlay_draw_polys(cache, frame, &layout, 0, opacity);
    minimap_overlay_draw_polys(cache, frame, &layout, 1, opacity);
    minimap_overlay_draw_objective_room_highlights(cache, frame, &layout, opacity);
    minimap_overlay_draw_current_tile(cache, frame, &layout, opacity);
    minimap_overlay_draw_floor_edges(cache, frame, &layout, opacity);
    minimap_overlay_draw_current_room_edges(cache, frame, &layout, opacity);
    minimap_overlay_draw_edge_mask(&layout, opacity);
    minimap_overlay_draw_objective_pins(frame, &layout, opacity);
    minimap_overlay_draw_enemy_pins(frame, &layout, opacity);
    minimap_overlay_draw_player(frame, &layout, opacity);
    minimap_overlay_flush();

    return 1;
}

static void minimap_overlay_save_gl_state(MinimapOverlayGlState *state)
{
    glGetIntegerv(GL_CURRENT_PROGRAM, &state->program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->array_buffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state->draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state->read_fbo);
    glGetIntegerv(GL_VIEWPORT, state->viewport);
    glGetIntegerv(GL_SCISSOR_BOX, state->scissor_box);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state->blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state->blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state->blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state->blend_dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &state->blend_eq_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state->blend_eq_alpha);
    state->blend = glIsEnabled(GL_BLEND);
    state->depth_test = glIsEnabled(GL_DEPTH_TEST);
    state->scissor = glIsEnabled(GL_SCISSOR_TEST);
    state->cull_face = glIsEnabled(GL_CULL_FACE);
    state->sample_alpha_to_coverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state->depth_mask);
}

static void minimap_overlay_restore_gl_state(const MinimapOverlayGlState *state)
{
    if (state->blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (state->depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (state->scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (state->cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (state->sample_alpha_to_coverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    glDepthMask(state->depth_mask);
    glBlendEquationSeparate((GLenum)state->blend_eq_rgb, (GLenum)state->blend_eq_alpha);
    glBlendFuncSeparate((GLenum)state->blend_src_rgb,
                        (GLenum)state->blend_dst_rgb,
                        (GLenum)state->blend_src_alpha,
                        (GLenum)state->blend_dst_alpha);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)state->read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)state->draw_fbo);
    glViewport(state->viewport[0], state->viewport[1], state->viewport[2], state->viewport[3]);
    glScissor(state->scissor_box[0],
              state->scissor_box[1],
              state->scissor_box[2],
              state->scissor_box[3]);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state->array_buffer);
    glBindVertexArray((GLuint)state->vao);
    glUseProgram((GLuint)state->program);
}

void minimap_overlay_draw_queued_frames(void)
{
    const MinimapLevelCache *cache = minimap_get_level_cache();
    const MinimapFrameQueue *queue = minimap_get_frame_queue();
    MinimapOverlayTraceSummary trace_summary;
    MinimapOverlayGlState state;
    GLint viewport[4] = {0, 0, 0, 0};
    int fb_width;
    int fb_height;
    u32 i;
    extern SDL_Window *g_sdlWindow;

    memset(&trace_summary, 0, sizeof(trace_summary));
    trace_summary.status = "no_queue";
    trace_summary.cache_ready = (cache != NULL && cache->ready) ? 1 : 0;
    trace_summary.cache_poly_count = cache != NULL ? cache->poly_count : 0;
    trace_summary.cache_overflow_count = cache != NULL ? cache->overflow_count : 0;
    s_minimap_trace_draw_calls = 0;
    s_minimap_trace_vertices_flushed = 0;
    s_minimap_overlay_metal_backend = 0;
    s_minimap_overlay_submit_failed = 0;

    if (queue == NULL || queue->count == 0) {
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }
    trace_summary.queued_frames = queue->count;
    for (i = 0; i < queue->count; i++) {
        trace_summary.objective_pins += queue->frames[i].objective_count;
        trace_summary.enemy_pins += queue->frames[i].enemy_count;
    }

    if (!minimap_is_enabled() || cache == NULL || !cache->ready) {
        minimap_clear_frame_queue();
        trace_summary.status = "disabled_or_not_ready";
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }

    if (g_sdlWindow != NULL) {
        SDL_GL_GetDrawableSize(g_sdlWindow, &fb_width, &fb_height);
    } else {
        fb_width = 0;
        fb_height = 0;
    }

    glGetIntegerv(GL_VIEWPORT, viewport);
    if (fb_width <= 0) {
        fb_width = viewport[2] > 0 ? viewport[2] : (int)gfx_current_dimensions.width;
    }
    if (fb_height <= 0) {
        fb_height = viewport[3] > 0 ? viewport[3] : (int)gfx_current_dimensions.height;
    }

    if (fb_width <= 0 || fb_height <= 0) {
        minimap_clear_frame_queue();
        trace_summary.status = "invalid_framebuffer";
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }
    trace_summary.fb_width = fb_width;
    trace_summary.fb_height = fb_height;

    minimap_overlay_save_gl_state(&state);
    if (!minimap_overlay_ensure_program()) {
        minimap_overlay_restore_gl_state(&state);
        minimap_clear_frame_queue();
        trace_summary.status = "shader_failed";
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb_width, fb_height);
    glDisable(GL_DEPTH_TEST);
    minimap_overlay_disable_scissor();
    glDisable(GL_CULL_FACE);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(s_minimap_program);
    glUniform2f(s_minimap_screen_location, (float)fb_width, (float)fb_height);

    for (i = 0; i < queue->count; i++) {
        if (minimap_overlay_draw_frame(cache, &queue->frames[i], fb_width, fb_height)) {
            trace_summary.drawn_frames++;
        } else {
            trace_summary.layout_failures++;
        }
    }

    minimap_overlay_flush();
    minimap_overlay_restore_gl_state(&state);
    minimap_clear_frame_queue();
    trace_summary.status = trace_summary.drawn_frames > 0 ? "drawn" : "no_drawn_frames";
    trace_summary.draw_calls = s_minimap_trace_draw_calls;
    trace_summary.vertices_flushed = s_minimap_trace_vertices_flushed;
    minimap_overlay_dump_summary(&trace_summary);
}

#ifdef __APPLE__
void minimap_overlay_draw_queued_frames_metal(int fb_width, int fb_height)
{
    const MinimapLevelCache *cache = minimap_get_level_cache();
    const MinimapFrameQueue *queue = minimap_get_frame_queue();
    MinimapOverlayTraceSummary trace_summary;
    u32 i;

    memset(&trace_summary, 0, sizeof(trace_summary));
    trace_summary.status = "no_queue";
    trace_summary.cache_ready = (cache != NULL && cache->ready) ? 1 : 0;
    trace_summary.cache_poly_count = cache != NULL ? cache->poly_count : 0;
    trace_summary.cache_overflow_count = cache != NULL ? cache->overflow_count : 0;
    s_minimap_trace_draw_calls = 0;
    s_minimap_trace_vertices_flushed = 0;
    s_minimap_overlay_metal_backend = 0;
    s_minimap_overlay_submit_failed = 0;

    if (queue == NULL || queue->count == 0) {
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }
    trace_summary.queued_frames = queue->count;
    for (i = 0; i < queue->count; i++) {
        trace_summary.objective_pins += queue->frames[i].objective_count;
        trace_summary.enemy_pins += queue->frames[i].enemy_count;
    }

    if (!minimap_is_enabled() || cache == NULL || !cache->ready) {
        minimap_clear_frame_queue();
        trace_summary.status = "disabled_or_not_ready";
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }

    if (fb_width <= 0) {
        fb_width = (int)gfx_current_dimensions.width;
    }
    if (fb_height <= 0) {
        fb_height = (int)gfx_current_dimensions.height;
    }
    if (fb_width <= 0 || fb_height <= 0) {
        minimap_clear_frame_queue();
        trace_summary.status = "invalid_framebuffer";
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }
    trace_summary.fb_width = fb_width;
    trace_summary.fb_height = fb_height;

    s_minimap_overlay_metal_backend = 1;
    s_minimap_overlay_fb_width = fb_width;
    s_minimap_overlay_fb_height = fb_height;
    s_minimap_overlay_scissor_enabled = 0;
    s_minimap_overlay_scissor_x = 0;
    s_minimap_overlay_scissor_y = 0;
    s_minimap_overlay_scissor_w = fb_width;
    s_minimap_overlay_scissor_h = fb_height;

    for (i = 0; i < queue->count; i++) {
        if (minimap_overlay_draw_frame(cache, &queue->frames[i], fb_width, fb_height)) {
            trace_summary.drawn_frames++;
        } else {
            trace_summary.layout_failures++;
        }
    }

    minimap_overlay_flush();
    s_minimap_overlay_metal_backend = 0;
    s_minimap_overlay_scissor_enabled = 0;
    minimap_clear_frame_queue();
    trace_summary.status = s_minimap_overlay_submit_failed
                               ? "submit_failed"
                               : (trace_summary.drawn_frames > 0 ? "drawn" : "no_drawn_frames");
    trace_summary.draw_calls = s_minimap_trace_draw_calls;
    trace_summary.vertices_flushed = s_minimap_trace_vertices_flushed;
    minimap_overlay_dump_summary(&trace_summary);
}
#endif

#ifdef MGB64_WEBGPU_BACKEND
/* WebGPU minimap draw entry — mirrors the Metal path: build the overlay
 * geometry (shared with the GL path) then flush it through the WebGPU backend's
 * gfx_webgpu_draw_minimap_overlay hook (a 2D screen-space pass into the scene
 * target). Called from wgpu_end_frame with the scene dimensions. */
void minimap_overlay_draw_queued_frames_webgpu(int fb_width, int fb_height)
{
    const MinimapLevelCache *cache = minimap_get_level_cache();
    const MinimapFrameQueue *queue = minimap_get_frame_queue();
    MinimapOverlayTraceSummary trace_summary;
    u32 i;

    memset(&trace_summary, 0, sizeof(trace_summary));
    trace_summary.status = "no_queue";
    trace_summary.cache_ready = (cache != NULL && cache->ready) ? 1 : 0;
    trace_summary.cache_poly_count = cache != NULL ? cache->poly_count : 0;
    trace_summary.cache_overflow_count = cache != NULL ? cache->overflow_count : 0;
    s_minimap_trace_draw_calls = 0;
    s_minimap_trace_vertices_flushed = 0;
    s_minimap_overlay_webgpu_backend = 0;
    s_minimap_overlay_submit_failed = 0;

    if (queue == NULL || queue->count == 0) {
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }
    trace_summary.queued_frames = queue->count;
    for (i = 0; i < queue->count; i++) {
        trace_summary.objective_pins += queue->frames[i].objective_count;
        trace_summary.enemy_pins += queue->frames[i].enemy_count;
    }

    if (!minimap_is_enabled() || cache == NULL || !cache->ready) {
        minimap_clear_frame_queue();
        trace_summary.status = "disabled_or_not_ready";
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }

    if (fb_width <= 0) {
        fb_width = (int)gfx_current_dimensions.width;
    }
    if (fb_height <= 0) {
        fb_height = (int)gfx_current_dimensions.height;
    }
    if (fb_width <= 0 || fb_height <= 0) {
        minimap_clear_frame_queue();
        trace_summary.status = "invalid_framebuffer";
        minimap_overlay_dump_summary(&trace_summary);
        return;
    }
    trace_summary.fb_width = fb_width;
    trace_summary.fb_height = fb_height;

    s_minimap_overlay_webgpu_backend = 1;
    s_minimap_overlay_fb_width = fb_width;
    s_minimap_overlay_fb_height = fb_height;
    s_minimap_overlay_scissor_enabled = 0;
    s_minimap_overlay_scissor_x = 0;
    s_minimap_overlay_scissor_y = 0;
    s_minimap_overlay_scissor_w = fb_width;
    s_minimap_overlay_scissor_h = fb_height;

    for (i = 0; i < queue->count; i++) {
        if (minimap_overlay_draw_frame(cache, &queue->frames[i], fb_width, fb_height)) {
            trace_summary.drawn_frames++;
        } else {
            trace_summary.layout_failures++;
        }
    }

    minimap_overlay_flush();
    s_minimap_overlay_webgpu_backend = 0;
    s_minimap_overlay_scissor_enabled = 0;
    minimap_clear_frame_queue();
    trace_summary.status = s_minimap_overlay_submit_failed
                               ? "submit_failed"
                               : (trace_summary.drawn_frames > 0 ? "drawn" : "no_drawn_frames");
    trace_summary.draw_calls = s_minimap_trace_draw_calls;
    trace_summary.vertices_flushed = s_minimap_trace_vertices_flushed;
    minimap_overlay_dump_summary(&trace_summary);
}
#endif
