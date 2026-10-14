#ifndef SEGAMOD2_VITA_GL_RENDERER_H
#define SEGAMOD2_VITA_GL_RENDERER_H

int vita_gl_open(void);
void vita_gl_swap_buffers(void);
void vita_gl_shutdown(void);
void vita_gl_menu(int have_game);
void vita_gl_message(const char *text);

#endif
