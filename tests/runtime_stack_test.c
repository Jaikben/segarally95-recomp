#include "model2_rom.h"
#include "i960_mem.h"
#include "cgm_format.h"
#include "placement_catalog_feed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

MODEL2_HOST_ALIGN u8 model2_maincpu_rom[MAINCPU_SIZE];
MODEL2_HOST_ALIGN u8 model2_workram[WORKRAM_SIZE];
static u8 catalog[32] = "CGM 1.0 ";
static u32 mode, batches;
static unsigned printf_calls;

extern u32 catalog_draw_setup(u32, u32, u32);
extern void geo_draw_frame_entry(u32, u32, u32);
extern void geo_view_mode_apply(u32, u32, u32);
extern void libc_printf(const char *, u32, u32);

const u8 *model2_rom_at(u32 address)
{
    return address >= MAIN_DATA_A && address < MAIN_DATA_A + sizeof(catalog)
        ? catalog + address - MAIN_DATA_A : NULL;
}
u32 i960_ld_u32(i960_space space, u32 base, u32 offset)
{
    (void)space; (void)offset;
    return base == 0x2142c8u ? mode : base == 0x20c954u ? batches : 0;
}
u32 i960_ld_u16(i960_space s, u32 b, u32 o) { (void)s; (void)b; (void)o; return 0; }
u32 i960_ld_u8(i960_space s, u32 b, u32 o)
{ (void)s; (void)o; return b == CGM_HEADER_TEMPLATE ? 'C' : b == 0x20201au ? 1 : 0; }
void i960_st_u32(i960_space s, u32 b, u32 o, u32 v) { (void)s; (void)b; (void)o; (void)v; }
void i960_st_u16(i960_space s, u32 b, u32 o, u16 v) { (void)s; (void)b; (void)o; (void)v; }
void i960_st_u8(i960_space s, u32 b, u32 o, u8 v) { (void)s; (void)b; (void)o; (void)v; }
void i960_mmio_write_u32(u32 a, u32 v) { (void)a; (void)v; }
u32 i960_mmio_read_u32(u32 a) { (void)a; return 0; }
u32 cgm_catalog_stage(u32 a) { return a; }
void cgm_leading_colorbase(void *a, void *b, u32 c) { (void)a; (void)b; (void)c; }
void cgm_record_dispatch(u32 a, u32 b) { (void)a; (void)b; }
u32 draw_scene_dispatch(u32 a, u32 b, u32 c) { (void)a; (void)b; (void)c; return 0; }
void tile_cursor_seed(u32 a, u32 b) { (void)a; (void)b; }
void geo_fifo_bootstrap(u32 a, u32 b, u32 c) { (void)a; (void)b; (void)c; }
u32 geo_view_trig_scale(u32 a, u32 b, u32 c) { (void)a; (void)b; (void)c; return 0; }
int i960_host_scene_seeded(void) { return 0; }
int placement_catalog_feed_source(void) { return 0; }
unsigned placement_catalog_feed_env_batch(void) { return 0; }
void placement_catalog_feed_track_batch(u32 a, u32 b) { (void)a; (void)b; }
void placement_catalog_feed_race_batch(u32 a, u32 b) { (void)a; (void)b; }
void *libc_printf_dispatch(void *format, u32 arg1, u32 arg2)
{
    const u64 *scratch = (const u64 *)g13;
    CHECK(scratch[0] == (u64)(uintptr_t)format);
    CHECK(scratch[2] == (u64)g4 && scratch[4] == (u64)g8);
    CHECK(g1 == arg1 && g2 == arg2);
    printf_calls++;
    return NULL;
}

int main(void)
{
    static u64 frame[64];
    uintptr_t stack = (uintptr_t)frame;
    unsigned i;
    sp = fp = stack;
    libc_printf("test", 7, 9);
    CHECK(sp == stack && fp == stack);
    for (i = 0; i < 10000; i++) {
        geo_draw_frame_entry(0, 0, 0);
        CHECK(sp == stack && fp == stack);
        mode = i % 5u;
        geo_view_mode_apply(0, 0, 0);
        CHECK(sp == stack);
        batches = 0;
        catalog[0] = 'C';
        g4 = 4;
        CHECK(catalog_draw_setup(0, 0, MAIN_DATA_A) == 0);
        CHECK(sp == stack);
        catalog[0] = 0;
        CHECK(catalog_draw_setup(0, 0, MAIN_DATA_A) == 0xffffffffu);
        CHECK(sp == stack && fp == stack);
        batches = 512;
        CHECK(catalog_draw_setup(0, 0, MAIN_DATA_A) == 0xffffffffu);
        CHECK(sp == stack && fp == stack);
        libc_printf("test", 7, 9);
        CHECK(sp == stack && fp == stack);
    }
    CHECK(printf_calls == 30001);
    puts("10,000 repeated geometry, view, catalog and formatter calls preserve stack state");
    return 0;
}
