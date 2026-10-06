#include "frontend.h"
#include "controls.h"

#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <stdio.h>

extern void model2_snd_host_audio_volume(int volume, int mute);
static const char settings_path[] = "ux0:data/segamod2/vita.cfg";

unsigned vita_pad_buttons(unsigned native)
{
    static const struct { unsigned native, portable; } map[] = {
        {SCE_CTRL_CROSS, VITA_CROSS}, {SCE_CTRL_CIRCLE, VITA_CIRCLE},
        {SCE_CTRL_SQUARE, VITA_SQUARE}, {SCE_CTRL_TRIANGLE, VITA_TRIANGLE},
        {SCE_CTRL_UP, VITA_UP}, {SCE_CTRL_DOWN, VITA_DOWN},
        {SCE_CTRL_LEFT, VITA_LEFT}, {SCE_CTRL_RIGHT, VITA_RIGHT},
        {SCE_CTRL_LTRIGGER, VITA_L}, {SCE_CTRL_RTRIGGER, VITA_R},
        {SCE_CTRL_START, VITA_START}, {SCE_CTRL_SELECT, VITA_SELECT}
    };
    unsigned i, buttons = 0;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
        if (native & map[i].native)
            buttons |= map[i].portable;
    return buttons;
}

int vita_frontend_apply(int save)
{
    const vita_settings_t *s = &g_vita_menu.settings;
    int cpu = scePowerSetArmClockFrequency(s->cpu);
    int gpu = scePowerSetGpuClockFrequency(s->gpu);
    int result = 0;
    g_vita_menu.actual_cpu = scePowerGetArmClockFrequency();
    g_vita_menu.actual_gpu = scePowerGetGpuClockFrequency();
    model2_snd_host_audio_volume(s->volume, s->mute);
    if (cpu < 0 || gpu < 0) {
        fprintf(stderr, "lift: Vita clock setup failed: CPU=%d GPU=%d\n", cpu, gpu);
        result = -1;
    }
    if (save && vita_settings_save(s, settings_path) != 0)
        result = -1;
    snprintf(g_vita_menu.status, sizeof(g_vita_menu.status),
             result ? "Settings apply/save failed - see vita.log." : "Settings applied%s.",
             save ? " and saved" : "");
    return result;
}

void vita_frontend_save_error(void)
{
    snprintf(g_vita_menu.status, sizeof(g_vita_menu.status), "NVRAM save failed - see vita.log.");
}
