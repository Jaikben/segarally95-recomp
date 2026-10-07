#include "gxm_renderer.h"
#include "model2_geo_tex.h"
#include "sys24_tile.h"
#include "menu.h"
#include "bitmap_font.h"
#include "gxm_math.h"
#include <vita2d.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)
static unsigned char *pool;
static unsigned pool_size, pool_used, pending, waits, locked, gen;
static unsigned texture_draws, geometry_draws, geometry_vertices, order[16384], order_count;
static FILE *snapshot;
static unsigned texel_reads, arena_allocations, indexed_allocations, palette_bias, texel_bias;
static unsigned char *arena;
static unsigned arena_size, arena_mapped, last_patch_end;
static int fail_arena, fail_map, fail_base, fail_texture, check_source_pixels;
static float *stress_mesh;
static model2_geo_tri_mat_t *stress_mats;
static unsigned stress_triangles;
static unsigned first_tile, last_tile;
static int no_mesh, no_projection, exhaust_pool, wide, fail_font;
static unsigned menu_highlights, menu_labels;
static unsigned glyph_draws;
static unsigned checker_draws, expected_subdivisions;
static unsigned priority_part_draws;
static float priority_crop[8];
static int fail_checker;
static SceGxmContext context;
static float mesh[] = {
    -20, -20, 1, 0, 0, 20, -20, 2, 32, 0, 0, 20, 3, 16, 32,
    -20, -20, 1, 0, 0, 20, -20, 2, 32, 0, 0, 20, 3, 16, 32
};
static model2_geo_tri_mat_t mats[2];

