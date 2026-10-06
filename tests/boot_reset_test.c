#include "model2_rom.h"
#include "i960_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

MODEL2_HOST_ALIGN u8 model2_maincpu_rom[MAINCPU_SIZE];
MODEL2_HOST_ALIGN u8 model2_crx_ram[0x40000];
MODEL2_HOST_ALIGN u8 model2_workram[WORKRAM_SIZE];
u8 model2_cpu_wait[0x38];
u8 *model2_main_data_rom;
u32 model2_main_data_size;

extern void maincpu_reset_entry(u32, u32, u32);

u32 i960_mmio_read_u8(u32 address)
{
    CHECK(address == 0xf80000u);
    return 0;
}

void i960_mmio_write_u8(u32 address, u8 value)
{
    CHECK(address == 0xf80000u && value == 2);
}

void i960_st_u32(i960_space space, u32 base, u32 offset, u32 value)
{
    CHECK(space == I960_WORKRAM && base == 0x201e54u && offset == 0);
    memcpy(model2_crx_ram + 0x1e54u, &value, sizeof(value));
}

void *i960_vaddr_ptr(u32 address)
{
    CHECK(address == 0xff000010u);
    return (void *)(uintptr_t)address;
}

void i960_synmovq(uintptr_t source, uintptr_t destination)
{
    CHECK(source == 0xff000010u);
    CHECK(destination == (uintptr_t)(model2_maincpu_rom + 0x3d0u));
}

static void reset_case(unsigned words)
{
    u8 data[8] = { 'C', 'G', 'M', ' ', '1', '.', '0', ' ' };
    u32 sentinel = 0xffffffffu;
    unsigned i;
    memset(model2_maincpu_rom, 0x5a, sizeof(model2_maincpu_rom));
    memset(model2_cpu_wait, 0xa5, sizeof(model2_cpu_wait));
    for (i = 0; i < words; i++) {
        u32 value = i == 0 ? 0xffu : 0x12340000u + i;
        memcpy(model2_maincpu_rom + 0x3e0u + i * 4u, &value, sizeof(value));
    }
    memcpy(model2_maincpu_rom + 0x3e0u + words * 4u, &sentinel, sizeof(sentinel));
    model2_main_data_rom = data;
    model2_main_data_size = sizeof(data);
    g8 = 0x12345678u;
    maincpu_reset_entry(0, 0, 0);
    CHECK(model2_main_data_rom == data && model2_main_data_size == sizeof(data));
    CHECK(memcmp(model2_cpu_wait, model2_maincpu_rom + 0x3e0u, words * 4u) == 0);
    for (i = words * 4u; i < sizeof(model2_cpu_wait); i++)
        CHECK(model2_cpu_wait[i] == 0xa5);
    CHECK(memcmp(model2_crx_ram + 0x1e40u, model2_maincpu_rom + 0xb0u, 0x14u) == 0);
    {
        u32 crx_base;
        memcpy(&crx_base, model2_crx_ram + 0x1e54u, sizeof(crx_base));
        CHECK(crx_base == (u32)(uintptr_t)model2_crx_ram);
    }
    CHECK(memcmp(model2_crx_ram + 0x1e58u, model2_maincpu_rom + 0xc8u, 44u * 4u - 0x18u) == 0);
    CHECK(memcmp(model2_crx_ram, model2_maincpu_rom + 0x1030u, 0x101u * 4u) == 0);
    CHECK(memcmp(model2_workram + 0xa0000u, model2_maincpu_rom + 0x1000u, 0x60000u) == 0);
    CHECK(g8 == 0x12345678u);
}

int main(void)
{
    reset_case(sizeof(model2_cpu_wait) / 4u);
    reset_case(1);
#if !defined(_WIN32)
    {
        int status;
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            memset(model2_maincpu_rom, 0, sizeof(model2_maincpu_rom));
            maincpu_reset_entry(0, 0, 0);
            _exit(0);
        }
        CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    }
#endif
    puts("Boot wait-table sentinel, ROM-state preservation and bounds passed");
    return 0;
}
