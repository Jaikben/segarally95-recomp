#include "menu.h"
#include "controls.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

vita_menu_t g_vita_menu;
static const int cpu_choices[] = {111, 222, 333, 444};
static const int gpu_choices[] = {41, 77, 111, 166};

void vita_settings_defaults(vita_settings_t *s)
{
    *s = (vita_settings_t){333, 111, 100, 0, 10, 0};
}

static int valid_clock(int value, const int *choices)
{
    int i;
    for (i = 0; i < 4; ++i)
        if (value == choices[i])
            return 1;
    return 0;
}

static int valid_settings(const vita_settings_t *s)
{
    return valid_clock(s->cpu, cpu_choices) && valid_clock(s->gpu, gpu_choices)
        && s->volume >= 0 && s->volume <= 100
        && s->deadzone >= 0 && s->deadzone <= 40
        && (s->mute == 0 || s->mute == 1) && (s->invert == 0 || s->invert == 1);
}

static int settings_read(vita_settings_t *s, const char *path)
{
    vita_settings_t loaded;
    char line[128], key[40], extra;
    int value, ok = 1;
    FILE *f;
    vita_settings_defaults(s);
    loaded = *s;
    f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT)
            return 1;
        fprintf(stderr, "lift: Vita settings open failed: %s: %s\n", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%39[^=]=%d %c", key, &value, &extra) != 2) {
            ok = 0;
            break;
        }
        if (!strcmp(key, "cpu_clock")) loaded.cpu = value;
        else if (!strcmp(key, "gpu_clock")) loaded.gpu = value;
        else if (!strcmp(key, "volume")) loaded.volume = value;
        else if (!strcmp(key, "mute")) loaded.mute = value;
        else if (!strcmp(key, "deadzone")) loaded.deadzone = value;
        else if (!strcmp(key, "steer_invert")) loaded.invert = value;
        else {
            ok = 0;
            break;
        }
    }
    if (ferror(f))
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok || !valid_settings(&loaded)) {
        fprintf(stderr, "lift: invalid/unreadable Vita settings: %s; using defaults\n", path);
        return -1;
    }
    *s = loaded;
    return 0;
}

int vita_settings_load(vita_settings_t *s, const char *path)
{
    char backup[512];
    int result = settings_read(s, path);
    int recovered;
    if (result == 0)
        return 0;
    if (snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) {
        fprintf(stderr, "lift: Vita settings backup path too long\n");
        return -1;
    }
    recovered = settings_read(s, backup);
    if (recovered == 0) {
        fprintf(stderr, "lift: recovered Vita settings from %s\n", backup);
        return 0;
    }
    return result == 1 && recovered == 1 ? 0 : -1;
}

int vita_settings_save(const vita_settings_t *s, const char *path)
{
    char tmp[512], backup[512];
    FILE *f;
    int ok, moved = 0;
    if (!valid_settings(s)
        || snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)
        || snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) {
        fprintf(stderr, "lift: invalid Vita settings or settings path\n");
        return -1;
    }
    f = fopen(tmp, "w");
    if (!f) {
        fprintf(stderr, "lift: Vita settings write open failed: %s\n", strerror(errno));
        return -1;
    }
    ok = fprintf(f, "cpu_clock=%d\ngpu_clock=%d\nvolume=%d\nmute=%d\n"
                 "deadzone=%d\nsteer_invert=%d\n",
                 s->cpu, s->gpu, s->volume, s->mute, s->deadzone, s->invert) > 0;
    if (fflush(f) != 0)
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;
    /* Vita's rename need not replace an existing destination. Keep the old
     * generation until the fully written temporary file has been installed. */
    if (ok) {
        f = fopen(path, "r");
        if (f) {
            if (fclose(f) != 0)
                ok = 0;
            if (ok && remove(backup) != 0 && errno != ENOENT)
                ok = 0;
            if (ok) {
                if (rename(path, backup) != 0)
                    ok = 0;
                else
                    moved = 1;
            }
        } else if (errno != ENOENT) {
            ok = 0;
        }
    }
    if (ok && rename(tmp, path) == 0)
        return 0;
    fprintf(stderr, "lift: Vita settings save failed: %s: %s\n", path, strerror(errno));
    if (moved && rename(backup, path) != 0)
        fprintf(stderr, "lift: Vita settings rollback failed; backup retained: %s\n", backup);
    if (remove(tmp) != 0)
        fprintf(stderr, "lift: Vita settings temporary file cleanup failed: %s\n", strerror(errno));
    return -1;
}

void vita_menu_open(vita_menu_t *m)
{
    m->options = m->selection = 0;
}

static int cycle(int value, const int *choices, int direction)
{
    int i;
    for (i = 0; i < 4; ++i)
        if (choices[i] == value)
            return choices[(i + (direction < 0 ? 3 : 1)) % 4];
    return choices[0];
}

