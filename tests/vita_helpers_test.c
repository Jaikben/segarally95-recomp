#include "gxm_math.h"
#include "controls.h"
#include "bitmap_font.h"
#include "model2_geo_order.h"
#include "model2_geo_tex.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); \
} } while (0)

static void test_clipping(void)
{
    vita_camera_vertex_t in[3] = {{0, 0, 1, 0, 0}, {1, 0, 1, 8, 0}, {0, 1, -1, 0, 8}};
    vita_camera_vertex_t out[4];
    vita_screen_vertex_t screen[12];
    model2_geo_projection_t p = {1, 1, {0, 0, 496, 384}, {248, 192}, 1};
    unsigned n, i;
    n = vita_clip_near(in, out);
    CHECK(n == 4);
    for (i = 0; i < n; i++) {
        CHECK(out[i].z >= 0.01f);
        screen[i] = vita_project(out[i], &p);
    }
    n = vita_clip_screen(screen, n, &p);
    CHECK(n >= 3 && n <= 8);
    for (i = 0; i < n; i++) {
        CHECK(screen[i].x >= -0.001f && screen[i].x <= 496.001f);
        CHECK(screen[i].y >= -0.001f && screen[i].y <= 384.001f);
        CHECK(isfinite(screen[i].uq / screen[i].q));
    }
    for (i = 0; i < 3; i++)
        in[i].z = -1;
    CHECK(vita_clip_near(in, out) == 0);
    for (i = 0; i < 3; i++)
        in[i].z = 0.01f;
    CHECK(vita_clip_near(in, out) == 3);
    screen[0] = (vita_screen_vertex_t){-40, -20, 1, 1, 2};
    screen[1] = (vita_screen_vertex_t){-10, -20, 1, 1, 2};
    screen[2] = (vita_screen_vertex_t){-40, -10, 1, 1, 2};
    CHECK(vita_clip_screen(screen, 3, &p) == 0);
}

static void test_perspective(void)
{
    vita_screen_vertex_t v[3] = {{0, 0, 1, 0, 0}, {100, 0, 0.1f, 10, 0}, {0, 100, 1, 0, 100}};
    vita_screen_vertex_t mid = vita_barycentric(v, 0.5f, 0);
    CHECK(fabsf(mid.x - 50) < 0.0001f);
    CHECK(fabsf(mid.uq / mid.q - 100.0f / 11.0f) < 0.0001f);
    CHECK(fabsf(mid.uq / mid.q - 50.0f) > 40.0f);
    CHECK(vita_swap_rb(0x12345678u) == 0x12785634u);
    CHECK(vita_swap_rb(vita_swap_rb(0xff123456u)) == 0xff123456u);
}

static void test_controls(void)
{
    vita_controls_t state = {0, 0, 1};
    vita_actions_t a = vita_controls_update(&state, VITA_CROSS);
    CHECK(a.held == 0);
    vita_controls_update(&state, 0);
    a = vita_controls_update(&state, VITA_SELECT);
    CHECK(a.held == VITA_SELECT);
    a = vita_controls_update(&state, VITA_START | VITA_SELECT);
    CHECK(a.pause_changed && state.paused && a.held == 0);
    a = vita_controls_update(&state, VITA_START | VITA_SELECT);
    CHECK(!a.pause_changed && state.paused);
    vita_controls_update(&state, 0);
    a = vita_controls_update(&state, VITA_CROSS);
    CHECK(!a.pause_changed && state.paused && a.pressed == VITA_CROSS && a.held == 0);
    state.paused = 0;
    state.wait_release = 1;
    a = vita_controls_update(&state, VITA_CROSS);
    CHECK(a.held == 0);
    vita_controls_update(&state, 0);
    a = vita_controls_update(&state, VITA_L);
    CHECK(a.pressed == VITA_L);
    a = vita_controls_update(&state, VITA_L);
    CHECK(a.pressed == 0 && a.held == VITA_L);
    vita_controls_update(&state, VITA_START | VITA_SELECT);
    vita_controls_update(&state, 0);
    a = vita_controls_update(&state, VITA_CIRCLE);
    CHECK(a.pressed == VITA_CIRCLE && state.paused && a.held == 0);
    vita_controls_update(&state, 0);
    a = vita_controls_update(&state, VITA_TRIANGLE);
    CHECK(a.pressed == VITA_TRIANGLE && !a.test && state.paused && a.held == 0);
    CHECK(vita_controls_steer(&state, 0, 0) == 128);
    state.paused = 0;
    vita_controls_update(&state, 0);
    CHECK(vita_controls_steer(&state, 0, 0) == 0);
    CHECK(vita_controls_steer(&state, 255, 0) == 255);
    CHECK(vita_controls_steer(&state, 115, 0) == 115);
    CHECK(vita_controls_steer(&state, 116, 0) == 128);
    CHECK(vita_controls_steer(&state, 140, 0) == 128);
    CHECK(vita_controls_steer(&state, 141, 0) == 141);
    CHECK(vita_controls_steer(&state, 0, 1) == 128);
}

static void test_order(void)
{
    model2_geo_tri_mat_t a = {0}, b = {0};
    a.z_sort = 1;
    b.z_sort = 10;
    CHECK(model2_geo_zval(-1) == 0);
    CHECK(model2_geo_zval(1) == 0xffffu);
    CHECK(model2_geo_depth_compare(&a, 0, &b, 1) < 0);
    b.z_sort = a.z_sort;
    CHECK(model2_geo_depth_compare(&a, 2, &b, 1) < 0);
    CHECK(model2_geo_depth_compare(&a, 1, &b, 1) == 0);
    a.pad = 1;
    CHECK(model2_geo_order_compare(&a, 0, &b, 1) > 0);
    a.pad = 0;
    a.flags = MODEL2_GEO_TEX_TEXTURED;
    CHECK(model2_geo_order_compare(&a, 0, &b, 1) > 0);
    b.flags = MODEL2_GEO_TEX_TEXTURED | MODEL2_GEO_TEX_CUTOUT;
    CHECK(model2_geo_order_compare(&a, 0, &b, 1) < 0);
}

static void test_bitmap_font(void)
{
    unsigned c, row;
    CHECK(vita_font_character('a') == 'A');
    CHECK(vita_font_character(0) == '?');
    CHECK(vita_font_character(255) == '?');
    CHECK(vita_font_row('A', 4) == 31);
    CHECK(vita_font_row('A', 7) == 0);
    for (c = 32; c <= 126; ++c) {
        unsigned ink = 0;
        for (row = 0; row < 7; ++row) {
            unsigned bits = vita_font_row(c, row);
            CHECK(bits <= 31);
            ink |= bits;
            if (c >= 'a' && c <= 'z')
                CHECK(bits == vita_font_row(c - ('a' - 'A'), row));
        }
        CHECK(c == ' ' ? ink == 0 : ink != 0);
    }
}

int main(void)
{
    test_clipping();
    test_perspective();
    test_controls();
    test_order();
    test_bitmap_font();
    puts("Vita helper tests passed");
    return 0;
}
