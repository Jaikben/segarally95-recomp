#ifndef SEGAMOD2_VITA_STARTUP_LOG_H
#define SEGAMOD2_VITA_STARTUP_LOG_H

#if defined(I960_HOST_VITA_GXM)
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <string.h>

static inline void vita_startup_log(const char *message)
{
    SceUID fd;
    int result, closed;
    size_t length = strlen(message);
    sceClibPrintf("segamod2: %s", message);
    fd = sceIoOpen("ux0:data/segamod2/vita.log",
                   SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd < 0) {
        sceClibPrintf("segamod2: startup log open failed: 0x%08x\n", (unsigned)fd);
        return;
    }
    result = sceIoWrite(fd, message, length);
    closed = sceIoClose(fd);
    if (result != (int)length || closed < 0)
        sceClibPrintf("segamod2: startup log write/close failed: %d/%d\n", result, closed);
}
#else
#include <stdio.h>
static inline void vita_startup_log(const char *message)
{
    fprintf(stderr, "segamod2: %s", message);
}
#endif

#endif
