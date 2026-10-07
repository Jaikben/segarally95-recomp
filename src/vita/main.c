#include "i960_host.h"
#include "i960_lift.h"
#include "model2_rom.h"
#include "track_viewer.h"
#include "sys24_viewer.h"
#include "gxm_renderer.h"
#include "controls.h"
#include "frontend.h"
#include "startup_log.h"
#include "rom_diagnostic.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

unsigned int _newlib_heap_size_user = 128 * 1024 * 1024;
static const char *const data_root = "ux0:data/segamod2";

static int prepare_storage(void)
{
    SceUID fd;
    int closed;
    sceClibPrintf("segamod2: entered main; preparing storage\n");
    if (sceIoMkdir("ux0:data", 0777) < 0) {
        SceIoStat st;
        int rc = sceIoGetstat("ux0:data", &st);
        if (rc < 0) {
            sceClibPrintf("segamod2: ux0:data unavailable: 0x%08x\n", (unsigned)rc);
            return -1;
        }
    }
    if (sceIoMkdir(data_root, 0777) < 0) {
        SceIoStat st;
        int rc = sceIoGetstat(data_root, &st);
        if (rc < 0) {
            sceClibPrintf("segamod2: data directory unavailable: 0x%08x\n", (unsigned)rc);
            return -1;
        }
    }
    fd = sceIoOpen("ux0:data/segamod2/vita.log",
                   SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        sceClibPrintf("segamod2: startup log creation failed: 0x%08x\n", (unsigned)fd);
        return -1;
    }
    closed = sceIoClose(fd);
    if (closed < 0) {
        sceClibPrintf("segamod2: startup log close failed: 0x%08x\n", (unsigned)closed);
        return -1;
    }
    vita_startup_log("startup: native storage ready; opening C stderr\n");
    if (!freopen("ux0:data/segamod2/vita.log", "a", stderr)) {
        vita_startup_log("startup: C stderr open failed\n");
        return -1;
    }
    setvbuf(stderr, NULL, _IONBF, 0);
    vita_startup_log("startup: C stderr ready; setting data root\n");
    if (setenv("SEGAMOD2_ROOT", data_root, 1) != 0) {
        vita_startup_log("startup: data root environment failed\n");
        return -1;
    }
    vita_startup_log("startup: data root ready; setting ROM directory\n");
    if (setenv("SEGAMOD2_ROM_DIR", "ux0:data/segamod2/ROMS/srallyc-b", 1) != 0) {
        vita_startup_log("startup: ROM directory environment failed\n");
        return -1;
    }
    vita_startup_log("startup: ROM directory ready; setting NVRAM path\n");
    if (setenv("I960_HOST_NVRAM", "ux0:data/segamod2/nvram.yaml", 1) != 0) {
        vita_startup_log("startup: NVRAM environment failed\n");
        return -1;
    }
    vita_startup_log("startup: storage environment ready\n");
    return 0;
}

static int start_screen(int error)
{
    vita_controls_t controls = {0, 1, 1};
    vita_menu_open(&g_vita_menu);
    if (error)
        snprintf(g_vita_menu.status, sizeof(g_vita_menu.status), "ROM load failed - fix board dumps, then retry. See vita.log.");
    vita_startup_log("startup: submitting launch menu\n");
    vita_gxm_menu(0);
    vita_startup_log("startup: launch menu submitted; waiting for input\n");
    for (;;) {
        SceCtrlData pad;
        vita_actions_t input;
        vita_menu_action_t action;
        vita_gxm_menu(0);
        if (sceCtrlPeekBufferPositive(0, &pad, 1) <= 0) {
            fprintf(stderr, "lift: Vita startup controller read failed\n");
            return -2;
        }
        /* Startup has no guest yet, so the pause chord must not resume it. */
        input = vita_controls_update(&controls,
            vita_pad_buttons(pad.buttons) & ~(VITA_START | VITA_SELECT));
        action = vita_menu_update(&g_vita_menu, input.pressed, 0);
        if (action.changed)
            vita_frontend_apply(1);
        if (action.quit && vita_frontend_apply(1) == 0)
            return -1;
        if (action.resume || action.reset)
            return 0;
        sceKernelDelayThread(16000);
    }
}

