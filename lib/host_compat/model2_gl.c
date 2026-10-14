#include "model2_gl.h"

#include <stdint.h>

#ifdef I960_HOST_HAVE_GL

#ifdef I960_HOST_HAVE_SDL
#include <SDL.h>
#endif

#if defined(_WIN32)

#include <stdio.h>

PFN_model2_glCreateShader model2_glCreateShader;
PFN_model2_glShaderSource model2_glShaderSource;
PFN_model2_glCompileShader model2_glCompileShader;
PFN_model2_glGetShaderiv model2_glGetShaderiv;
PFN_model2_glGetShaderInfoLog model2_glGetShaderInfoLog;
PFN_model2_glDeleteShader model2_glDeleteShader;
PFN_model2_glCreateProgram model2_glCreateProgram;
PFN_model2_glAttachShader model2_glAttachShader;
PFN_model2_glLinkProgram model2_glLinkProgram;
PFN_model2_glDeleteProgram model2_glDeleteProgram;
PFN_model2_glGetProgramiv model2_glGetProgramiv;
PFN_model2_glGetProgramInfoLog model2_glGetProgramInfoLog;
PFN_model2_glGetUniformLocation model2_glGetUniformLocation;
PFN_model2_glUseProgram model2_glUseProgram;
PFN_model2_glActiveTexture model2_glActiveTexture;
PFN_model2_glUniform1i model2_glUniform1i;
PFN_model2_glUniform1f model2_glUniform1f;
PFN_model2_glUniform4f model2_glUniform4f;
PFN_model2_glUniform2f model2_glUniform2f;

static void *gl_get_proc(const char *name)
{
    void *p = NULL;

#ifdef I960_HOST_HAVE_SDL
    p = (void *)SDL_GL_GetProcAddress(name);
#endif
    if (!p)
        p = (void *)(uintptr_t)wglGetProcAddress(name);
    return p;
}

int model2_gl_load(void)
{
    static int once;
    static int ok;

    if (once)
        return ok;
    once = 1;
    ok = 1;

#define LOAD(sym, name)                                                      \
    do {                                                                     \
        sym = (PFN_##sym)gl_get_proc(name);                                  \
        if (!sym)                                                            \
            ok = 0;                                                          \
    } while (0)

    LOAD(model2_glCreateShader, "glCreateShader");
    LOAD(model2_glShaderSource, "glShaderSource");
    LOAD(model2_glCompileShader, "glCompileShader");
    LOAD(model2_glGetShaderiv, "glGetShaderiv");
    LOAD(model2_glGetShaderInfoLog, "glGetShaderInfoLog");
    LOAD(model2_glDeleteShader, "glDeleteShader");
    LOAD(model2_glCreateProgram, "glCreateProgram");
    LOAD(model2_glAttachShader, "glAttachShader");
    LOAD(model2_glLinkProgram, "glLinkProgram");
    LOAD(model2_glDeleteProgram, "glDeleteProgram");
    LOAD(model2_glGetProgramiv, "glGetProgramiv");
    LOAD(model2_glGetProgramInfoLog, "glGetProgramInfoLog");
    LOAD(model2_glGetUniformLocation, "glGetUniformLocation");
    LOAD(model2_glUseProgram, "glUseProgram");
    LOAD(model2_glActiveTexture, "glActiveTexture");
    LOAD(model2_glUniform1i, "glUniform1i");
    LOAD(model2_glUniform1f, "glUniform1f");
    LOAD(model2_glUniform4f, "glUniform4f");
    LOAD(model2_glUniform2f, "glUniform2f");
#undef LOAD

    if (!ok)
        fprintf(stderr, "lift: OpenGL 2.0 shader entry points missing\n");
    return ok;
}

#else /* !_WIN32 */

#if defined(I960_HOST_VITA_GL)

#include <stdlib.h>
#include <string.h>

#undef glBegin
#undef glEnd
#undef glVertex2f
#undef glVertex3f
#undef glColor3f
#undef glColor4f
#undef glTexCoord2f

static GLenum s_mode;
static int s_active;
static float *s_pos;
static float *s_uv;
static unsigned char *s_col;
static unsigned s_n, s_cap;
static float s_cur_col[4] = {1.f, 1.f, 1.f, 1.f};
static float s_cur_uv[2];
static int s_have_uv;

static int imm_reserve(unsigned need)
{
    unsigned cap;
    float *p;
    float *u;
    unsigned char *c;

    if (need <= s_cap)
        return 1;
    cap = s_cap ? s_cap : 1024u;
    while (cap < need)
        cap *= 2u;
    p = (float *)realloc(s_pos, (size_t)cap * 3u * sizeof(float));
    if (!p)
        return 0;
    s_pos = p;
    u = (float *)realloc(s_uv, (size_t)cap * 2u * sizeof(float));
    if (!u)
        return 0;
    s_uv = u;
    c = (unsigned char *)realloc(s_col, (size_t)cap * 4u);
    if (!c)
        return 0;
    s_col = c;
    s_cap = cap;
    return 1;
}

static unsigned char to_u8(float v)
{
    if (v < 0.f)
        v = 0.f;
    if (v > 1.f)
        v = 1.f;
    return (unsigned char)(v * 255.f + 0.5f);
}

void model2_vita_glBegin(GLenum mode)
{
    s_mode = mode;
    s_n = 0;
    s_have_uv = 0;
    s_active = 1;
}

static void imm_vertex(float x, float y, float z)
{
    unsigned i = s_n;

    if (!s_active || !imm_reserve(i + 1u))
        return;
    s_pos[i * 3u + 0u] = x;
    s_pos[i * 3u + 1u] = y;
    s_pos[i * 3u + 2u] = z;
    s_uv[i * 2u + 0u] = s_cur_uv[0];
    s_uv[i * 2u + 1u] = s_cur_uv[1];
    s_col[i * 4u + 0u] = to_u8(s_cur_col[0]);
    s_col[i * 4u + 1u] = to_u8(s_cur_col[1]);
    s_col[i * 4u + 2u] = to_u8(s_cur_col[2]);
    s_col[i * 4u + 3u] = to_u8(s_cur_col[3]);
    s_n++;
}

void model2_vita_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    imm_vertex(x, y, z);
}

