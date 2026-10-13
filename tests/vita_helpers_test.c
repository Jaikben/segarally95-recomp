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
    test_controls();
    test_order();
    test_bitmap_font();
    puts("Vita helper tests passed");
    return 0;
}
