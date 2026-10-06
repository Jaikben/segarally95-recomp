#include "gxm_renderer.h"
#include "model2_geo_tex.h"
#include "sys24_tile.h"
#include "menu.h"
#include "bitmap_font.h"
#include <vita2d.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)
static unsigned char *pool;
static unsigned pool_size, pool_used, pending, waits, locked, gen;
static unsigned texture_draws, geometry_draws, geometry_vertices, order[32], order_count;
static unsigned first_tile, last_tile;
static int no_mesh, no_projection, exhaust_pool, wide, fail_font;
static unsigned menu_highlights, menu_labels;
static unsigned glyph_draws;
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
    if (fail_font && w == VITA_FONT_WIDTH && h == VITA_FONT_HEIGHT) {
        free(t);
        return NULL;
    }
    CHECK(t);
    t->w = w; t->h = h; t->format = format;
    t->stride = ((w + 31u) & ~31u) * (format == SCE_GXM_TEXTURE_FORMAT_P8_ABGR ? 1 : 4);
    t->data = calloc(h, t->stride);
    CHECK(t->data);
    return t;
}
vita2d_texture *vita2d_create_empty_texture(unsigned w, unsigned h)
{ return vita2d_create_empty_texture_format(w, h, 999); }
void vita2d_texture_set_filters(vita2d_texture *t, unsigned a, unsigned b) { (void)t; (void)a; (void)b; }
void vita2d_free_texture(vita2d_texture *t) { CHECK(!pending); free(t->data); free(t); }
unsigned vita2d_texture_get_stride(const vita2d_texture *t) { return t->stride; }
void *vita2d_texture_get_datap(const vita2d_texture *t) { CHECK(!pending); return t->data; }
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
    CHECK(locked && p);
    CHECK(t->gxm_tex.u == SCE_GXM_TEXTURE_ADDR_MIRROR);
    CHECK(t->gxm_tex.v == SCE_GXM_TEXTURE_ADDR_REPEAT);
    CHECK((p[15] >> 24) == 0);
    CHECK(order_count < 32);
    order[order_count++] = p[0] & 255u;
    for (i = 0; i < n; i++)
        CHECK(isfinite(v[i].x) && isfinite(v[i].y) && isfinite(v[i].u) && isfinite(v[i].v));
    geometry_draws++;
    geometry_vertices += (unsigned)n;
}
void vita2d_draw_array(unsigned m, const vita2d_color_vertex *v, size_t n)
{ (void)m; (void)v; (void)n; CHECK(locked); }
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
    *v = mesh; *nv = 6; *m = mats; *nt = 2;
    return 0;
}
void model2_geo_unlock(void) { CHECK(locked); locked = 0; }
int model2_host_aspect_is_widescreen(void) { return wide; }
u32 model2_tex_sheets_dirty_gen(void) { return gen; }
const u32 *model2_tex_sheet_bank(unsigned s) { static u32 bank; (void)s; return &bank; }
u16 model2_get_texel(const u32 *s, u32 bx, u32 by, int x, int y)
{ (void)s; (void)bx; (void)by; return (u16)((x + y) & 15); }
void model2_palette_build_texel_lut(u32 cb, u32 lb, u32 luma, int cutout, u8 rgba[64])
{
    unsigned i; (void)lb; (void)luma;
    for (i = 0; i < 16; i++) {
        rgba[i*4] = (u8)cb; rgba[i*4+1] = 2; rgba[i*4+2] = 3;
        rgba[i*4+3] = cutout && i == 15 ? 0 : 255;
    }
}
void model2_palette_lookup_solid(u32 cb, u32 luma, u8 rgb[3])
{ (void)cb; (void)luma; memset(rgb, 255, 3); }

int main(void)
{
    unsigned n = SYS24_FB_WIDTH * SYS24_FB_HEIGHT, i;
    u32 *bottom = calloc(n, sizeof(*bottom)), *priority = calloc(n, sizeof(*priority));
    CHECK(bottom && priority);
    bottom[0] = bottom[(SYS24_FB_HEIGHT - 1) * SYS24_FB_WIDTH] = 0xffff0000u;
    for (i = 0; i < 2; i++) {
        mats[i].flags = MODEL2_GEO_TEX_TEXTURED | MODEL2_GEO_TEX_CUTOUT | MODEL2_GEO_TEX_MIRROR_X;
        mats[i].patch_w = mats[i].patch_h = 32;
        mats[i].colorbase = (u16)(i + 10);
        mats[i].z_sort = (float)(i + 1);
    }
    CHECK(vita_gxm_open() == 0);
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
    CHECK(vita_gxm_present(bottom, priority, 0, 1, 0) == 0);
    CHECK(geometry_draws == 2 && geometry_vertices == 96);
    CHECK(order[0] == 11 && order[1] == 10);
    CHECK(first_tile == 0 && texture_draws == 3 && !locked);
    no_projection = 1;
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
    CHECK(geometry_draws == 2 && !locked);
    no_projection = 0;
    no_mesh = 1;
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
    CHECK(geometry_draws == 2 && !locked);
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 1) == 0);
    CHECK(geometry_draws == 2);
    no_mesh = 0; gen++; wide = 1;
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
    CHECK(geometry_draws == 4 && waits >= 4);
    {
        unsigned before = geometry_vertices;
        for (i = 0; i < 6; i++)
            mesh[i * 5u + 2u] = 1;
        CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == 0);
        CHECK(geometry_vertices == before + 6u && geometry_draws == 6);
    }
    exhaust_pool = 1;
    CHECK(vita_gxm_present(bottom, priority, 0, 0, 0) == -1);
    CHECK(!locked);
    vita_gxm_shutdown();
    vita_gxm_shutdown();
    CHECK(!pool && !pending);
    fail_font = 1;
    CHECK(vita_gxm_open() == -1);
    CHECK(!pool && !pending);
    fail_font = 0;
    CHECK(vita_gxm_open() == 0);
    vita_gxm_message("ROM load failed", "Check board dumps in ux0:data/segamod2/ROMS/srallyc-b/");
    vita_gxm_shutdown();
    CHECK(!pool && !pending);
    free(bottom); free(priority);
    puts("Vita GXM lifetime, composition, tessellation and failure tests passed");
    return 0;
}
