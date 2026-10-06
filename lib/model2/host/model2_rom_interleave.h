#ifndef MODEL2_ROM_INTERLEAVE_H
#define MODEL2_ROM_INTERLEAVE_H

#include <stdio.h>

/* Consume one ROM_LOAD32_WORD pair at the current image offset.
 * Returns 1 for identical bytes, 0 for mismatched data, -1 for I/O errors. */
int model2_rom_interleave_matches(FILE *image, FILE *low, FILE *high);

#endif
