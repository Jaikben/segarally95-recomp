#include "model2_rom_interleave.h"

int model2_rom_interleave_matches(FILE *image, FILE *low, FILE *high)
{
    unsigned char low_bytes[4096], high_bytes[4096], image_bytes[8192];
    size_t n, high_n, image_n, i;

    if (!image || !low || !high) {
        fprintf(stderr, "lift: ROM cache comparison requires three open streams\n");
        return -1;
    }
    for (;;) {
        n = fread(low_bytes, 1, sizeof(low_bytes), low);
        if (ferror(low)) {
            fprintf(stderr, "lift: ROM cache comparison: low ROM read failed\n");
            return -1;
        }
        if (n == 0) {
            int extra = fgetc(high);
            if (ferror(high)) {
                fprintf(stderr, "lift: ROM cache comparison: high ROM read failed\n");
                return -1;
            }
            return extra == EOF ? 1 : 0;
        }
        if (n % 2u != 0)
            return 0;
        high_n = fread(high_bytes, 1, n, high);
        image_n = fread(image_bytes, 1, n * 2u, image);
        if (ferror(high) || ferror(image)) {
            fprintf(stderr, "lift: ROM cache comparison: high ROM or image read failed\n");
            return -1;
        }
        if (high_n != n || image_n != n * 2u)
            return 0;
        for (i = 0; i < n; i += 2u) {
            size_t offset = i * 2u;
            if (image_bytes[offset] != low_bytes[i]
                || image_bytes[offset + 1u] != low_bytes[i + 1u]
                || image_bytes[offset + 2u] != high_bytes[i]
                || image_bytes[offset + 3u] != high_bytes[i + 1u])
                return 0;
        }
    }
}
