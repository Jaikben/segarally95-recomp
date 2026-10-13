#include "vita_gl_renderer.h"

#include "bitmap_font.h"
#include "menu.h"
#include "startup_log.h"

#include <vitaGL.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    VITA_GL_WIDTH = 960,
    VITA_GL_HEIGHT = 544,
    VITA_GL_VRAM_BUDGET = 32 * 1024 * 1024,
    VITA_MENU_WIDTH = VITA_GL_WIDTH / 2,
    VITA_MENU_HEIGHT = VITA_GL_HEIGHT / 2,
    VITA_MENU_HEADER_HEIGHT = 34
};

static int g_initialized;
static int g_menu_diagnostics = 1;
static GLuint g_menu_texture;
static uint8_t g_menu_pixels[VITA_MENU_WIDTH * VITA_MENU_HEIGHT * 4];
static const GLfloat g_menu_vertices[] = {
    0.f, 0.f,
    (float)VITA_GL_WIDTH, 0.f,
    (float)VITA_GL_WIDTH, (float)VITA_GL_HEIGHT,
    0.f, (float)VITA_GL_HEIGHT
};
static const GLfloat g_menu_texcoords[] = {
    0.f, 1.f,
    1.f, 1.f,
    1.f, 0.f,
    0.f, 0.f
};

static void set_pixel(int x, int y, const uint8_t color[4])
{
    size_t offset;

    if (x < 0 || x >= VITA_MENU_WIDTH || y < 0 || y >= VITA_MENU_HEIGHT)
        return;
    offset = ((size_t)y * VITA_MENU_WIDTH + (size_t)x) * 4;
    memcpy(&g_menu_pixels[offset], color, 4);
}

static void fill_rect(int x, int y, int width, int height, const uint8_t color[4])
{
    int row, col;

    for (row = y; row < y + height; ++row)
        for (col = x; col < x + width; ++col)
            set_pixel(col, row, color);
}

static void draw_text(int x, int y, const char *text, const uint8_t color[4])
{
    const unsigned char *p = (const unsigned char *)text;

    for (; *p && x < VITA_MENU_WIDTH - 5; ++p, x += 6) {
        unsigned c = vita_font_character(*p);
        unsigned row;

        for (row = 0; row < 7; ++row) {
            unsigned bits = vita_font_row(c, row);
            unsigned col;

            for (col = 0; col < 5; ++col)
                if (bits & (1u << (4 - col)))
                    set_pixel(x + (int)col, y + (int)row, color);
        }
    }
}

static void build_menu_pixels(int have_game)
{
    static const uint8_t background[4] = {12, 16, 24, 255};
    static const uint8_t header[4] = {24, 34, 50, 255};
    static const uint8_t panel[4] = {18, 24, 34, 255};
    static const uint8_t selected[4] = {82, 62, 24, 255};
    static const uint8_t accent[4] = {244, 180, 58, 255};
    static const uint8_t white[4] = {235, 235, 235, 255};
    int i, count;
    char label[96], line[128];

    fill_rect(0, 0, VITA_MENU_WIDTH, VITA_MENU_HEIGHT, background);
    fill_rect(0, 0, VITA_MENU_WIDTH, VITA_MENU_HEADER_HEIGHT, header);
    fill_rect(0, VITA_MENU_HEADER_HEIGHT, VITA_MENU_WIDTH, 2, accent);

    draw_text(16, 10, "SEGA RALLY RECOMP", white);
    draw_text(16, 41, g_vita_menu.options ? "OPTIONS" : "PS VITA", accent);
    if (!g_vita_menu.options) {
        snprintf(line, sizeof(line), "CPU %d MHZ  GPU %d MHZ",
                 g_vita_menu.actual_cpu, g_vita_menu.actual_gpu);
        draw_text(16, 54, line, white);
    }

    count = g_vita_menu.options ? VITA_OPTION_COUNT : 4;
    for (i = 0; i < count; ++i) {
        int y = g_vita_menu.options ? 45 + i * 12 : 79 + i * 28;
        int row_height = g_vita_menu.options ? 10 : 18;
        const uint8_t *color = i == g_vita_menu.selection ? accent : white;

        if (i == g_vita_menu.selection)
            fill_rect(12, y - 3, VITA_MENU_WIDTH - 24, row_height, selected);
        vita_menu_label(&g_vita_menu, i, have_game, label, sizeof(label));
        snprintf(line, sizeof(line), "%c %s",
                 i == g_vita_menu.selection ? '>' : ' ', label);
        draw_text(20, y, line, color);
    }

    fill_rect(12, 221, VITA_MENU_WIDTH - 24, 1, panel);
    draw_text(16, 231, g_vita_menu.status, white);
    draw_text(16, 253,
        g_vita_menu.options
            ? "UP DOWN MOVE  LEFT RIGHT CHANGE  X SELECT  O BACK"
            : "UP DOWN MOVE  X SELECT  O RESUME  START SELECT PAUSE",
        white);
}

int vita_gl_open(void)
{
    if (g_initialized)
        return 0;

    vita_startup_log("startup: VitaGL initialization begin (4x MSAA)\n");
    vglInitExtended(0, VITA_GL_WIDTH, VITA_GL_HEIGHT, VITA_GL_VRAM_BUDGET,
                    SCE_GXM_MULTISAMPLE_4X);
    vita_startup_log("startup: VitaGL surface initialized\n");
    glViewport(0, 0, VITA_GL_WIDTH, VITA_GL_HEIGHT);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    vita_startup_log("startup: creating menu texture\n");
    glGenTextures(1, &g_menu_texture);
    glBindTexture(GL_TEXTURE_2D, g_menu_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VITA_MENU_WIDTH, VITA_MENU_HEIGHT,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_initialized = 1;
    vita_startup_log("startup: VitaGL renderer ready at 960x544 with menu texture\n");
    return 0;
}

void vita_gl_swap_buffers(void)
{
    if (g_initialized)
        vglSwapBuffers(GL_FALSE);
}

void vita_gl_menu(int have_game)
{
    int diagnose = g_menu_diagnostics;

    if (!g_initialized)
        return;

    build_menu_pixels(have_game);
    if (diagnose)
        vita_startup_log("menu diagnostic: bitmap rasterized\n");
    glBindTexture(GL_TEXTURE_2D, g_menu_texture);
    if (diagnose)
        vita_startup_log("menu diagnostic: texture upload begin\n");
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VITA_MENU_WIDTH, VITA_MENU_HEIGHT,
                    GL_RGBA, GL_UNSIGNED_BYTE, g_menu_pixels);
    if (diagnose)
        vita_startup_log("menu diagnostic: texture upload complete\n");

    glViewport(0, 0, VITA_GL_WIDTH, VITA_GL_HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, VITA_GL_WIDTH, VITA_GL_HEIGHT, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, g_menu_vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, g_menu_texcoords);
    if (diagnose)
        vita_startup_log("menu diagnostic: textured array draw begin\n");
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);
    if (diagnose)
        vita_startup_log("menu diagnostic: textured array draw complete\n");
    vita_gl_swap_buffers();
    if (diagnose) {
        vita_startup_log("menu diagnostic: first frame presented\n");
        g_menu_diagnostics = 0;
    }
}

void vita_gl_shutdown(void)
{
    g_initialized = 0;
}