int vita2d_init_advanced(unsigned size) { pool = malloc(size); pool_size = size; return pool != NULL; }
void vita2d_set_vblank_wait(int e) { (void)e; }
void vita2d_set_clear_color(unsigned c) { (void)c; }
vita2d_texture *vita2d_create_empty_texture_format(unsigned w, unsigned h, unsigned format)
{
    vita2d_texture *t = calloc(1, sizeof(*t));
    if ((fail_font && w == VITA_FONT_WIDTH && h == VITA_FONT_HEIGHT)
        || (fail_checker && w == 2 && h == 2)) {
        free(t);
        return NULL;
    }
    CHECK(t);
    if (format == SCE_GXM_TEXTURE_FORMAT_P8_ABGR)
        indexed_allocations++;
    t->w = w; t->h = h; t->format = format;
    t->gxm_tex.w = w; t->gxm_tex.h = h;
    t->stride = ((w + 31u) & ~31u) * (format == SCE_GXM_TEXTURE_FORMAT_P8_ABGR ? 1 : 4);
    t->data = calloc(h, t->stride);
    CHECK(t->data);
    return t;
}
vita2d_texture *vita2d_create_empty_texture(unsigned w, unsigned h)
{ return vita2d_create_empty_texture_format(w, h, 999); }
void vita2d_texture_set_filters(vita2d_texture *t, unsigned a, unsigned b) { (void)t; (void)a; (void)b; }
void vita2d_free_texture(vita2d_texture *t) { CHECK(!pending); free(t->data); free(t); }
unsigned vita2d_texture_get_stride(const vita2d_texture *t)
{ return t->data ? t->stride : (t->gxm_tex.w + 7u) & ~7u; }
void *vita2d_texture_get_datap(const vita2d_texture *t)
{ CHECK(!pending); return t->data ? t->data : (void *)t->gxm_tex.data; }
SceUID sceKernelAllocMemBlock(const char *name, unsigned type, unsigned size, const void *options)
{
    (void)name; (void)options;
    CHECK(!pending && !arena && type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW);
    CHECK(size == 32u * 1024u * 1024u && size % (256u * 1024u) == 0);
    if (fail_arena)
        return -1;
    arena = malloc(size);
    CHECK(arena);
    arena_size = size;
    arena_allocations++;
    return 1;
}
int sceKernelGetMemBlockBase(SceUID uid, void **base)
{
    CHECK(uid == 1 && arena);
    if (fail_base)
        return -1;
    *base = arena;
    return 0;
}
int sceKernelFreeMemBlock(SceUID uid)
{
    CHECK(!pending && !arena_mapped && uid == 1 && arena);
    free(arena); arena = NULL; arena_size = 0;
    return 0;
}
int sceGxmMapMemory(void *base, unsigned size, unsigned attributes)
{
    CHECK(!pending && base == arena && size == arena_size
        && attributes == SCE_GXM_MEMORY_ATTRIB_READ && !arena_mapped);
    if (fail_map)
        return -1;
    arena_mapped = 1;
    return 0;
}
int sceGxmUnmapMemory(void *base)
{
    CHECK(!pending && base == arena && arena_mapped);
    arena_mapped = 0;
    return 0;
}
int sceGxmTextureInitLinear(SceGxmTexture *t, const void *data, unsigned format,
    unsigned w, unsigned h, unsigned mip_count)
{
    size_t offset = (const unsigned char *)data - arena;
    CHECK(!pending && arena_mapped && format == SCE_GXM_TEXTURE_FORMAT_P8_ABGR);
    CHECK(w && h && w <= 4096 && h <= 4096 && mip_count == 0);
    CHECK(offset % SCE_GXM_TEXTURE_ALIGNMENT == 0);
    CHECK(offset + ((w + 7u) & ~7u) * h <= arena_size);
    if (fail_texture)
        return -1;
    if (!offset)
        last_patch_end = 0;
    CHECK(offset >= last_patch_end);
    last_patch_end = (unsigned)offset + ((w + 7u) & ~7u) * h;
    t->data = data; t->w = w; t->h = h;
    return 0;
}
void *vita2d_pool_memalign(unsigned size, unsigned alignment)
{
    unsigned pos = (pool_used + alignment - 1) & ~(alignment - 1);
    if (exhaust_pool || size > pool_size - pos)
        return NULL;
    pool_used = pos + size;
    return pool + pos;
}
unsigned vita2d_pool_free_space(void) { return pool_size - pool_used; }
int sceGxmTextureSetPalette(SceGxmTexture *t, void *p) { t->palette = p; return 0; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *t, unsigned m) { t->u = m; return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *t, unsigned m) { t->v = m; return 0; }
void vita2d_draw_array_textured(const vita2d_texture *t, unsigned mode,
    const vita2d_texture_vertex *v, size_t n, unsigned color)
{
    size_t i;
    const unsigned *p = t->gxm_tex.palette;
    (void)mode; (void)color;
    if (t->w == 2 && t->h == 2) {
        unsigned x, y, stride = t->stride / sizeof(unsigned);
        const unsigned *pixels = t->data;
        float sx = wide ? 960.0f / SYS24_FB_WIDTH : 544.0f / SYS24_FB_HEIGHT;
        float sy = 544.0f / SYS24_FB_HEIGHT;
        float ox = wide ? 0 : (960.0f - SYS24_FB_WIDTH * sx) * 0.5f;
        CHECK(locked && n == 3 && (color >> 24) == 255);
        CHECK(t->gxm_tex.u == SCE_GXM_TEXTURE_ADDR_REPEAT
            && t->gxm_tex.v == SCE_GXM_TEXTURE_ADDR_REPEAT);
        for (i = 0; i < n; i++) {
            if (fabsf(v[i].u - (v[i].x - ox) / sx * 0.5f) >= 0.0001f)
                fprintf(stderr, "checker vertex x=%f u=%f sx=%f ox=%f wide=%d\n",
                    v[i].x, v[i].u, sx, ox, wide);
            CHECK(fabsf(v[i].u - (v[i].x - ox) / sx * 0.5f) < 0.0001f);
            CHECK(fabsf(v[i].v - v[i].y / sy * 0.5f) < 0.0001f);
        }
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++) {
                unsigned pixel = pixels[(y & 1u) * stride + (x & 1u)];
                CHECK(pixel == (((x ^ y) & 1u) ? RGBA8(255, 255, 255, 255) : 0));
            }
        CHECK(order_count < sizeof(order) / sizeof(order[0]));
        order[order_count++] = color & 255u;
        geometry_draws++;
        geometry_vertices += (unsigned)n;
        checker_draws++;
        return;
    }
    CHECK(locked && p);
    CHECK(t->gxm_tex.u == SCE_GXM_TEXTURE_ADDR_MIRROR || t->gxm_tex.u == SCE_GXM_TEXTURE_ADDR_REPEAT);
    CHECK(t->gxm_tex.v == SCE_GXM_TEXTURE_ADDR_MIRROR || t->gxm_tex.v == SCE_GXM_TEXTURE_ADDR_REPEAT);
    CHECK((p[15] >> 24) == 0);
    CHECK(n && n % 3 == 0 && n <= 65532);
    CHECK(order_count < sizeof(order) / sizeof(order[0]));
    order[order_count++] = p[0] & 255u;
    if (check_source_pixels) {
        const unsigned char *data = t->gxm_tex.data;
        unsigned expected = (2u * ((p[0] - 10u) & 255u) + texel_bias) & 15u;
        unsigned w = t->gxm_tex.w, h = t->gxm_tex.h;
        unsigned stride = (w + 7u) & ~7u;
        CHECK(data[0] == expected
            && data[(h - 1u) * stride + w - 1u] == ((expected + w + h - 2u) & 15u));
    }
    if (expected_subdivisions) {
        vita_screen_vertex_t screen[3];
        vita2d_texture_vertex grid[9][9];
        unsigned row, col, at = 0, s = expected_subdivisions;
        float sx = wide ? 960.0f / SYS24_FB_WIDTH : 544.0f / SYS24_FB_HEIGHT;
        float sy = 544.0f / SYS24_FB_HEIGHT;
        float ox = wide ? 0 : (960.0f - SYS24_FB_WIDTH * sx) * 0.5f;
        CHECK(n == 3u * s * s);
        for (i = 0; i < 3; i++) {
            float q = 1.0f / mesh[i * 5u + 2u];
            screen[i] = (vita_screen_vertex_t){
                248 + mesh[i * 5u] * q, 192 - mesh[i * 5u + 1u] * q,
                q, mesh[i * 5u + 3u] * q, mesh[i * 5u + 4u] * q
            };
        }
        for (row = 0; row <= s; row++)
            for (col = 0; col <= s - row; col++) {
                float b = (float)row / s, c = (float)col / s, a = 1.0f - b - c;
                float q = a * screen[0].q + b * screen[1].q + c * screen[2].q;
                grid[row][col] = (vita2d_texture_vertex){
                    ox + (a * screen[0].x + b * screen[1].x + c * screen[2].x) * sx,
                    (a * screen[0].y + b * screen[1].y + c * screen[2].y) * sy, 0.5f,
                    (a * screen[0].uq + b * screen[1].uq + c * screen[2].uq) / (q * 32),
                    (a * screen[0].vq + b * screen[1].vq + c * screen[2].vq) / (q * 32)
                };
            }
        for (row = 0; row < s; row++)
            for (col = 0; col < s - row; col++) {
                CHECK(memcmp(v + at++, &grid[row][col], sizeof(*v)) == 0);
                CHECK(memcmp(v + at++, &grid[row + 1][col], sizeof(*v)) == 0);
                CHECK(memcmp(v + at++, &grid[row][col + 1], sizeof(*v)) == 0);
                if (row + col + 1 < s) {
                    CHECK(memcmp(v + at++, &grid[row + 1][col], sizeof(*v)) == 0);
                    CHECK(memcmp(v + at++, &grid[row + 1][col + 1], sizeof(*v)) == 0);
                    CHECK(memcmp(v + at++, &grid[row][col + 1], sizeof(*v)) == 0);
                }
            }
    }
    for (i = 0; i < n; i++) {
        CHECK(isfinite(v[i].x) && isfinite(v[i].y) && isfinite(v[i].u) && isfinite(v[i].v));
        if (snapshot) {
            unsigned kind = 1;
            CHECK(fwrite(&kind, sizeof(kind), 1, snapshot) == 1);
            CHECK(fwrite(v + i, sizeof(*v), 1, snapshot) == 1);
            CHECK(fwrite(p, 64, 1, snapshot) == 1);
            CHECK(fwrite(&color, sizeof(color), 1, snapshot) == 1);
            CHECK(fwrite(&t->gxm_tex.u, sizeof(unsigned), 1, snapshot) == 1);
            CHECK(fwrite(&t->gxm_tex.v, sizeof(unsigned), 1, snapshot) == 1);
        }
    }
    geometry_draws++;
    geometry_vertices += (unsigned)n;
}
void vita2d_draw_array(unsigned m, const vita2d_color_vertex *v, size_t n)
{
    size_t i;
    (void)m;
    CHECK(locked && n && n % 3 == 0 && n <= 65532);
    CHECK(order_count < sizeof(order) / sizeof(order[0]));
    order[order_count++] = v[0].color & 255u;
    for (i = 0; i < n; i++) {
        CHECK(isfinite(v[i].x) && isfinite(v[i].y));
        if (snapshot) {
            unsigned kind = 0;
            CHECK(fwrite(&kind, sizeof(kind), 1, snapshot) == 1);
            CHECK(fwrite(v + i, sizeof(*v), 1, snapshot) == 1);
        }
    }
    geometry_draws++;
    geometry_vertices += (unsigned)n;
}
void vita2d_wait_rendering_done(void) { pending = 0; waits++; }
void vita2d_start_drawing(void) { CHECK(!pending); pool_used = 0; }
void vita2d_end_drawing(void) { pending = 1; }
void vita2d_swap_buffers(void) {}
void vita2d_clear_screen(void) {}
SceGxmContext *vita2d_get_context(void) { return &context; }
void sceGxmSetFrontDepthFunc(SceGxmContext *c, unsigned m) { (void)c; (void)m; }
void sceGxmSetBackDepthFunc(SceGxmContext *c, unsigned m) { (void)c; (void)m; }
void sceGxmSetFrontDepthWriteEnable(SceGxmContext *c, unsigned m) { (void)c; (void)m; }
void sceGxmSetBackDepthWriteEnable(SceGxmContext *c, unsigned m) { (void)c; (void)m; }
void vita2d_draw_texture_scale(const vita2d_texture *t, float x, float y, float sx, float sy)
{
    (void)x; (void)y; (void)sx; (void)sy;
    CHECK(!locked);
    first_tile = ((const unsigned *)t->data)[0];
    last_tile = *(const unsigned *)((const unsigned char *)t->data + (SYS24_FB_HEIGHT - 1) * t->stride);
    texture_draws++;
}
void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; }
void vita2d_draw_texture_tint_part_scale(const vita2d_texture *t, float x, float y,
    float tx, float ty, float w, float h, float sx, float sy, unsigned color)
{
    if (t->w == SYS24_FB_WIDTH && t->h == SYS24_FB_HEIGHT) {
        priority_crop[0] = x; priority_crop[1] = y;
        priority_crop[2] = tx; priority_crop[3] = ty;
        priority_crop[4] = w; priority_crop[5] = h;
        priority_crop[6] = sx; priority_crop[7] = sy;
        priority_part_draws++;
        return;
    }
    unsigned row, col, character = 32 + (unsigned)(ty / 8) * 16 + (unsigned)(tx / 8);
    int yellow = color == RGBA8(255, 200, 70, 255);
    CHECK(t->w == VITA_FONT_WIDTH && t->h == VITA_FONT_HEIGHT);
    CHECK(w == 5 && h == 7 && sx > 0 && sx == sy);
    CHECK(x >= 0 && x + w * sx <= 930.001f && y >= 0 && y + h * sy <= 544);
    CHECK(tx >= 0 && tx + w <= t->w && ty >= 0 && ty + h <= t->h);
    for (row = 0; row < 7; ++row)
        for (col = 0; col < 5; ++col) {
            unsigned pixel = *(const unsigned *)((const unsigned char *)t->data
                + ((unsigned)ty + row) * t->stride + ((unsigned)tx + col) * 4);
            CHECK(pixel == ((vita_font_row(character, row) & (1u << (4 - col)))
                ? RGBA8(255, 255, 255, 255) : 0));
        }
    if (character == '>' && fabsf(x - 42) < 0.001f) {
        CHECK(yellow);
        menu_highlights++;
        menu_labels++;
    }
    if (!yellow && fabsf(x - 68.4f) < 0.001f)
        menu_labels++;
    glyph_draws++;
}
int vita2d_fini(void) { CHECK(!pending); free(pool); pool = NULL; return 1; }
int model2_geo_projection(model2_geo_projection_t *p)
{ *p = (model2_geo_projection_t){1, 1, {0, 0, 496, 384}, {248, 192}, 1}; return !no_projection; }
int model2_geo_lock_textured(const float **v, unsigned *nv, const model2_geo_tri_mat_t **m, unsigned *nt)
{
    if (no_mesh) { *v = NULL; *m = NULL; *nv = *nt = 0; return -1; }
    CHECK(!locked); locked = 1;
    *v = stress_triangles ? stress_mesh : mesh;
    *nv = stress_triangles ? stress_triangles * 3u : 6u;
    *m = stress_triangles ? stress_mats : mats;
    *nt = stress_triangles ? stress_triangles : 2u;
    return 0;
}
void model2_geo_unlock(void) { CHECK(locked); locked = 0; }
static void set_widescreen(int enabled)
{
#if defined(_WIN32)
    CHECK(_putenv_s("I960_HOST_ASPECT", enabled ? "16:9" : "4:3") == 0);
#else
    CHECK(setenv("I960_HOST_ASPECT", enabled ? "16:9" : "4:3", 1) == 0);
#endif
    wide = enabled;
}
u32 model2_tex_sheets_dirty_gen(void) { return gen; }
const u32 *model2_tex_sheet_bank(unsigned s) { static u32 bank; (void)s; return &bank; }
u16 model2_get_texel(const u32 *s, u32 bx, u32 by, int x, int y)
{ (void)s; (void)bx; (void)by; texel_reads++; return (u16)((x + y + texel_bias) & 15); }
void model2_palette_build_texel_lut(u32 cb, u32 lb, u32 luma, int cutout, u8 rgba[64])
{
    unsigned i;
    for (i = 0; i < 16; i++) {
        rgba[i*4] = (u8)(cb + palette_bias);
        rgba[i*4+1] = (u8)(lb + 2); rgba[i*4+2] = (u8)(luma + 3);
        rgba[i*4+3] = cutout && i == 15 ? 0 : 255;
    }
}
void model2_palette_lookup_solid(u32 cb, u32 luma, u8 rgb[3])
{ (void)cb; (void)luma; memset(rgb, 255, 3); }

