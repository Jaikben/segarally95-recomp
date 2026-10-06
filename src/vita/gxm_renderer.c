#include "gxm_renderer.h"
#include "menu.h"
#include "startup_log.h"
#include "bitmap_font.h"
#include "gxm_math.h"
#include "model2_geo_order.h"
#include "model2_geo_tex.h"
#include "model2_host_aspect.h"
#include "sys24_tile.h"

#include <stdio.h>
#include <stdlib.h>
#include <vita2d.h>
#if defined(I960_HOST_VITA_GXM)
#include <psp2/kernel/processmgr.h>
static uint64_t g_performance_time;
static unsigned g_performance_frame;
#endif

enum {
    SOURCE_CAP = 256,
    MATERIAL_CAP = 2048,
    SOURCE_BUDGET = 32 * 1024 * 1024,
    POOL_BYTES = 16 * 1024 * 1024
};

typedef struct {
    u16 x, y, w, h;
    u8 sheet;
    vita2d_texture *texture;
} source_t;

typedef struct {
    model2_geo_tri_mat_t key;
    vita2d_texture view;
} material_t;

static source_t g_sources[SOURCE_CAP];
static unsigned g_source_count, g_source_bytes, g_sheet_gen;
static material_t g_materials[MATERIAL_CAP];
static unsigned g_material_count;
static vita2d_texture *g_bottom, *g_priority;
static vita2d_texture *g_font;
static unsigned *g_order, g_order_cap;
static const model2_geo_tri_mat_t *g_sort_mats;
static int g_initialized;
static unsigned g_diagnostic_frames;
static int g_diagnostic_due;
static unsigned g_diagnostic_triangles;
static const char *g_diagnostic_geometry;

static void clear_sources(void)
{
    unsigned i;
    for (i = 0; i < g_source_count; i++)
        vita2d_free_texture(g_sources[i].texture);
    g_source_count = g_source_bytes = 0;
}

static int fail(const char *what)
{
    char message[192];
    snprintf(message, sizeof(message), "runtime: native GXM error: %s\n", what);
    vita_startup_log(message);
    fprintf(stderr, "lift: native GXM: %s\n", what);
    return -1;
}

static void *geometry_alloc(unsigned size, unsigned alignment)
{
    /* Keep space for priority tiles and the pause/error overlay after geometry. */
    if (vita2d_pool_free_space() < size + 64u * 1024u)
        return NULL;
    return vita2d_pool_memalign(size, alignment);
}