vita_menu_action_t vita_menu_update(vita_menu_t *m, unsigned pressed, int have_game)
{
    vita_menu_action_t a = {0};
    int count = m->options ? VITA_OPTION_COUNT : 4;
    int direction = (pressed & VITA_LEFT) ? -1 : 1;
    int activate = (pressed & VITA_CROSS) != 0;
    if (pressed & VITA_UP)
        m->selection = (m->selection + count - 1) % count;
    if (pressed & VITA_DOWN)
        m->selection = (m->selection + 1) % count;
    if (pressed & VITA_CIRCLE) {
        if (m->options) {
            m->options = 0;
            m->selection = 2;
        } else {
            a.resume = have_game;
        }
        return a;
    }
    if (!m->options) {
        if (activate) {
            switch (m->selection) {
            case 0: a.resume = 1; break;
            case 1: a.reset = 1; break;
            case 2: m->options = 1; m->selection = 0; break;
            case 3: a.quit = 1; break;
            }
        }
        return a;
    }
    if (!(pressed & (VITA_LEFT | VITA_RIGHT | VITA_CROSS)))
        return a;
    switch (m->selection) {
    case 0: m->settings.cpu = cycle(m->settings.cpu, cpu_choices, direction); a.changed = 1; break;
    case 1: m->settings.gpu = cycle(m->settings.gpu, gpu_choices, direction); a.changed = 1; break;
    case 2:
        m->settings.volume += direction * 10;
        if (m->settings.volume < 0) m->settings.volume = 0;
        if (m->settings.volume > 100) m->settings.volume = 100;
        a.changed = 1;
        break;
    case 3: m->settings.mute = !m->settings.mute; a.changed = 1; break;
    case 4:
        m->settings.deadzone += direction * 5;
        if (m->settings.deadzone < 0) m->settings.deadzone = 0;
        if (m->settings.deadzone > 40) m->settings.deadzone = 40;
        a.changed = 1;
        break;
    case 5: m->settings.invert = !m->settings.invert; a.changed = 1; break;
    case 6: snprintf(m->status, sizeof(m->status), "Audio: existing SDL / 68K + SCSP engine."); break;
    case 7: snprintf(m->status, sizeof(m->status), "Native GXM, 960 x 544, original aspect ratio."); break;
    case 8: snprintf(m->status, sizeof(m->status), "Board dumps: ux0:data/segamod2/ROMS/srallyc-b/"); break;
    case 9: snprintf(m->status, sizeof(m->status), "Stick: steer  X: gas  Square: brake  L/R: shift"); break;
    case 10: if (activate) { a.test = a.resume = have_game; } break;
    case 11: if (activate) { a.service = a.resume = have_game; } break;
    case 12: if (activate) { vita_settings_defaults(&m->settings); a.changed = 1; } break;
    case 13: if (activate) { m->options = 0; m->selection = 2; } break;
    }
    if ((m->selection == 10 || m->selection == 11) && !have_game)
        snprintf(m->status, sizeof(m->status), "Start the game before using cabinet actions.");
    return a;
}

void vita_menu_label(const vita_menu_t *m, int row, int have_game, char *text, unsigned size)
{
    static const char *const main_labels[] = {"START GAME", "RESET GAME", "OPTIONS", "SAVE AND QUIT"};
    static const char *const fixed[] = {"AUDIO ENGINE: SDL / SCSP", "GRAPHICS API: GXM",
        "ROM: SRALLYC-B BOARD DUMPS", "BINDINGS: VITA FIXED",
        "ENTER TEST MENU", "SERVICE COIN", "RESET DEFAULTS", "BACK"};
    const vita_settings_t *s = &m->settings;
    if (!m->options) {
        snprintf(text, size, "%s", row == 0 && have_game ? "RESUME GAME" : main_labels[row]);
        return;
    }
    switch (row) {
    case 0: snprintf(text, size, "CPU CLOCK: %d MHz", s->cpu); break;
    case 1: snprintf(text, size, "GPU CLOCK: %d MHz", s->gpu); break;
    case 2: snprintf(text, size, "VOLUME: %d%%", s->volume); break;
    case 3: snprintf(text, size, "MUTE: %s", s->mute ? "ON" : "OFF"); break;
    case 4: snprintf(text, size, "DEAD ZONE: %d%%", s->deadzone); break;
    case 5: snprintf(text, size, "INVERT STEERING: %s", s->invert ? "ON" : "OFF"); break;
    default: snprintf(text, size, "%s", fixed[row - 6]); break;
    }
}

unsigned char vita_settings_steer(const vita_settings_t *s, unsigned char lx)
{
    int delta = (int)lx - 128;
    int limit = s->deadzone * 128 / 100;
    if (delta >= -limit && delta <= limit)
        return 128;
    return s->invert ? (unsigned char)(255 - lx) : lx;
}