static void set_stress_mesh(unsigned triangles, unsigned group)
{
    unsigned i;
    free(stress_mesh); free(stress_mats);
    stress_triangles = triangles;
    stress_mesh = malloc((size_t)triangles * 15u * sizeof(*stress_mesh));
    stress_mats = malloc((size_t)triangles * sizeof(*stress_mats));
    CHECK(stress_mesh && stress_mats);
    for (i = 0; i < triangles; i++) {
        memcpy(stress_mesh + i * 15u, mesh, 15u * sizeof(*mesh));
        stress_mats[i] = mats[0];
        stress_mats[i].colorbase = (u16)(10u + i / group);
        stress_mats[i].z_sort = (float)(triangles - i);
    }
    order_count = 0;
}

int main(int argc, char **argv)
{
    unsigned n = SYS24_FB_WIDTH * SYS24_FB_HEIGHT, i;
    u32 *bottom = calloc(n, sizeof(*bottom)), *priority = calloc(n, sizeof(*priority));
    CHECK(bottom && priority);
    set_widescreen(0);
    bottom[0] = bottom[(SYS24_FB_HEIGHT - 1) * SYS24_FB_WIDTH] = 0xffff0000u;
    for (i = 0; i < 2; i++) {
        mats[i].flags = MODEL2_GEO_TEX_TEXTURED | MODEL2_GEO_TEX_CUTOUT | MODEL2_GEO_TEX_MIRROR_X;
        mats[i].patch_w = mats[i].patch_h = 32;
        mats[i].colorbase = (u16)(i + 10);
        mats[i].z_sort = (float)(i + 1);
    }
    CHECK(vita_gxm_open() == 0);
    if (argc > 1) {
        unsigned frames = 200;
        if (strcmp(argv[1], "--snapshot") == 0) {
            CHECK(argc == 3);
            snapshot = fopen(argv[2], "wb");
            CHECK(snapshot);
            frames = 1;
            CHECK(vita_gxm_present(bottom, priority, 0, 1, 0) == 0);
        } else {
            CHECK(argc == 2 && (strcmp(argv[1], "--benchmark") == 0
                || strcmp(argv[1], "--benchmark-varying") == 0));
        }
        if (strcmp(argv[1], "--benchmark-varying") != 0)
            for (i = 0; i < 6; i++)
                mesh[i * 5u + 2u] = 1;
        set_stress_mesh(8192, 8);
        for (i = 0; i < frames; i++) {
            order_count = 0;
            CHECK(vita_gxm_present(bottom, priority, 0, i == 0, 0) == 0);
        }
        if (snapshot) {
            set_stress_mesh(14, 14);
            stress_mats[1].flags &= ~MODEL2_GEO_TEX_TEXTURED;
            stress_mats[2].lumabase = 7;
            stress_mats[3].luma = 3;
            stress_mats[4].flags |= MODEL2_GEO_TEX_MIRROR_Y;
            stress_mats[5].flags |= MODEL2_GEO_TEX_CHECKER;
            stress_mats[6].patch_w = stress_mats[6].patch_h = 0;
            stress_mats[7].patch_x = 13;
            stress_mats[8].patch_y = 19;
            stress_mesh[9u * 15u] = -2000;
            stress_mesh[10u * 15u + 2u] = -1;
            stress_mats[11].flags &= ~MODEL2_GEO_TEX_TEXTURED;
            stress_mats[12].flags &= ~MODEL2_GEO_TEX_TEXTURED;
            stress_mats[12].flags |= MODEL2_GEO_TEX_CHECKER;
            stress_mats[13].patch_w = 33; stress_mats[13].patch_h = 17;
            CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
            palette_bias = 17; gen++;
            CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
            CHECK(fclose(snapshot) == 0);
            snapshot = NULL;
        }
        if (argc == 3)
            printf("Snapshot written: %s\n", argv[2]);
        else
            printf("frames=%u triangles/frame=%u draws/frame=%u vertices/frame=%u\n",
                frames, stress_triangles, geometry_draws / frames, geometry_vertices / frames);
        vita_gxm_shutdown();
        free(stress_mesh); free(stress_mats); free(bottom); free(priority);
        return 0;
    }
    vita_settings_defaults(&g_vita_menu.settings);
    vita_gxm_menu(0);
    CHECK(menu_highlights == 1 && menu_labels == 4 && glyph_draws > 100);
    g_vita_menu.options = 1;
    g_vita_menu.selection = 13;
    vita_gxm_menu(1);
    CHECK(menu_highlights == 2 && menu_labels == 4 + VITA_OPTION_COUNT);
    for (i = 0; i < VITA_OPTION_COUNT; ++i) {
        unsigned highlights = menu_highlights, labels = menu_labels;
        g_vita_menu.selection = (int)i;
        vita_gxm_menu(1);
        CHECK(menu_highlights == highlights + 1 && menu_labels == labels + VITA_OPTION_COUNT);
    }
    memset(g_vita_menu.status, 'W', sizeof(g_vita_menu.status) - 1);
    vita_gxm_menu(1);
    g_vita_menu.status[0] = 0;
    vita_menu_open(&g_vita_menu);
    CHECK(vita_gxm_present(bottom, priority, 1, 1, 0) == 0);
    CHECK(first_tile == 0xff0000ffu && last_tile == first_tile && geometry_draws == 0);
    expected_subdivisions = 4;
    CHECK(vita_gxm_present(bottom, priority, 0, 1, 0) == 0);
    expected_subdivisions = 0;
    CHECK(geometry_draws == 2 && geometry_vertices == 96);
    CHECK(order[0] == 11 && order[1] == 10);
    CHECK(first_tile == 0xff0000ffu && texture_draws == 2 && !locked);
    CHECK(arena_allocations == 1 && indexed_allocations == 0 && texel_reads == 1024);
    no_projection = 1;
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
    CHECK(geometry_draws == 2 && !locked);
    no_projection = 0;
    no_mesh = 1;
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
    CHECK(geometry_draws == 2 && !locked);
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 1) == 0);
    CHECK(geometry_draws == 2);
    no_mesh = 0; gen++; set_widescreen(1);
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
    CHECK(geometry_draws == 4 && waits >= 4);
    {
        unsigned before = geometry_vertices;
        for (i = 0; i < 6; i++)
            mesh[i * 5u + 2u] = 1;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(geometry_vertices == before + 6u && geometry_draws == 6);
    }
    {
        unsigned draws = geometry_draws, vertices = geometry_vertices, reads = texel_reads;
        set_stress_mesh(8192, 8);
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(geometry_draws == draws + 1024 && geometry_vertices == vertices + 24576);
        CHECK(order_count == 1024 && texel_reads == reads);
        for (i = 0; i < order_count; i++)
            CHECK(order[i] == ((10u + i) & 255u));
        draws = geometry_draws; vertices = geometry_vertices;
        set_stress_mesh(24000, 24000);
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(geometry_draws == draws + 2 && geometry_vertices == vertices + 72000);
        CHECK(order_count == 2 && order[0] == 10 && order[1] == 10);
        draws = geometry_draws; vertices = geometry_vertices;
        set_stress_mesh(4, 4);
        stress_mats[1].flags &= ~MODEL2_GEO_TEX_TEXTURED;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(geometry_draws == draws + 3 && geometry_vertices == vertices + 12);
        CHECK(order_count == 3 && order[0] == 10 && order[1] == 255 && order[2] == 10);
        set_stress_mesh(2, 2);
        palette_bias = 17;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(order_count == 1 && order[0] == 27 && texel_reads == reads);
        palette_bias = 0;
        draws = geometry_draws; vertices = geometry_vertices;
        set_stress_mesh(2, 1);
        stress_mats[0].flags &= ~MODEL2_GEO_TEX_TEXTURED;
        stress_mats[1].flags &= ~MODEL2_GEO_TEX_TEXTURED;
        stress_mats[1].flags |= MODEL2_GEO_TEX_CHECKER;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(geometry_draws == draws + 2 && geometry_vertices == vertices + 6);
        CHECK(checker_draws == 1 && order[0] == 255 && order[1] == 255);
        set_widescreen(0);
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(checker_draws == 2);
        set_widescreen(1);
        set_stress_mesh(3, 1);
        for (i = 0; i < stress_triangles; i++) {
            stress_mats[i].patch_x = (u16)(17u * i);
            stress_mats[i].patch_y = (u16)i;
            stress_mats[i].patch_w = (u16)(33u + i);
            stress_mats[i].patch_h = (u16)(17u + i);
        }
        gen++; check_source_pixels = 1;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(texel_reads == reads + 33u * 17u + 34u * 18u + 35u * 19u);
        check_source_pixels = 0; reads = texel_reads;
        set_stress_mesh(256, 1);
        for (i = 0; i < stress_triangles; i++) {
            stress_mats[i].patch_x = (u16)((i * 17u) & 2047u);
            stress_mats[i].patch_y = (u16)i;
        }
        gen++; texel_bias = 3; check_source_pixels = 1;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(texel_reads == reads + 256u * 1024u && arena_allocations == 1);
        check_source_pixels = 0; texel_bias = 0;
        set_stress_mesh(257, 1);
        for (i = 0; i < stress_triangles; i++) {
            stress_mats[i].patch_x = (u16)((i * 17u) & 2047u);
            stress_mats[i].patch_y = (u16)i;
        }
        gen++;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == -1 && !locked);
        CHECK(order_count == 256);
        set_stress_mesh(2049, 1);
        gen++;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == -1 && !locked);
        CHECK(order_count == 2048);
        set_stress_mesh(3, 1);
        for (i = 0; i < stress_triangles; i++) {
            stress_mats[i].patch_w = stress_mats[i].patch_h = 4096;
            stress_mats[i].patch_x = (u16)i;
        }
        gen++; reads = texel_reads;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == -1 && !locked);
        CHECK(order_count == 2 && texel_reads == reads + 32u * 1024u * 1024u);
        CHECK(arena_allocations == 1 && indexed_allocations == 0);
        free(stress_mesh); free(stress_mats);
        stress_mesh = NULL; stress_mats = NULL; stress_triangles = 0;
    }
    {
        unsigned vertices = geometry_vertices;
        for (i = 0; i < 2; i++) {
            float *tri = mesh + i * 15u;
            tri[0] = -200; tri[1] = -100; tri[2] = 1;
            tri[5] = 200; tri[6] = -100; tri[7] = 10;
            tri[10] = 0; tri[11] = 100; tri[12] = 1;
        }
        expected_subdivisions = 8;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(geometry_vertices == vertices + 384u);
        expected_subdivisions = 0;
    }
    {
        unsigned draws = geometry_draws;
        float saved_depth = mats[0].z_sort;
        mats[0].z_sort = NAN;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == -1 && !locked);
        CHECK(geometry_draws == draws);
        mats[0].z_sort = saved_depth;
    }
    no_mesh = 1;
    {
        unsigned draws = texture_draws, parts = priority_part_draws;
        priority[2 * SYS24_FB_WIDTH + 1] = 0xff0000ffu;
        priority[5 * SYS24_FB_WIDTH + 7] = 0xff00ff00u;
        CHECK(vita_gxm_present(bottom, priority, 0, 1, 0) == 0);
        CHECK(texture_draws == draws + 1 && priority_part_draws == parts + 1);
        CHECK(priority_crop[2] == 1 && priority_crop[3] == 2
            && priority_crop[4] == 7 && priority_crop[5] == 4);
        CHECK(priority_crop[6] == 544.0f / SYS24_FB_HEIGHT
            && priority_crop[7] == priority_crop[6]);
        priority[2 * SYS24_FB_WIDTH + 1] = 0;
        priority[5 * SYS24_FB_WIDTH + 7] = 0;
        CHECK(vita_gxm_present(bottom, priority, 0, 1, 0) == 0);
        CHECK(texture_draws == draws + 2 && priority_part_draws == parts + 1);
    }
    no_mesh = 0;
    exhaust_pool = 1;
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == -1);
    CHECK(!locked);
    vita_gxm_shutdown();
    vita_gxm_shutdown();
    CHECK(!pool && !pending && !arena && !arena_mapped);
    exhaust_pool = 0;
    for (i = 0; i < 4; i++) {
        fail_arena = i == 0; fail_base = i == 1; fail_map = i == 2; fail_texture = i == 3;
        gen++;
        CHECK(vita_gxm_open() == 0);
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == -1 && !locked);
        vita_gxm_shutdown();
        CHECK(!pool && !pending && !arena && !arena_mapped);
    }
    fail_arena = fail_base = fail_map = fail_texture = 0;
    fail_font = 1;
    CHECK(vita_gxm_open() == -1);
    CHECK(!pool && !pending);
    fail_font = 0;
    fail_checker = 1;
    CHECK(vita_gxm_open() == -1);
    CHECK(!pool && !pending);
    fail_checker = 0;
    CHECK(vita_gxm_open() == 0);
    vita_gxm_message("ROM load failed", "Check board dumps in ux0:data/segamod2/ROMS/srallyc-b/");
    vita_gxm_shutdown();
    CHECK(!pool && !pending);
    free(bottom); free(priority);
    puts("Vita GXM lifetime, cache collisions/caps, ordered batches, arena and failure tests passed");
    return 0;
}
