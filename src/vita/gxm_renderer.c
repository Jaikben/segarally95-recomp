#include "gxm_renderer.h"
#include "menu.h"
#include "startup_log.h"
#include "bitmap_font.h"
#include "gxm_math.h"
#include "gxm_order.h"
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
    SOURCE_BUCKETS = 512,
    MATERIAL_CAP = 2048,
    MATERIAL_BUCKETS = 4096,
    SOURCE_BUDGET = 32 * 1024 * 1024,
    POOL_BYTES = 16 * 1024 * 1024,
    MAX_BATCH_VERTICES = 65532
};

typedef struct {
    unsigned x, y, w, h;
} tile_bounds_t;

typedef struct {
    u16 x, y, w, h;
    u8 sheet;
    vita2d_texture texture;
} source_t;

typedef struct {
    model2_geo_tri_mat_t key;
    vita2d_texture view;
} material_t;

static source_t g_sources[SOURCE_CAP];
static u16 g_source_slots[SOURCE_BUCKETS];
static unsigned g_source_count, g_source_bytes, g_sheet_gen;
static SceUID g_source_uid = -1;
static u8 *g_source_data;
static int g_source_mapped;
static material_t g_materials[MATERIAL_CAP];
static u16 g_material_slots[MATERIAL_BUCKETS];
static unsigned g_material_count;
static vita2d_texture *g_bottom, *g_priority;
static int g_priority_nonempty;
static tile_bounds_t g_priority_bounds;
static vita2d_texture *g_font;
static vita2d_texture *g_checker;
static vita_painter_entry_t *g_order;
static unsigned g_order_cap;
static int g_initialized;
static unsigned g_diagnostic_frames;
static int g_diagnostic_due;
static unsigned g_diagnostic_triangles;
static unsigned g_diagnostic_draws, g_diagnostic_vertices, g_material_probes;
static unsigned g_diagnostic_checker_tris;
static unsigned g_diagnostic_checker_rgb; /* last solid-checker tri's RGB, for log visibility */
static unsigned g_diagnostic_tex_checker_tris; /* textured-checker (alpha=128) tri count */
static const char *g_diagnostic_geometry;

