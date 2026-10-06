#include "sys24_tile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

enum { PIXELS = SYS24_FB_WIDTH * SYS24_FB_HEIGHT, GUARD = 32 };

int main(void)
{
    static const u16 scrolls[] = { 0, 1, 127, 128, 255, 383, 496, 511 };
    u16 *map = calloc(0x8000u, sizeof(*map));
    u8 *chars = malloc(0x80000u);
    u16 *palette = calloc(0x2000u, sizeof(*palette));
    u32 *storage = malloc((PIXELS + 2 * GUARD) * sizeof(*storage));
    sys24_tile_state_t *tiles = sys24_tile_create(SYS24_TILE_MASK_M2);
    unsigned i, h, v, color;
    CHECK(map && chars && palette && storage && tiles);
    memset(chars, 0xff, 0x80000u);
    map[0x5004] = map[0x5005] = map[0x5007] = 0x8000u;
    sys24_tile_bind(tiles, (const u8 *)map, chars);
    for (color = 0; color < 256; color++) {
        for (i = 0; i < 0x1000u; i++)
            map[0x2000u + i] = (u16)(color << 7);
        palette[color * 16u + 15u] = 0x001fu;
        for (i = 0; i < PIXELS + 2 * GUARD; i++)
            storage[i] = 0xdeadbeefu;
        sys24_tile_draw_layers_rgb32(tiles, storage + GUARD,
                                    (const u8 *)palette, 0xff000000u, SYS24_PASS_BOTTOM);
        for (i = 0; i < PIXELS; i++) {
            CHECK(storage[GUARD + i] == 0xffff0000u);
        }
        for (i = 0; i < GUARD; i++) {
            CHECK(storage[i] == 0xdeadbeefu);
            CHECK(storage[GUARD + PIXELS + i] == 0xdeadbeefu);
        }
    }
    for (h = 0; h < sizeof(scrolls) / sizeof(scrolls[0]); h++) {
        for (v = 0; v < sizeof(scrolls) / sizeof(scrolls[0]); v++) {
            map[0x5002] = scrolls[h];
            map[0x5006] = scrolls[v];
            sys24_tile_draw_layers_rgb32(tiles, storage + GUARD,
                                        (const u8 *)palette, 0xff000000u, SYS24_PASS_BOTTOM);
            for (i = 0; i < PIXELS; i++)
                CHECK(storage[GUARD + i] == 0xffff0000u);
            for (i = 0; i < GUARD; i++) {
                CHECK(storage[i] == 0xdeadbeefu);
                CHECK(storage[GUARD + PIXELS + i] == 0xdeadbeefu);
            }
        }
    }
    map[0x5002] = 0x8000u;
    for (i = 0; i < SYS24_FB_HEIGHT; i++)
        map[0x4400u + i] = scrolls[i % (sizeof(scrolls) / sizeof(scrolls[0]))];
    sys24_tile_draw_layers_rgb32(tiles, storage + GUARD,
                                (const u8 *)palette, 0xff000000u, SYS24_PASS_BOTTOM);
    for (i = 0; i < PIXELS; i++)
        CHECK(storage[GUARD + i] == 0xffff0000u);
    for (i = 0; i < 0x1000u; i++)
        map[0x2000u + i] = 0xffffu;
    sys24_tile_draw_layers_rgb32(tiles, storage + GUARD,
                                (const u8 *)palette, 0xff000000u, SYS24_PASS_PRIORITY);
    for (i = 0; i < PIXELS; i++)
        CHECK(storage[GUARD + i] == 0xffff0000u);
    for (i = 0; i < SYS24_FB_HEIGHT * 4u; i++)
        map[0x6800u + i] = 0xaaaau;
    sys24_tile_draw_layers_rgb32(tiles, storage + GUARD,
                                (const u8 *)palette, 0xff000000u, SYS24_PASS_PRIORITY);
    for (i = 0; i < PIXELS; i++)
        CHECK(storage[GUARD + i] ==
              (((i % SYS24_FB_WIDTH) / 8u) & 1u ? 0xffff0000u : 0xff000000u));
    for (i = 0; i < SYS24_FB_HEIGHT * 4u; i++)
        map[0x6800u + i] = 0xffffu;
    sys24_tile_draw_layers_rgb32(tiles, storage + GUARD,
                                (const u8 *)palette, 0xff000000u, SYS24_PASS_PRIORITY);
    for (i = 0; i < PIXELS; i++)
        CHECK(storage[GUARD + i] == 0xff000000u);
    for (i = 0; i < GUARD; i++) {
        CHECK(storage[i] == 0xdeadbeefu);
        CHECK(storage[GUARD + PIXELS + i] == 0xdeadbeefu);
    }
    sys24_tile_destroy(tiles);
    free(storage);
    free(palette);
    free(chars);
    free(map);
    puts("Tile palette banks, scroll/wrap, line scroll, priority and masks passed");
    return 0;
}
