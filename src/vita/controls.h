#ifndef SEGAMOD2_VITA_CONTROLS_H
#define SEGAMOD2_VITA_CONTROLS_H

enum {
    VITA_CROSS = 1u << 0, VITA_CIRCLE = 1u << 1,
    VITA_SQUARE = 1u << 2, VITA_TRIANGLE = 1u << 3,
    VITA_UP = 1u << 4, VITA_DOWN = 1u << 5,
    VITA_LEFT = 1u << 6, VITA_RIGHT = 1u << 7,
    VITA_L = 1u << 8, VITA_R = 1u << 9,
    VITA_START = 1u << 10, VITA_SELECT = 1u << 11
};

typedef struct {
    unsigned previous;
    int paused;
    int wait_release;
} vita_controls_t;

typedef struct {
    unsigned held, pressed;
    int pause_changed, test, service;
} vita_actions_t;

static inline vita_actions_t vita_controls_update(vita_controls_t *s, unsigned buttons)
{
    vita_actions_t out = {0};
    unsigned pressed = buttons & ~s->previous;
    s->previous = buttons;
    if (s->wait_release) {
        if (!buttons)
            s->wait_release = 0;
        return out;
    }
    if ((buttons & (VITA_START | VITA_SELECT)) == (VITA_START | VITA_SELECT)) {
        s->paused = !s->paused;
        s->wait_release = 1;
        out.pause_changed = 1;
        return out;
    }
    if (s->paused) {
        out.pressed = pressed;
        return out;
    }
    out.held = buttons;
    out.pressed = pressed;
    return out;
}

static inline unsigned char vita_controls_steer(const vita_controls_t *s,
                                               unsigned char lx, int in_test)
{
    return s->wait_release || s->paused || in_test || (lx > 115 && lx < 141)
               ? 0x80u : lx;
}

#endif
