#ifndef VITA_ROM_DIAGNOSTIC_H
#define VITA_ROM_DIAGNOSTIC_H

#include "model2_rom.h"
#include "startup_log.h"

#include <stdio.h>
#include <string.h>

static void vita_rom_diagnostic(const char *stage)
{
    const u8 *mapped = model2_rom_at(0x02879db0u);
    const u8 *raw = NULL;
    char message[256];
    if (model2_main_data_rom && model2_main_data_size >= 0x00879db8u)
        raw = model2_main_data_rom + 0x00879db0u;
    snprintf(message, sizeof(message),
             "runtime: ROM %s data=%p size=%u mapped=%p raw=%p header_ok=%d bytes=%02x/%02x/%02x/%02x/%02x/%02x/%02x/%02x\n",
             stage, (void *)model2_main_data_rom, (unsigned)model2_main_data_size,
             (const void *)mapped, (const void *)raw,
             raw && mapped == raw && memcmp(raw, "CGM 1.0 ", 8) == 0,
             raw ? raw[0] : 0, raw ? raw[1] : 0,
             raw ? raw[2] : 0, raw ? raw[3] : 0,
             raw ? raw[4] : 0, raw ? raw[5] : 0,
             raw ? raw[6] : 0, raw ? raw[7] : 0);
    vita_startup_log(message);
}

#endif
