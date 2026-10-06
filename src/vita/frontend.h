#ifndef SEGAMOD2_VITA_FRONTEND_H
#define SEGAMOD2_VITA_FRONTEND_H

#include "menu.h"

unsigned vita_pad_buttons(unsigned native);
int vita_frontend_apply(int save);
void vita_frontend_save_error(void);

#endif
