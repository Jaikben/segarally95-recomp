#ifndef SEGAMOD2_GXM_RENDERER_H
#define SEGAMOD2_GXM_RENDERER_H

#include "model2_geo.h"

int vita_gxm_open(void);
int vita_gxm_present(const u32 *bottom, const u32 *priority, int opaque,
                     int tiles_dirty, int paused);
void vita_gxm_shutdown(void);
void vita_gxm_message(const char *title, const char *detail);
void vita_gxm_menu(int have_game);

#endif
