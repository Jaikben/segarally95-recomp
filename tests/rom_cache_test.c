#include "model2_rom_interleave.h"

#include <stdlib.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

static FILE *stream(const unsigned char *bytes, size_t size)
{
    FILE *file = tmpfile();
    CHECK(file);
    CHECK(fwrite(bytes, 1, size, file) == size);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    return file;
}

static int compare(const unsigned char *image, size_t image_size,
                   const unsigned char *low, size_t low_size,
                   const unsigned char *high, size_t high_size)
{
    FILE *i = stream(image, image_size);
    FILE *l = stream(low, low_size);
    FILE *h = stream(high, high_size);
    int result = model2_rom_interleave_matches(i, l, h);
    CHECK(fclose(i) == 0);
    CHECK(fclose(l) == 0);
    CHECK(fclose(h) == 0);
    return result;
}

int main(void)
{
    unsigned char low[8194], high[8194], image[16388];
    unsigned i;
    FILE *joined, *l, *h;
    for (i = 0; i < sizeof(low); i++) {
        low[i] = (unsigned char)(i * 17u);
        high[i] = (unsigned char)(i * 31u + 5u);
    }
    for (i = 0; i < sizeof(low); i += 2u) {
        image[i * 2u] = low[i];
        image[i * 2u + 1u] = low[i + 1u];
        image[i * 2u + 2u] = high[i];
        image[i * 2u + 3u] = high[i + 1u];
    }
    CHECK(compare(image, sizeof(image), low, sizeof(low), high, sizeof(high)) == 1);
    image[8888] ^= 1;
    CHECK(compare(image, sizeof(image), low, sizeof(low), high, sizeof(high)) == 0);
    image[8888] ^= 1;
    image[16387] ^= 1;
    CHECK(compare(image, sizeof(image), low, sizeof(low), high, sizeof(high)) == 0);
    image[16387] ^= 1;
    CHECK(compare(image, sizeof(image) - 1u, low, sizeof(low), high, sizeof(high)) == 0);
    CHECK(compare(image, sizeof(image), low, sizeof(low), high, sizeof(high) - 2u) == 0);
    CHECK(compare(image, sizeof(image), low, sizeof(low) - 2u, high, sizeof(high)) == 0);
    CHECK(compare(image, sizeof(image), low, sizeof(low) - 1u, high, sizeof(high) - 1u) == 0);
    joined = tmpfile();
    CHECK(joined);
    CHECK(fwrite(image, 1, sizeof(image), joined) == sizeof(image));
    CHECK(fwrite(image, 1, sizeof(image), joined) == sizeof(image));
    CHECK(fseek(joined, 0, SEEK_SET) == 0);
    for (i = 0; i < 2u; i++) {
        l = stream(low, sizeof(low));
        h = stream(high, sizeof(high));
        CHECK(model2_rom_interleave_matches(joined, l, h) == 1);
        CHECK(fclose(l) == 0 && fclose(h) == 0);
    }
    CHECK(fgetc(joined) == EOF && !ferror(joined));
    CHECK(fclose(joined) == 0);
    puts("ROM cache valid, same-size corruption, truncation and sequential-pair tests passed");
    return 0;
}
