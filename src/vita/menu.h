#ifndef SEGAMOD2_VITA_MENU_H
#define SEGAMOD2_VITA_MENU_H

typedef struct {
    int cpu, gpu, volume, mute, deadzone, invert;
} vita_settings_t;

typedef struct {
    vita_settings_t settings;
    int options, selection, restart, actual_cpu, actual_gpu;
    char status[128];
} vita_menu_t;

typedef struct {
    int resume, reset, quit, test, service, changed;
} vita_menu_action_t;

enum { VITA_OPTION_COUNT = 14 };

extern vita_menu_t g_vita_menu;
void vita_settings_defaults(vita_settings_t *settings);
int vita_settings_load(vita_settings_t *settings, const char *path);
int vita_settings_save(const vita_settings_t *settings, const char *path);
void vita_menu_open(vita_menu_t *menu);
vita_menu_action_t vita_menu_update(vita_menu_t *menu, unsigned pressed, int have_game);
void vita_menu_label(const vita_menu_t *menu, int row, int have_game, char *text, unsigned size);
unsigned char vita_settings_steer(const vita_settings_t *settings, unsigned char lx);

#endif
