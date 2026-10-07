#include "gxm_math.h"
#include "gxm_order.h"
#include "controls.h"
#include "bitmap_font.h"
#include "model2_geo_order.h"
#include "model2_geo_tex.h"

#include <stdio.h>
#include <stdlib.h>
#include <float.h>

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
    CHECK(vita_perspective_subdivisions(v, 1, 1) == 4);
    CHECK(vita_perspective_subdivisions(v, 4, 4) == 8);
    CHECK(vita_perspective_subdivisions(v, 3.2f, 3.2f) == 4);
    v[1].q = v[2].q = v[0].q;
    CHECK(vita_perspective_subdivisions(v, 4, 4) == 1);
    v[1].q = 1.0f / 3.0f;
    CHECK(vita_perspective_subdivisions(v, 4, 4) == 4);
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

static const model2_geo_tri_mat_t *reference_mats;
static int reference_painter_compare(const void *va, const void *vb)
{
    unsigned a = *(const unsigned *)va, b = *(const unsigned *)vb;
    if ((reference_mats[a].pad & 1u) != (reference_mats[b].pad & 1u))
        return (reference_mats[a].pad & 1u) ? 1 : -1;
    return -model2_geo_depth_compare(&reference_mats[a], a, &reference_mats[b], b);
}

static uint32_t random_state = 1234567u;
static uint32_t next_random(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static void test_painter_sort(void)
{
    static const unsigned sizes[] = {0, 1, 2, 31, 32, 33, 256, 8192};
    model2_geo_tri_mat_t *mats = calloc(8192, sizeof(*mats));
    vita_painter_entry_t *entries = malloc(16384u * sizeof(*entries));
    unsigned *expected = malloc(8192u * sizeof(*expected));
    unsigned size, mode, i;
    CHECK(mats && entries && expected);
    reference_mats = mats;
    for (size = 0; size < sizeof(sizes) / sizeof(sizes[0]); size++)
        for (mode = 0; mode < 6; mode++) {
            unsigned n = sizes[size];
            const vita_painter_entry_t *actual;
            for (i = 0; i < n; i++) {
                uint32_t bits = next_random();
                if ((bits & 0x7f800000u) == 0x7f800000u)
                    bits ^= 0x00800000u;
                memcpy(&mats[i].z_sort, &bits, sizeof(bits));
                if (mode == 1) mats[i].z_sort = 1;
                if (mode == 2) mats[i].z_sort = (float)(i / 8u);
                if (mode == 3) mats[i].z_sort = (float)(n - i);
                if (mode == 4) {
                    static const float depths[] = {0, -0.0f, -1, 1, FLT_MIN, FLT_MAX};
                    mats[i].z_sort = depths[i % 6u];
                }
                if (mode == 5) {
                    bits &= 0x07ffffffu;
                    memcpy(&mats[i].z_sort, &bits, sizeof(bits));
                }
                mats[i].pad = mode == 1 ? 0 : (u16)(next_random() >> 16);
                entries[i].index = expected[i] = i;
                CHECK(vita_painter_key(&mats[i], &entries[i].key));
            }
            qsort(expected, n, sizeof(*expected), reference_painter_compare);
            actual = vita_painter_sort(entries, entries + 8192, n);
            for (i = 0; i < n; i++)
                CHECK(actual[i].index == expected[i]);
        }
    mats[0].z_sort = NAN;
    CHECK(!vita_painter_key(mats, &entries[0].key));
    mats[0].z_sort = INFINITY;
    CHECK(!vita_painter_key(mats, &entries[0].key));
    mats[0].z_sort = -INFINITY;
    CHECK(!vita_painter_key(mats, &entries[0].key));
    free(expected);
    free(entries);
    free(mats);
}

static unsigned reference_clip_near(const vita_camera_vertex_t in[3],
                                    vita_camera_vertex_t out[4])
{
    unsigned i, n = 0;
    for (i = 0; i < 3; i++) {
        const vita_camera_vertex_t *a = in + i, *b = in + (i + 1) % 3;
        if (a->z >= 0.01f)
            out[n++] = *a;
        if ((a->z >= 0.01f) != (b->z >= 0.01f)) {
            float t = (0.01f - a->z) / (b->z - a->z);
            out[n++] = (vita_camera_vertex_t){
                a->x + t * (b->x - a->x), a->y + t * (b->y - a->y),
                0.01f, a->u + t * (b->u - a->u), a->v + t * (b->v - a->v)
            };
        }
    }
    return n;
}

static unsigned reference_clip_screen(vita_screen_vertex_t *vertices, unsigned n,
                                      const model2_geo_projection_t *p)
{
    unsigned plane, i;
    for (plane = 0; plane < 4 && n; plane++) {
        vita_screen_vertex_t tmp[12];
        unsigned count = 0;
        float edge = (float)p->viewport[plane];
        for (i = 0; i < n; i++) {
            vita_screen_vertex_t a = vertices[i], b = vertices[(i + 1) % n];
            float av = (plane & 1) ? a.y : a.x, bv = (plane & 1) ? b.y : b.x;
            int ai = plane < 2 ? av >= edge : av <= edge;
            int bi = plane < 2 ? bv >= edge : bv <= edge;
            if (ai)
                tmp[count++] = a;
            if (ai != bi)
                tmp[count++] = vita_screen_mix(a, b, (edge - av) / (bv - av));
        }
        memcpy(vertices, tmp, count * sizeof(*vertices));
        n = count;
    }
    return n;
}

static void test_clipping_parity(void)
{
    unsigned trial, i;
    for (trial = 0; trial < 6000; trial++) {
        vita_camera_vertex_t in[3], near[4], expected_near[4];
        vita_screen_vertex_t actual[12], expected[12];
        model2_geo_projection_t p = {1, 1, {0, 0, 496, 384}, {248, 192}, 1};
        unsigned n, reference_n;
        p.viewport[0] = -(int)(next_random() % 64u);
        p.viewport[1] = -(int)(next_random() % 64u);
        for (i = 0; i < 3; i++) {
            in[i] = (vita_camera_vertex_t){
                (float)(int)(next_random() % 1201u) - 600,
                (float)(int)(next_random() % 1201u) - 600,
                (float)(int)(next_random() % 1001u) / 100.0f - 2,
                (float)(next_random() % 2048u), (float)(next_random() % 1024u)
            };
        }
        n = vita_clip_near(in, near);
        reference_n = reference_clip_near(in, expected_near);
        CHECK(n == reference_n && memcmp(near, expected_near, n * sizeof(*near)) == 0);
        for (i = 0; i < n; i++)
            actual[i] = expected[i] = vita_project(near[i], &p);
        n = vita_clip_screen(actual, n, &p);
        reference_n = reference_clip_screen(expected, reference_n, &p);
        CHECK(n == reference_n && memcmp(actual, expected, n * sizeof(*actual)) == 0);
    }
}

int main(void)
{
    test_clipping();
    test_perspective();
    test_controls();
    test_order();
    test_painter_sort();
    test_clipping_parity();
    test_bitmap_font();
    puts("Vita helper tests passed");
    return 0;
}