static void clear_sources(void)
{
    /* The caller fences GPU readers before recycling arena storage. */
    memset(g_source_slots, 0, sizeof(g_source_slots));
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

static void release_source_arena(void)
{
    if (g_source_mapped) {
        if (sceGxmUnmapMemory(g_source_data) < 0) {
            fail("texture arena unmapping failed; retaining its storage");
            return;
        }
        g_source_mapped = 0;
    }
    if (g_source_uid >= 0) {
        if (sceKernelFreeMemBlock(g_source_uid) < 0) {
            fail("texture arena release failed");
            return;
        }
        g_source_uid = -1;
        g_source_data = NULL;
    }
}

static int source_arena_open(void)
{
    void *base = NULL;
    if (g_source_mapped)
        return 0;
    if (g_source_uid >= 0)
        return fail("previous texture arena release incomplete");
    g_source_uid = sceKernelAllocMemBlock("segamod2 textures",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SOURCE_BUDGET, NULL);
    if (g_source_uid < 0)
        return fail("32 MiB texture arena allocation failed");
    if (sceKernelGetMemBlockBase(g_source_uid, &base) < 0) {
        release_source_arena();
        return fail("texture arena base lookup failed");
    }
    g_source_data = base;
    if (sceGxmMapMemory(g_source_data, SOURCE_BUDGET, SCE_GXM_MEMORY_ATTRIB_READ) < 0) {
        release_source_arena();
        return fail("texture arena mapping failed");
    }
    g_source_mapped = 1;
    return 0;
}

static u32 hash_field(u32 hash, u32 value)
{
    return (hash ^ value) * 16777619u;
}

static u32 patch_hash(unsigned x, unsigned y, unsigned w, unsigned h, unsigned sheet)
{
    u32 hash = hash_field(2166136261u, x);
    hash = hash_field(hash, y);
    hash = hash_field(hash, w);
    hash = hash_field(hash, h);
    hash = hash_field(hash, sheet);
    return hash ^ (hash >> 16);
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

static int create_checker(void)
{
    unsigned stride;
    u32 *pixels;
    g_checker = vita2d_create_empty_texture(2, 2);
    if (!g_checker)
        return fail("checker mask allocation failed");
    stride = vita2d_texture_get_stride(g_checker);
    pixels = vita2d_texture_get_datap(g_checker);
    if (!pixels || stride < 2u * sizeof(u32) || stride % sizeof(u32))
        return fail("checker mask storage unavailable");
    memset(pixels, 0, stride * 2u);
    pixels[1] = pixels[stride / sizeof(u32)] = RGBA8(255, 255, 255, 255);
    vita2d_texture_set_filters(g_checker, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    if (sceGxmTextureSetUAddrMode(&g_checker->gxm_tex, SCE_GXM_TEXTURE_ADDR_REPEAT) < 0
        || sceGxmTextureSetVAddrMode(&g_checker->gxm_tex, SCE_GXM_TEXTURE_ADDR_REPEAT) < 0)
        return fail("checker mask addressing failed");
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
    g_priority_nonempty = 0;
    memset(&g_priority_bounds, 0, sizeof(g_priority_bounds));
    vita2d_set_vblank_wait(0);
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
    g_bottom = vita2d_create_empty_texture(SYS24_FB_WIDTH, SYS24_FB_HEIGHT);
    g_priority = vita2d_create_empty_texture(SYS24_FB_WIDTH, SYS24_FB_HEIGHT);
    vita_startup_log("startup: tile textures allocated; built-in bitmap font creation begin\n");
    if (!g_bottom || !g_priority) {
        vita_gxm_shutdown();
        return fail("tile texture allocation failed");
    }
    if (create_font() != 0 || create_checker() != 0) {
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
    unsigned y, x, slot, offset, stride, bytes;
    unsigned w = m->patch_w ? m->patch_w : 32;
    unsigned h = m->patch_h ? m->patch_h : 32;
    const u32 *sheet;
    source_t *s;
    slot = patch_hash(m->patch_x, m->patch_y, w, h, m->sheet) & (SOURCE_BUCKETS - 1u);
    while (g_source_slots[slot]) {
        s = &g_sources[g_source_slots[slot] - 1u];
        if (s->x == m->patch_x && s->y == m->patch_y && s->w == w
            && s->h == h && s->sheet == m->sheet)
            return &s->texture;
        slot = (slot + 1u) & (SOURCE_BUCKETS - 1u);
    }
    stride = (w + 7u) & ~7u;
    bytes = stride * h;
    offset = (g_source_bytes + SCE_GXM_TEXTURE_ALIGNMENT - 1u)
        & ~(SCE_GXM_TEXTURE_ALIGNMENT - 1u);
    if (g_source_count == SOURCE_CAP || w > 4096 || h > 4096
        || bytes > SOURCE_BUDGET - offset) {
        fail("texture cache budget exhausted (no geometry silently dropped)");
        return NULL;
    }
    sheet = model2_tex_sheet_bank(m->sheet);
    if (!sheet) {
        fail("texture sheet unavailable");
        return NULL;
    }
    if (source_arena_open() != 0)
        return NULL;
    s = &g_sources[g_source_count];
    memset(&s->texture, 0, sizeof(s->texture));
    if (sceGxmTextureInitLinear(&s->texture.gxm_tex, g_source_data + offset,
            SCE_GXM_TEXTURE_FORMAT_P8_ABGR, w, h, 0) < 0) {
        fail("indexed patch initialization failed");
        return NULL;
    }
    s->x = m->patch_x; s->y = m->patch_y;
    s->w = (u16)w; s->h = (u16)h; s->sheet = m->sheet;
    vita2d_texture_set_filters(&s->texture, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    for (y = 0; y < h; y++) {
        u8 *row = g_source_data + offset + y * stride;
        for (x = 0; x < w; x++) {
            /* Decode the same logical sheet coordinates as the GL R8 atlas. */
            row[x] = (u8)model2_get_texel(sheet, 0, 0,
                         (int)((m->patch_x + x) & 2047u),
                         (int)((m->patch_y + y) & 1023u));
        }
    }
    g_source_bytes = offset + bytes;
    g_source_slots[slot] = (u16)(g_source_count + 1u);
    g_source_count++;
    return &s->texture;
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
    unsigned i, slot;
    u32 hash = patch_hash(m->patch_x, m->patch_y, m->patch_w, m->patch_h, m->sheet);
    u8 rgba[64];
    u32 *palette;
    vita2d_texture *source;
    material_t *out;
    hash = hash_field(hash, m->colorbase);
    hash = hash_field(hash, m->lumabase);
    hash = hash_field(hash, m->luma);
    hash = hash_field(hash, m->flags);
    slot = (hash ^ (hash >> 16)) & (MATERIAL_BUCKETS - 1u);
    for (;;) {
        g_material_probes++;
        if (!g_material_slots[slot])
            break;
        i = g_material_slots[slot] - 1u;
        if (same_material(m, &g_materials[i].key))
            return &g_materials[i].view;
        slot = (slot + 1u) & (MATERIAL_BUCKETS - 1u);
    }
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
    g_material_slots[slot] = (u16)(g_material_count + 1u);
    g_material_count++;
    return &out->view;
}

static vita2d_texture_vertex gpu_vertex(vita_screen_vertex_t v, float sx, float sy,
                                       float ox, float oy, unsigned w, unsigned h)
{
    return (vita2d_texture_vertex){
        ox + v.x * sx, oy + v.y * sy, 0.5f,
        v.uq / (v.q * (float)w), v.vq / (v.q * (float)h)
    };
}

typedef struct {
    const vita2d_texture *texture;
    void *vertices;
    unsigned count, tint;
} batch_t;

static void flush_batch(batch_t *batch)
{
    if (!batch->count)
        return;
    if (batch->texture)
        vita2d_draw_array_textured(batch->texture, SCE_GXM_PRIMITIVE_TRIANGLES,
            batch->vertices, batch->count, batch->tint);
    else
        vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, batch->vertices, batch->count);
    g_diagnostic_draws++;
    g_diagnostic_vertices += batch->count;
    batch->count = 0;
}

static void append_batch(batch_t *batch, const vita2d_texture *texture,
                         void *vertices, unsigned count, unsigned tint)
{
    unsigned stride = texture ? sizeof(vita2d_texture_vertex) : sizeof(vita2d_color_vertex);
    /* Never reorder polygons or span palette/alignment gaps in the shared pool. */
    if (batch->count && (batch->texture != texture || batch->tint != tint
        || batch->count + count > MAX_BATCH_VERTICES
        || (u8 *)batch->vertices + batch->count * stride != vertices))
        flush_batch(batch);
    if (!batch->count) {
        batch->texture = texture;
        batch->vertices = vertices;
        batch->tint = tint;
    }
    batch->count += count;
}

static int draw_triangle(batch_t *batch, const vita_screen_vertex_t v[3], const model2_geo_tri_mat_t *m,
                          float sx, float sy, float ox, float oy)
{
    unsigned i, j, count = 0;
    unsigned alpha = (m->flags & MODEL2_GEO_TEX_CHECKER) ? 128 : 255;
    if (m->flags & MODEL2_GEO_TEX_TEXTURED) {
        const unsigned subdivisions = vita_perspective_subdivisions(v, sx, sy);
        const vita2d_texture *texture = material_for(m);
        vita2d_texture_vertex *vertices;
        vita2d_texture_vertex grid[9][9];
        unsigned w = m->patch_w ? m->patch_w : 32;
        unsigned h = m->patch_h ? m->patch_h : 32;
        if (!texture)
            return -1;
        if (m->flags & MODEL2_GEO_TEX_CHECKER)
            g_diagnostic_tex_checker_tris++;
        vertices = geometry_alloc(3 * subdivisions * subdivisions * sizeof(*vertices), 4);
        if (!vertices)
            return fail("GPU vertex pool exhausted");
        for (i = 0; i <= subdivisions; i++)
            for (j = 0; j <= subdivisions - i; j++)
                grid[i][j] = gpu_vertex(vita_barycentric(v,
                    (float)i / subdivisions, (float)j / subdivisions),
                    sx, sy, ox, oy, w, h);
        for (i = 0; i < subdivisions; i++) {
            for (j = 0; j < subdivisions - i; j++) {
                vertices[count++] = grid[i][j];
                vertices[count++] = grid[i + 1][j];
                vertices[count++] = grid[i][j + 1];
                if (j + i + 1 < subdivisions) {
                    vertices[count++] = grid[i + 1][j];
                    vertices[count++] = grid[i + 1][j + 1];
                    vertices[count++] = grid[i][j + 1];
                }
            }
        }
        append_batch(batch, texture, vertices, count, RGBA8(255, 255, 255, alpha));
    } else {
        u8 rgb[3];
        model2_palette_lookup_solid(m->colorbase, m->luma, rgb);
        if (m->flags & MODEL2_GEO_TEX_CHECKER) {
            vita2d_texture_vertex *vertices = geometry_alloc(3 * sizeof(*vertices), 4);
            if (!vertices)
                return fail("GPU checker vertex pool exhausted");
            g_diagnostic_checker_tris++;
            g_diagnostic_checker_rgb = RGBA8(rgb[0], rgb[1], rgb[2], 255);
            for (i = 0; i < 3; i++) {
                /* UV must track the actual display pixel, not the pre-upscale
                 * game coordinate — otherwise the 2x2 dither cell spans more
                 * than one real pixel (sx/sy > 1 on Vita's 960x544 panel) and
                 * the checkerboard comes out stretched/moire instead of a
                 * clean per-pixel mask. */
                float sxp = ox + v[i].x * sx;
                float syp = oy + v[i].y * sy;
                vertices[i] = (vita2d_texture_vertex){sxp, syp, 0.5f, sxp * 0.5f, syp * 0.5f};
            }
            append_batch(batch, g_checker, vertices, 3, RGBA8(rgb[0], rgb[1], rgb[2], 255));
            return 0;
        }
        vita2d_color_vertex *vertices = geometry_alloc(3 * sizeof(*vertices), 4);
        if (!vertices)
            return fail("GPU solid vertex pool exhausted");
        for (i = 0; i < 3; i++)
            vertices[i] = (vita2d_color_vertex){
                ox + v[i].x * sx, oy + v[i].y * sy, 0.5f,
                RGBA8(rgb[0], rgb[1], rgb[2], alpha)
            };
        append_batch(batch, NULL, vertices, 3, 0);
    }
    return 0;
}

static int draw_geometry(float sx, float sy, float ox, float oy)
{
    batch_t batch = {0};
    const float *xyzuv;
    const model2_geo_tri_mat_t *mats;
    const vita_painter_entry_t *order;
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
        vita_painter_entry_t *storage;
        if ((size_t)ntris > SIZE_MAX / sizeof(*storage) / 2u) {
            model2_geo_unlock();
            return fail("polygon order size overflow");
        }
        storage = realloc(g_order, (size_t)ntris * 2u * sizeof(*storage));
        if (!storage) {
            model2_geo_unlock();
            return fail("polygon order allocation failed");
        }
        g_order = storage;
        g_order_cap = ntris;
    }
    for (t = 0; t < ntris; t++) {
        g_order[t].index = t;
        if (!vita_painter_key(&mats[t], &g_order[t].key)) {
            model2_geo_unlock();
            return fail("non-finite polygon sort depth");
        }
    }
    order = ntris ? vita_painter_sort(g_order, g_order + g_order_cap, ntris) : g_order;
    for (t = 0; t < ntris && result == 0; t++) {
        unsigned tri = order[t].index, n;
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
            result = draw_triangle(&batch, fan, &mats[tri], sx, sy, ox, oy);
        }
    }
    flush_batch(&batch);
    model2_geo_unlock();
    return result;
}

static int upload_tiles(vita2d_texture *texture, const u32 *pixels, int opaque,
                       tile_bounds_t *bounds)
{
    int has_pixels = 0;
    unsigned left = SYS24_FB_WIDTH, top = SYS24_FB_HEIGHT;
    unsigned right = 0, bottom = 0, y, x;
    unsigned stride = vita2d_texture_get_stride(texture);
    u8 *base = vita2d_texture_get_datap(texture);
    for (y = 0; y < SYS24_FB_HEIGHT; y++) {
        u32 *row = (u32 *)(base + y * stride);
        for (x = 0; x < SYS24_FB_WIDTH; x++) {
            u32 p = pixels[y * SYS24_FB_WIDTH + x];
            if (p & 0xffffffu) {
                has_pixels = 1;
                if (bounds) {
                    if (x < left) left = x;
                    if (y < top) top = y;
                    if (x + 1u > right) right = x + 1u;
                    if (y + 1u > bottom) bottom = y + 1u;
                }
            }
            row[x] = !opaque && !(p & 0xffffffu) ? 0 : vita_swap_rb(p | 0xff000000u);
        }
    }
    if (bounds) {
        bounds->x = has_pixels ? left : 0;
        bounds->y = has_pixels ? top : 0;
        bounds->w = has_pixels ? right - left : 0;
        bounds->h = has_pixels ? bottom - top : 0;
    }
    return has_pixels;
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
    /* Full-framebuffer pixel scan is startup-bringup diagnostics only — drop it
     * from the perpetual 300-frame cadence so idle/menu/2D-only screens don't
     * keep paying an O(w*h) scan forever just for a log line. */
    {
        int pixel_scan_due = g_diagnostic_frames <= 3u
            || (g_diagnostic_frames <= 1800u && g_diagnostic_frames % 120u == 0);
        g_diagnostic_triangles = 0;
        g_diagnostic_draws = g_diagnostic_vertices = g_material_probes = 0;
        g_diagnostic_checker_tris = 0;
        g_diagnostic_checker_rgb = 0;
        g_diagnostic_tex_checker_tris = 0;
        g_diagnostic_geometry = opaque ? "tiles only" : "not started";
        if (pixel_scan_due) {
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
    }
    /* Triple buffering does not protect the shared pool or uploaded textures. */
    vita2d_wait_rendering_done();
    if (g_sheet_gen != model2_tex_sheets_dirty_gen()
        || g_source_bytes > SOURCE_BUDGET * 3u / 4u || g_source_count == SOURCE_CAP) {
        clear_sources();
        g_sheet_gen = model2_tex_sheets_dirty_gen();
    }
    if (tiles_dirty) {
        upload_tiles(g_bottom, bottom, opaque, NULL);
        if (!opaque)
            g_priority_nonempty = upload_tiles(g_priority, priority, 0,
                                               &g_priority_bounds);
    }
    g_material_count = 0;
    memset(g_material_slots, 0, sizeof(g_material_slots));
    vita2d_start_drawing();
    sceGxmSetFrontDepthFunc(vita2d_get_context(), SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthFunc(vita2d_get_context(), SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(vita2d_get_context(), SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetBackDepthWriteEnable(vita2d_get_context(), SCE_GXM_DEPTH_WRITE_DISABLED);
    if (opaque && hud_x > 0.0f) {
        /* Tiles-only screens force every g_bottom pixel opaque (see
         * upload_tiles's !opaque branch), so the center band below is
         * guaranteed fully overwritten — only the pillarbox bars actually
         * need the clear color, not the whole 960x544 framebuffer. */
        vita2d_draw_rectangle(0, 0, hud_x, 544, RGBA8(0, 0, 0, 255));
        vita2d_draw_rectangle(960.0f - hud_x, 0, hud_x, 544, RGBA8(0, 0, 0, 255));
    } else {
        vita2d_clear_screen();
    }
    vita2d_draw_texture_scale(g_bottom, hud_x, 0, hud_scale, hud_scale);
    if (!opaque) {
        result = draw_geometry(geo_w / SYS24_FB_WIDTH, hud_scale, (960.0f - geo_w) * 0.5f, 0);
        if (g_priority_nonempty)
            vita2d_draw_texture_tint_part_scale(g_priority,
                hud_x + g_priority_bounds.x * hud_scale,
                g_priority_bounds.y * hud_scale,
                (float)g_priority_bounds.x, (float)g_priority_bounds.y,
                (float)g_priority_bounds.w, (float)g_priority_bounds.h,
                hud_scale, hud_scale, RGBA8(255, 255, 255, 255));
    }
    vita2d_end_drawing();
    vita2d_swap_buffers();
    if (g_diagnostic_due) {
        char message[256];
        snprintf(message, sizeof(message),
                 "runtime: GXM frame=%u submitted geometry=%s triangles=%u result=%d draws=%u vertices=%u materials=%u probes=%u sources=%u source_bytes=%u checker_tris=%u checker_rgba=%#x tex_checker_tris=%u\n",
                 g_diagnostic_frames, g_diagnostic_geometry, g_diagnostic_triangles, result,
                 g_diagnostic_draws, g_diagnostic_vertices, g_material_count,
                 g_material_probes, g_source_count, g_source_bytes,
                 g_diagnostic_checker_tris, g_diagnostic_checker_rgb,
                 g_diagnostic_tex_checker_tris);
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
    release_source_arena();
    if (g_bottom) vita2d_free_texture(g_bottom);
    if (g_priority) vita2d_free_texture(g_priority);
    if (g_font) vita2d_free_texture(g_font);
    if (g_checker) vita2d_free_texture(g_checker);
    g_bottom = g_priority = NULL;
    g_font = NULL;
    g_checker = NULL;
    g_priority_nonempty = 0;
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
