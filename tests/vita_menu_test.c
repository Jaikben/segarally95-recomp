#include "menu.h"
#include "controls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); \
} } while (0)

static void test_menu(void)
{
    vita_menu_t m = {0};
    vita_menu_action_t a;
    char text[96];
    int i;
    vita_settings_defaults(&m.settings);
    vita_menu_open(&m);
    vita_menu_label(&m, 0, 0, text, sizeof(text));
    CHECK(!strcmp(text, "START GAME"));
    vita_menu_label(&m, 0, 1, text, sizeof(text));
    CHECK(!strcmp(text, "RESUME GAME"));
    CHECK(!vita_menu_update(&m, VITA_CIRCLE, 0).resume);
    CHECK(vita_menu_update(&m, VITA_CIRCLE, 1).resume);
    CHECK(vita_menu_update(&m, VITA_CROSS, 0).resume);
    vita_menu_update(&m, VITA_UP, 0);
    CHECK(m.selection == 3);
    CHECK(vita_menu_update(&m, VITA_CROSS, 1).quit);
    vita_menu_update(&m, VITA_DOWN, 1);
    vita_menu_update(&m, VITA_DOWN, 1);
    CHECK(vita_menu_update(&m, VITA_CROSS, 1).reset);
    vita_menu_update(&m, VITA_DOWN, 1);
    vita_menu_update(&m, VITA_CROSS, 1);
    CHECK(m.options && m.selection == 0);
    a = vita_menu_update(&m, VITA_RIGHT, 1);
    CHECK(a.changed && m.settings.cpu == 444);
    vita_menu_update(&m, VITA_RIGHT, 1);
    CHECK(m.settings.cpu == 111);
    vita_menu_update(&m, VITA_LEFT, 1);
    CHECK(m.settings.cpu == 444);
    m.selection = 1;
    vita_menu_update(&m, VITA_LEFT, 1);
    CHECK(m.settings.gpu == 77);
    m.selection = 2;
    for (i = 0; i < 15; ++i) vita_menu_update(&m, VITA_LEFT, 1);
    CHECK(m.settings.volume == 0);
    for (i = 0; i < 15; ++i) vita_menu_update(&m, VITA_RIGHT, 1);
    CHECK(m.settings.volume == 100);
    m.selection = 3;
    CHECK(vita_menu_update(&m, VITA_CROSS, 1).changed && m.settings.mute);
    m.selection = 4;
    for (i = 0; i < 15; ++i) vita_menu_update(&m, VITA_RIGHT, 1);
    CHECK(m.settings.deadzone == 40);
    for (i = 0; i < 15; ++i) vita_menu_update(&m, VITA_LEFT, 1);
    CHECK(m.settings.deadzone == 0);
    m.selection = 5;
    vita_menu_update(&m, VITA_CROSS, 1);
    CHECK(m.settings.invert);
    m.selection = 10;
    a = vita_menu_update(&m, VITA_CROSS, 0);
    CHECK(!a.resume && !a.test && strstr(m.status, "Start"));
    a = vita_menu_update(&m, VITA_CROSS, 1);
    CHECK(a.resume && a.test);
    m.selection = 11;
    a = vita_menu_update(&m, VITA_CROSS, 1);
    CHECK(a.resume && a.service);
    m.selection = 12;
    CHECK(vita_menu_update(&m, VITA_CROSS, 1).changed);
    CHECK(m.settings.cpu == 333 && m.settings.gpu == 111 && m.settings.volume == 100);
    CHECK(!m.settings.mute && !m.settings.invert && m.settings.deadzone == 10);
    m.selection = 0;
    vita_menu_update(&m, VITA_UP, 1);
    CHECK(m.selection == VITA_OPTION_COUNT - 1);
    vita_menu_update(&m, VITA_CROSS, 1);
    CHECK(!m.options && m.selection == 2);
    vita_menu_update(&m, VITA_CROSS, 1);
    vita_menu_update(&m, VITA_CIRCLE | VITA_CROSS, 1);
    CHECK(!m.options && m.selection == 2);
    m.options = 1;
    for (i = 0; i < VITA_OPTION_COUNT; ++i) {
        vita_menu_label(&m, i, 1, text, sizeof(text));
        CHECK(text[0] && strlen(text) < sizeof(text) - 1);
    }
}

static void test_steer(void)
{
    vita_settings_t s;
    vita_settings_defaults(&s);
    CHECK(vita_settings_steer(&s, 0) == 0);
    CHECK(vita_settings_steer(&s, 255) == 255);
    CHECK(vita_settings_steer(&s, 115) == 115);
    CHECK(vita_settings_steer(&s, 116) == 128);
    CHECK(vita_settings_steer(&s, 140) == 128);
    CHECK(vita_settings_steer(&s, 141) == 141);
    s.deadzone = 0;
    CHECK(vita_settings_steer(&s, 127) == 127);
    CHECK(vita_settings_steer(&s, 128) == 128);
    s.invert = 1;
    CHECK(vita_settings_steer(&s, 128) == 128);
    CHECK(vita_settings_steer(&s, 0) == 255);
    CHECK(vita_settings_steer(&s, 255) == 0);
    s.deadzone = 40;
    CHECK(vita_settings_steer(&s, 77) == 128);
    CHECK(vita_settings_steer(&s, 179) == 128);
    CHECK(vita_settings_steer(&s, 76) == 179);
}

static void test_settings(const char *path)
{
    vita_settings_t s, loaded;
    char backup[512];
    FILE *f;
    CHECK(vita_settings_load(&s, path) == 0);
    CHECK(s.cpu == 333);
    s.cpu = 444; s.gpu = 166; s.volume = 30;
    s.mute = s.invert = 1; s.deadzone = 25;
    CHECK(vita_settings_save(&s, path) == 0);
    CHECK(vita_settings_load(&loaded, path) == 0);
    CHECK(!memcmp(&s, &loaded, sizeof(s)));
    s.volume = 60;
    CHECK(vita_settings_save(&s, path) == 0);
    CHECK(vita_settings_load(&loaded, path) == 0 && loaded.volume == 60);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    CHECK(vita_settings_load(&loaded, backup) == 0 && loaded.volume == 30);
    f = fopen(path, "w");
    CHECK(f && fputs("volume=invalid\n", f) >= 0 && fclose(f) == 0);
    CHECK(vita_settings_load(&loaded, path) == 0 && loaded.volume == 30);
    CHECK(remove(path) == 0);
    CHECK(vita_settings_load(&loaded, path) == 0 && loaded.volume == 30);
    CHECK(vita_settings_save(&s, path) == 0);
    CHECK(remove(backup) == 0);
    s.cpu = 999;
    CHECK(vita_settings_save(&s, path) == -1);
    CHECK(vita_settings_load(&loaded, path) == 0 && loaded.cpu == 444);
    f = fopen(path, "w");
    CHECK(f && fputs("cpu_clock=999\n", f) >= 0 && fclose(f) == 0);
    CHECK(vita_settings_load(&loaded, path) == -1 && loaded.cpu == 333);
    f = fopen(path, "w");
    CHECK(f && fputs("volume=50garbage\n", f) >= 0 && fclose(f) == 0);
    CHECK(vita_settings_load(&loaded, path) == -1);
    CHECK(remove(path) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    test_menu();
    test_steer();
    test_settings(argv[1]);
    puts("Vita menu, settings persistence and steering tests passed");
    return 0;
}