void model2_vita_glVertex2f(GLfloat x, GLfloat y)
{
    imm_vertex(x, y, 0.f);
}

void model2_vita_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    s_cur_col[0] = r;
    s_cur_col[1] = g;
    s_cur_col[2] = b;
    s_cur_col[3] = a;
    glColor4f(r, g, b, a);
}

void model2_vita_glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    model2_vita_glColor4f(r, g, b, 1.f);
}

void model2_vita_glTexCoord2f(GLfloat s, GLfloat t)
{
    s_cur_uv[0] = s;
    s_cur_uv[1] = t;
    s_have_uv = 1;
}

void model2_vita_glEnd(void)
{
    GLenum mode = s_mode;
    unsigned n = s_n;

    if (!s_active)
        return;
    s_active = 0;
    if (n == 0)
        return;

    if (mode == GL_QUADS) {
        unsigned quads = n / 4u, q, k = 0;
        unsigned need = quads * 6u;
        static const unsigned order[6] = {0, 1, 2, 0, 2, 3};
        float *tp;
        float *tu;
        unsigned char *tc;

        if (quads == 0)
            return;
        tp = (float *)malloc((size_t)need * 3u * sizeof(float));
        tu = (float *)malloc((size_t)need * 2u * sizeof(float));
        tc = (unsigned char *)malloc((size_t)need * 4u);
        if (!tp || !tu || !tc) {
            free(tp);
            free(tu);
            free(tc);
            return;
        }
        for (q = 0; q < quads; q++) {
            unsigned j;

            for (j = 0; j < 6u; j++, k++) {
                unsigned s = q * 4u + order[j];

                memcpy(&tp[k * 3u], &s_pos[s * 3u], 3u * sizeof(float));
                memcpy(&tu[k * 2u], &s_uv[s * 2u], 2u * sizeof(float));
                memcpy(&tc[k * 4u], &s_col[s * 4u], 4u);
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, tp);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, tc);
        if (s_have_uv) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, 0, tu);
        } else {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)need);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        free(tp);
        free(tu);
        free(tc);
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, s_pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, s_col);
    if (s_have_uv) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, s_uv);
    } else {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glDrawArrays(mode, 0, (GLsizei)n);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

#endif /* I960_HOST_VITA_GL */

int model2_gl_load(void)
{
    return 1;
}

#endif /* _WIN32 */

#else /* !I960_HOST_HAVE_GL */

int model2_gl_load(void)
{
    return 0;
}

#endif /* I960_HOST_HAVE_GL */