static int launch_menu(track_viewer_opts_t *opts)
{
    char *argv[] = {"segamod2", NULL};
    vita_startup_log("startup: setting controller sampling\n");
    if (sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG) < 0) {
        fprintf(stderr, "lift: Vita analog sampling setup failed\n");
        return -2;
    }
    if (vita_gxm_open() != 0)
        return -2;
    vita_startup_log("startup: loading frontend settings\n");
    if (vita_settings_load(&g_vita_menu.settings, "ux0:data/segamod2/vita.cfg") != 0) {
        vita_frontend_apply(0);
        snprintf(g_vita_menu.status, sizeof(g_vita_menu.status), "Settings load failed - defaults applied. See vita.log.");
    } else {
        vita_frontend_apply(0);
    }
    vita_startup_log("startup: parsing launch options\n");
    if (track_viewer_cli_parse(1, argv, opts) != 1) {
        fprintf(stderr, "lift: Vita launch options failed\n");
        return -2;
    }
    {
        int error = 0;
        for (;;) {
            int choice = start_screen(error);
            if (choice != 0)
                return choice;
            vita_startup_log("startup: start selected; verifying ROMs\n");
            if (model2_romset_verify() == 0 && model2_rom_load_default() == 0)
                break;
            fprintf(stderr, "lift: Vita ROM startup failed before reset\n");
            error = 1;
        }
    }
    {
        const u8 *header = model2_rom_at(0x02879db0u);
        char message[128];
        int valid = model2_main_data_size >= 0x00879db8u
            && header && memcmp(header, "CGM 1.0 ", 8) == 0;
        snprintf(message, sizeof(message),
                 "startup: loaded main data bytes=%u splash_header_ok=%d\n",
                 (unsigned)model2_main_data_size, valid);
        vita_startup_log(message);
    }
    vita_rom_diagnostic("loaded");
    return 0;
}

static void *run_game(void *opaque)
{
    const track_viewer_opts_t *opts = opaque;
    int result;
    vita_startup_log("startup: guest thread entered\n");
    do {
        g_vita_menu.restart = 0;
        vita_startup_log("startup: resetting guest hardware and sound\n");
        i960_host_reset();
        vita_rom_diagnostic("after reset");
        vita_startup_log("startup: guest reset complete; initializing trace\n");
        i960_host_trace_init();
        vita_startup_log("startup: guest trace ready; entering boot harness\n");
        result = i960_lift_boot_screen_run(opts);
        vita_startup_log("startup: boot harness returned; shutting down viewer\n");
        sys24_viewer_shutdown();
    } while (result == 0 && g_vita_menu.restart);
    vita_startup_log("startup: guest thread exiting\n");
    return (void *)(intptr_t)result;
}

int main(void)
{
    track_viewer_opts_t opts;
    pthread_attr_t attr;
    pthread_t thread;
    void *result = NULL;
    int rc;
    if (prepare_storage() != 0)
        return 1;
    vita_startup_log("startup: runtime diagnostic build " __DATE__ " " __TIME__ "\n");
    vita_startup_log("startup: boot wait-table terminator fix enabled\n");
    vita_startup_log("startup: incremental tile cache and stack stability fixes enabled\n");
    vita_startup_log("startup: hashed GXM caches, ordered batches and bounded texture arena enabled\n");
    vita_startup_log("startup: Daytona-style perspective lattice and solid checker mask enabled\n");
    vita_startup_log("startup: stable radix painter order and visible-polygon clipping fast paths enabled\n");
    fprintf(stderr, "lift: Vita startup, data=%s heap=128 MiB\n", data_root);
    rc = launch_menu(&opts);
    if (rc != 0) {
        vita_gxm_shutdown();
        return rc == -1 ? 0 : 1;
    }
    vita_startup_log("startup: creating guest thread after launch menu\n");
    rc = pthread_attr_init(&attr);
    if (rc != 0) {
        fprintf(stderr, "lift: Vita main thread attributes failed: %d\n", rc);
        vita_gxm_shutdown();
        return 1;
    }
    rc = pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (rc == 0)
        rc = pthread_create(&thread, &attr, run_game, &opts);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        fprintf(stderr, "lift: Vita main thread creation failed: %d\n", rc);
        vita_gxm_shutdown();
        return 1;
    }
    rc = pthread_join(thread, &result);
    if (rc != 0) {
        fprintf(stderr, "lift: Vita main thread join failed: %d\n", rc);
        return 1;
    }
    return (int)(intptr_t)result;
}
