#ifndef SEGAMOD2_GXM_ORDER_H
#define SEGAMOD2_GXM_ORDER_H

#include "model2_geo_order.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t key;
    unsigned index;
} vita_painter_entry_t;

static inline int vita_painter_key(const model2_geo_tri_mat_t *m, uint64_t *key)
{
    unsigned depth = model2_geo_zval(m->z_sort);
    uint32_t bits = 0;
    if (!isfinite(m->z_sort))
        return 0;
    /* Saturated hardware depths retain the existing positive-float tie break. */
    if (depth == 0xffffu)
        memcpy(&bits, &m->z_sort, sizeof(bits));
    *key = ((uint64_t)(m->pad & 1u) << 48)
        | ((uint64_t)(0xffffu - depth) << 32)
        | (depth == 0xffffu ? UINT32_MAX - bits : 0u);
    return 1;
}

/* Entries start in ascending index order; stable sorting preserves depth ties. */
static inline const vita_painter_entry_t *vita_painter_sort(
    vita_painter_entry_t *entries, vita_painter_entry_t *scratch, unsigned count)
{
    unsigned i, digit;
    vita_painter_entry_t *source = entries, *destination = scratch;
    if (count <= 32) {
        for (i = 1; i < count; i++) {
            vita_painter_entry_t entry = entries[i];
            unsigned pos = i;
            while (pos && entry.key < entries[pos - 1].key) {
                entries[pos] = entries[pos - 1];
                pos--;
            }
            entries[pos] = entry;
        }
        return entries;
    }
    unsigned buckets[7][256] = {{0}};
    for (i = 0; i < count; i++)
        for (digit = 0; digit < 7; digit++)
            buckets[digit][(entries[i].key >> (digit * 8u)) & 255u]++;
    for (digit = 0; digit < 7; digit++) {
        unsigned total = 0, bucket;
        int uniform = 0;
        vita_painter_entry_t *swap;
        for (bucket = 0; bucket < 256; bucket++) {
            unsigned size = buckets[digit][bucket];
            uniform |= size == count;
            buckets[digit][bucket] = total;
            total += size;
        }
        if (uniform)
            continue;
        for (i = 0; i < count; i++) {
            bucket = (unsigned)((source[i].key >> (digit * 8u)) & 255u);
            destination[buckets[digit][bucket]++] = source[i];
        }
        swap = source;
        source = destination;
        destination = swap;
    }
    return source;
}

#endif