static int create_font(void)
{
    unsigned stride, c, x, y;
    u32 *pixels;
    g_font = vita2d_create_empty_texture(VITA_FONT_WIDTH, VITA_FONT_HEIGHT);
    if (!g_font)
        return fail("bitmap font texture allocation failed");
    pixels = vita2d_texture_get_datap(g_font);
    stride = vita2d_texture_get_stride(g_font) / sizeof(u32);
    if (!pixels || stride < VITA_FONT_WIDTH)
        return fail("bitmap font texture storage unavailable");
    for (y = 0; y < VITA_FONT_HEIGHT; ++y)
        for (x = 0; x < stride; ++x)
            pixels[y * stride + x] = 0;
    for (c = 32; c <= 126; ++c) {
        unsigned left = ((c - 32) % 16) * 8;
        unsigned top = ((c - 32) / 16) * 8;
        for (y = 0; y < 7; ++y)
            for (x = 0; x < 5; ++x)
                if (vita_font_row(c, y) & (1u << (4 - x)))
                    pixels[(top + y) * stride + left + x] = RGBA8(255, 255, 255, 255);
    }
    vita2d_texture_set_filters(g_font, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    return 0;
}

static void draw_text(int left, int baseline, unsigned color, float scale, const char *text)
{
    float size = scale * 2.0f;
    float x = (float)left, y = baseline - 7 * size;
    const unsigned char *p = (const unsigned char *)text;
    for (; *p; ++p) {
        unsigned c = vita_font_character(*p) - 32;
        if (*p == '\n' || x + 5 * size > 930) {
            x = (float)left;
            y += 9 * size;
            if (*p == '\n')
                continue;
        }
        if (y + 7 * size > 544) {
            fprintf(stderr, "lift: bitmap UI text exceeds display height\n");
            break;
        }
        if (*p != ' ')
            vita2d_draw_texture_tint_part_scale(g_font, x, y,
                (float)((c % 16) * 8), (float)((c / 16) * 8), 5, 7, size, size, color);
        x += 6 * size;
    }
}

int vita_gxm_open(void)
{
    if (g_initialized)
        return 0;
    vita_startup_log("startup: libvita2d/GXM initialization begin\n");
    if (vita2d_init_advanced(POOL_BYTES) <= 0)
        return fail("libvita2d initialization failed");
    vita_startup_log("startup: libvita2d/GXM ready; tile texture allocation begin\n");
    g_initialized = 1;
    vita2d_set_vblank_wait(0);
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
    g_bottom = vita2d_create_empty_texture(SYS24_FB_WIDTH, SYS24_FB_HEIGHT);
    g_priority = vita2d_create_empty_texture(SYS24_FB_WIDTH, SYS24_FB_HEIGHT);
    vita_startup_log("startup: tile textures allocated; built-in bitmap font creation begin\n");
    if (!g_bottom || !g_priority) {
        vita_gxm_shutdown();
        return fail("tile texture allocation failed");
    }
    if (create_font() != 0) {
        vita_gxm_shutdown();
        return -1;
    }
    vita2d_texture_set_filters(g_bottom, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    vita2d_texture_set_filters(g_priority, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    vita_startup_log("startup: GXM textures and built-in bitmap font ready\n");
    return 0;
}

static vita2d_texture *source_for(const model2_geo_tri_mat_t *m)
{
    unsigned i, y, x;
    unsigned w = m->patch_w ? m->patch_w : 32;
    unsigned h = m->patch_h ? m->patch_h : 32;
    const u32 *sheet;
    source_t *s;
    for (i = 0; i < g_source_count; i++) {
        s = &g_sources[i];
        if (s->x == m->patch_x && s->y == m->patch_y && s->w == w
            && s->h == h && s->sheet == m->sheet)
            return s->texture;
    }
    if (g_source_count == SOURCE_CAP || w > 4096 || h > 4096
        || w * h + 8192u > SOURCE_BUDGET - g_source_bytes) {
        fail("texture cache budget exhausted (no geometry silently dropped)");
        return NULL;
    }
    sheet = model2_tex_sheet_bank(m->sheet);
    if (!sheet) {
        fail("texture sheet unavailable");
        return NULL;
    }
    s = &g_sources[g_source_count];
    s->texture = vita2d_create_empty_texture_format(w, h, SCE_GXM_TEXTURE_FORMAT_P8_ABGR);
    if (!s->texture) {
        fail("indexed patch allocation failed");
        return NULL;
    }
    s->x = m->patch_x; s->y = m->patch_y;
    s->w = (u16)w; s->h = (u16)h; s->sheet = m->sheet;
    vita2d_texture_set_filters(s->texture, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    for (y = 0; y < h; y++) {
        u8 *row = (u8 *)vita2d_texture_get_datap(s->texture)
                  + y * vita2d_texture_get_stride(s->texture);
        for (x = 0; x < w; x++) {
            /* Decode the same logical sheet coordinates as the GL R8 atlas. */
            row[x] = (u8)model2_get_texel(sheet, 0, 0,
                         (int)((m->patch_x + x) & 2047u),
                         (int)((m->patch_y + y) & 1023u));
        }
    }
    g_source_bytes += vita2d_texture_get_stride(s->texture) * h + 8192u;
    g_source_count++;
    return s->texture;
}

static int same_material(const model2_geo_tri_mat_t *a, const model2_geo_tri_mat_t *b)
{
    return a->colorbase == b->colorbase && a->lumabase == b->lumabase
        && a->luma == b->luma && a->flags == b->flags && a->sheet == b->sheet
        && a->patch_x == b->patch_x && a->patch_y == b->patch_y
        && a->patch_w == b->patch_w && a->patch_h == b->patch_h;
}

static const vita2d_texture *material_for(const model2_geo_tri_mat_t *m)
{
    unsigned i;
    u8 rgba[64];
    u32 *palette;
    vita2d_texture *source;
    material_t *out;
    for (i = 0; i < g_material_count; i++)
        if (same_material(m, &g_materials[i].key))
            return &g_materials[i].view;
    if (g_material_count == MATERIAL_CAP) {
        fail("per-frame material budget exhausted");
        return NULL;
    }
    source = source_for(m);
    if (!source)
        return NULL;
    palette = geometry_alloc(1024, 64);
    if (!palette) {
        fail("GPU palette pool exhausted");
        return NULL;
    }
    memset(palette, 0, 1024);
    model2_palette_build_texel_lut(m->colorbase, m->lumabase, m->luma,
                                   (m->flags & MODEL2_GEO_TEX_CUTOUT) != 0, rgba);
    for (i = 0; i < 16; i++)
        palette[i] = RGBA8(rgba[4*i], rgba[4*i+1], rgba[4*i+2], rgba[4*i+3]);
    out = &g_materials[g_material_count];
    out->key = *m;
    out->view = *source;
    if (sceGxmTextureSetPalette(&out->view.gxm_tex, palette) < 0
        || sceGxmTextureSetUAddrMode(&out->view.gxm_tex,
            (m->flags & MODEL2_GEO_TEX_MIRROR_X) ? SCE_GXM_TEXTURE_ADDR_MIRROR : SCE_GXM_TEXTURE_ADDR_REPEAT) < 0
        || sceGxmTextureSetVAddrMode(&out->view.gxm_tex,
            (m->flags & MODEL2_GEO_TEX_MIRROR_Y) ? SCE_GXM_TEXTURE_ADDR_MIRROR : SCE_GXM_TEXTURE_ADDR_REPEAT) < 0) {
        fail("GXM texture addressing or palette binding failed");
        return NULL;
    }
    g_material_count++;
    return &out->view;
}

static int painter_compare(const void *va, const void *vb)
{
    unsigned a = *(const unsigned *)va, b = *(const unsigned *)vb;
    if ((g_sort_mats[a].pad & 1u) != (g_sort_mats[b].pad & 1u))
        return (g_sort_mats[a].pad & 1u) ? 1 : -1;
    /* Alpha cutouts use painter ordering: reverse the hardware near-first list. */
    return -model2_geo_depth_compare(&g_sort_mats[a], a, &g_sort_mats[b], b);
}

static vita2d_texture_vertex gpu_vertex(vita_screen_vertex_t v, float sx, float sy,
                                       float ox, float oy, unsigned w, unsigned h)
{
    return (vita2d_texture_vertex){
        ox + v.x * sx, oy + v.y * sy, 0.5f,
        v.uq / (v.q * (float)w), v.vq / (v.q * (float)h)
    };
}

static int draw_triangle(const vita_screen_vertex_t v[3], const model2_geo_tri_mat_t *m,
                          float sx, float sy, float ox, float oy)
{
    unsigned i, j, count = 0;
    unsigned alpha = (m->flags & MODEL2_GEO_TEX_CHECKER) ? 128 : 255;
    if (m->flags & MODEL2_GEO_TEX_TEXTURED) {
        const unsigned subdivisions = v[0].q == v[1].q && v[1].q == v[2].q ? 1u : 4u;
        const vita2d_texture *texture = material_for(m);
        vita2d_texture_vertex *vertices;
        unsigned w = m->patch_w ? m->patch_w : 32;
        unsigned h = m->patch_h ? m->patch_h : 32;
        if (!texture)
            return -1;
        vertices = geometry_alloc(3 * subdivisions * subdivisions * sizeof(*vertices), 4);
        if (!vertices)
            return fail("GPU vertex pool exhausted");
        for (i = 0; i < subdivisions; i++) {
            for (j = 0; j < subdivisions - i; j++) {
                float b = (float)i / subdivisions, c = (float)j / subdivisions;
                float step = 1.0f / subdivisions;
                vita_screen_vertex_t a = vita_barycentric(v, b, c);
                vita_screen_vertex_t bb = vita_barycentric(v, b + step, c);
                vita_screen_vertex_t cc = vita_barycentric(v, b, c + step);
                vertices[count++] = gpu_vertex(a, sx, sy, ox, oy, w, h);
                vertices[count++] = gpu_vertex(bb, sx, sy, ox, oy, w, h);
                vertices[count++] = gpu_vertex(cc, sx, sy, ox, oy, w, h);
                if (j + i + 1 < subdivisions) {
                    vita_screen_vertex_t d = vita_barycentric(v, b + step, c + step);
                    vertices[count++] = gpu_vertex(bb, sx, sy, ox, oy, w, h);
                    vertices[count++] = gpu_vertex(d, sx, sy, ox, oy, w, h);
                    vertices[count++] = gpu_vertex(cc, sx, sy, ox, oy, w, h);
                }
            }
        }
        vita2d_draw_array_textured(texture, SCE_GXM_PRIMITIVE_TRIANGLES,
                                   vertices, count, RGBA8(255, 255, 255, alpha));
    } else {
        u8 rgb[3];
        vita2d_color_vertex *vertices = geometry_alloc(3 * sizeof(*vertices), 4);
        if (!vertices)
            return fail("GPU solid vertex pool exhausted");
        model2_palette_lookup_solid(m->colorbase, m->luma, rgb);
        for (i = 0; i < 3; i++)
            vertices[i] = (vita2d_color_vertex){
                ox + v[i].x * sx, oy + v[i].y * sy, 0.5f,
                RGBA8(rgb[0], rgb[1], rgb[2], alpha)
            };
        vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, vertices, 3);
    }
    return 0;
}

static int draw_geometry(float sx, float sy, float ox, float oy)
{
    const float *xyzuv;
    const model2_geo_tri_mat_t *mats;
    model2_geo_projection_t p;
    unsigned nverts, ntris, t, i;
    int result = 0;
    g_diagnostic_geometry = "no projection";
    if (!model2_geo_projection(&p))
        return 0; /* No hardware projection latched yet. */
    sx *= (float)SYS24_FB_WIDTH / (float)(p.viewport[2] - p.viewport[0]);
    sy *= (float)SYS24_FB_HEIGHT / (float)(p.viewport[3] - p.viewport[1]);
    ox -= (float)p.viewport[0] * sx;
    oy -= (float)p.viewport[1] * sy;
    g_diagnostic_geometry = "empty mesh";
    if (model2_geo_lock_textured(&xyzuv, &nverts, &mats, &ntris) != 0)
        return 0; /* The snapshot API reports an empty mesh as -1. */
    g_diagnostic_geometry = "mesh available";
    g_diagnostic_triangles = ntris;
    if (ntris > nverts / 3 || (ntris && (!xyzuv || !mats))) {
        model2_geo_unlock();
        return fail("invalid expanded geometry snapshot");
    }
    if (ntris > g_order_cap) {
        unsigned *order = realloc(g_order, (size_t)ntris * sizeof(*order));
        if (!order) {
            model2_geo_unlock();
            return fail("polygon order allocation failed");
        }
        g_order = order;
        g_order_cap = ntris;
    }
    g_sort_mats = mats;
    for (t = 0; t < ntris; t++)
        g_order[t] = t;
    if (ntris)
        qsort(g_order, ntris, sizeof(*g_order), painter_compare);
    for (t = 0; t < ntris && result == 0; t++) {
        unsigned tri = g_order[t], n;
        vita_camera_vertex_t in[3], near[4];
        vita_screen_vertex_t screen[12];
        for (i = 0; i < 3; i++) {
            const float *v = xyzuv + (tri * 3 + i) * 5;
            in[i] = (vita_camera_vertex_t){v[0], v[1], v[2], v[3], v[4]};
            if (!isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2])
                || !isfinite(v[3]) || !isfinite(v[4])) {
                result = fail("non-finite polygon coordinates");
                break;
            }
        }
        if (result != 0)
            break;
        n = vita_clip_near(in, near);
        for (i = 0; i < n; i++)
            screen[i] = vita_project(near[i], &p);
        n = vita_clip_screen(screen, n, &p);
        for (i = 1; i + 1 < n && result == 0; i++) {
            vita_screen_vertex_t fan[3] = {screen[0], screen[i], screen[i+1]};
            result = draw_triangle(fan, &mats[tri], sx, sy, ox, oy);
        }
    }
    model2_geo_unlock();
    return result;
}

static void upload_tiles(vita2d_texture *texture, const u32 *pixels, int opaque)
{
    unsigned y, x;
    unsigned stride = vita2d_texture_get_stride(texture);
    u8 *base = vita2d_texture_get_datap(texture);
    for (y = 0; y < SYS24_FB_HEIGHT; y++) {
        u32 *row = (u32 *)(base + y * stride);
        for (x = 0; x < SYS24_FB_WIDTH; x++) {
            u32 p = pixels[y * SYS24_FB_WIDTH + x];
            row[x] = !opaque && !(p & 0xffffffu) ? 0 : vita_swap_rb(p | 0xff000000u);
        }
    }
}

static void draw_menu(int have_game)
{
    const unsigned white = RGBA8(235, 235, 235, 255);
    const unsigned yellow = RGBA8(255, 200, 70, 255);
    int i, count = g_vita_menu.options ? VITA_OPTION_COUNT : 4;
    char label[96], line[128];
    vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(12, 16, 24, 255));
    draw_text(30, 40, white, 1.5f,
        g_vita_menu.options ? "SEGA RALLY RECOMP - OPTIONS" : "SEGA RALLY RECOMP - PS VITA");
    if (!g_vita_menu.options) {
        snprintf(line, sizeof(line), "CPU %d MHz   GPU %d MHz   NATIVE GXM (EXPERIMENTAL)",
                 g_vita_menu.actual_cpu, g_vita_menu.actual_gpu);
        draw_text(30, 80, white, 1.0f, line);
    }
    for (i = 0; i < count; ++i) {
        vita_menu_label(&g_vita_menu, i, have_game, label, sizeof(label));
        snprintf(line, sizeof(line), "%s%s", i == g_vita_menu.selection ? "> " : "  ", label);
        draw_text(42, g_vita_menu.options ? 78 + i * 28 : 145 + i * 44,
            i == g_vita_menu.selection ? yellow : white, 1.1f, line);
    }
    draw_text(30, g_vita_menu.options ? 486 : 360, white,
                        0.85f, g_vita_menu.status);
    draw_text(30, 526, white, 0.9f, g_vita_menu.options
        ? "UP/DOWN MOVE   LEFT/RIGHT CHANGE   CROSS SELECT   CIRCLE BACK"
        : "UP/DOWN MOVE   CROSS SELECT   CIRCLE RESUME   START+SELECT MENU");
}

int vita_gxm_present(const u32 *bottom, const u32 *priority, int opaque,
                     int tiles_dirty, int paused)
{
    float hud_scale = 544.0f / SYS24_FB_HEIGHT;
    float hud_x = (960.0f - SYS24_FB_WIDTH * hud_scale) * 0.5f;
    float geo_w = model2_host_aspect_is_widescreen() ? 960.0f : SYS24_FB_WIDTH * hud_scale;
    int result = 0;
#if defined(I960_HOST_VITA_GXM)
    uint64_t frame_started = sceKernelGetProcessTimeWide();
#endif
    if (!g_initialized || !bottom || !priority)
        return fail("present called without initialized tile buffers");
    if (paused) {
#if defined(I960_HOST_VITA_GXM)
        g_performance_time = 0;
#endif
        vita_gxm_menu(1);
        return 0;
    }
    g_diagnostic_frames++;
#if defined(I960_HOST_VITA_GXM)
    if (!g_performance_time) {
        g_performance_time = frame_started;
        g_performance_frame = g_diagnostic_frames - 1u;
    }
#endif
    g_diagnostic_due = g_diagnostic_frames <= 3u
        || (g_diagnostic_frames <= 1800u && g_diagnostic_frames % 120u == 0)
        || g_diagnostic_frames % 300u == 0;
    g_diagnostic_triangles = 0;
    g_diagnostic_geometry = opaque ? "tiles only" : "not started";
    if (g_diagnostic_due) {
        char message[192];
        unsigned i, bottom_pixels = 0, priority_pixels = 0;
        for (i = 0; i < SYS24_FB_WIDTH * SYS24_FB_HEIGHT; i++) {
            bottom_pixels += (bottom[i] & 0xffffffu) != 0;
            priority_pixels += !opaque && (priority[i] & 0xffffffu) != 0;
        }
        snprintf(message, sizeof(message),
                 "runtime: GXM frame=%u begin opaque=%d dirty=%d bottom_pixels=%u priority_pixels=%u\n",
                 g_diagnostic_frames, opaque, tiles_dirty, bottom_pixels, priority_pixels);
        vita_startup_log(message);
    }
    /* Triple buffering does not protect the shared pool or uploaded textures. */
    vita2d_wait_rendering_done();
    if (g_sheet_gen != model2_tex_sheets_dirty_gen()
        || g_source_bytes > SOURCE_BUDGET * 3u / 4u || g_source_count == SOURCE_CAP) {
        clear_sources();
        g_sheet_gen = model2_tex_sheets_dirty_gen();
    }
    if (tiles_dirty) {
        upload_tiles(g_bottom, bottom, opaque);
        if (!opaque)
            upload_tiles(g_priority, priority, 0);
    }
    g_material_count = 0;
    vita2d_start_drawing();
    sceGxmSetFrontDepthFunc(vita2d_get_context(), SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthFunc(vita2d_get_context(), SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(vita2d_get_context(), SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetBackDepthWriteEnable(vita2d_get_context(), SCE_GXM_DEPTH_WRITE_DISABLED);
    vita2d_clear_screen();
    vita2d_draw_texture_scale(g_bottom, hud_x, 0, hud_scale, hud_scale);
    if (!opaque) {
        result = draw_geometry(geo_w / SYS24_FB_WIDTH, hud_scale, (960.0f - geo_w) * 0.5f, 0);
        vita2d_draw_texture_scale(g_priority, hud_x, 0, hud_scale, hud_scale);
    }
    vita2d_end_drawing();
    vita2d_swap_buffers();
    if (g_diagnostic_due) {
        char message[160];
        snprintf(message, sizeof(message),
                 "runtime: GXM frame=%u submitted geometry=%s triangles=%u result=%d\n",
                 g_diagnostic_frames, g_diagnostic_geometry, g_diagnostic_triangles, result);
        vita_startup_log(message);
    }
#if defined(I960_HOST_VITA_GXM)
    if (g_diagnostic_frames % 300u == 0) {
        char message[160];
        uint64_t now = sceKernelGetProcessTimeWide();
        uint64_t elapsed = now - g_performance_time;
        snprintf(message, sizeof(message),
                 "runtime: performance frame=%u fps=%.2f present_us=%llu\n",
                 g_diagnostic_frames,
                 elapsed ? 1000000.0 * (g_diagnostic_frames - g_performance_frame) / elapsed : 0.0,
                 (unsigned long long)(now - frame_started));
        vita_startup_log(message);
        g_performance_time = now;
        g_performance_frame = g_diagnostic_frames;
    }
#endif
    return result;
}

void vita_gxm_shutdown(void)
{
    if (!g_initialized)
        return;
    vita2d_wait_rendering_done();
    clear_sources();
    if (g_bottom) vita2d_free_texture(g_bottom);
    if (g_priority) vita2d_free_texture(g_priority);
    if (g_font) vita2d_free_texture(g_font);
    g_bottom = g_priority = NULL;
    g_font = NULL;
    free(g_order);
    g_order = NULL;
    g_order_cap = 0;
    vita2d_fini();
    g_initialized = 0;
    g_diagnostic_frames = 0;
#if defined(I960_HOST_VITA_GXM)
    g_performance_time = 0;
    g_performance_frame = 0;
#endif
}

void vita_gxm_message(const char *title, const char *detail)
{
    vita2d_wait_rendering_done();
    vita2d_start_drawing();
    vita2d_clear_screen();
    draw_text(70, 130, RGBA8(255, 255, 255, 255), 1.5f, title);
    draw_text(70, 220, RGBA8(255, 255, 255, 255), 1.0f, detail);
    draw_text(70, 350, RGBA8(255, 255, 255, 255), 1.1f,
                         "Cross: start   Circle: quit   Start + Select in game: pause");
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void vita_gxm_menu(int have_game)
{
    vita2d_wait_rendering_done();
    vita2d_start_drawing();
    vita2d_clear_screen();
    draw_menu(have_game);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}
