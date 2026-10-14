#ifndef SEGAMOD2_VITA_PERF_H
#define SEGAMOD2_VITA_PERF_H

#include <stdint.h>

enum vita_perf_metric {
    VITA_PERF_SORT_US,
    VITA_PERF_PACK_US,
    VITA_PERF_TEXTURE_US,
    VITA_PERF_CACHE_HITS,
    VITA_PERF_CACHE_MISSES,
    VITA_PERF_CACHE_EVICTIONS,
    VITA_PERF_UPLOADS,
    VITA_PERF_UPLOAD_BYTES,
    VITA_PERF_SOLID_DRAWS,
    VITA_PERF_SOLID_TRIS,
    VITA_PERF_OPAQUE_DRAWS,
    VITA_PERF_OPAQUE_TRIS,
    VITA_PERF_CUTOUT_DRAWS,
    VITA_PERF_CUTOUT_TRIS,
    VITA_PERF_OVERLAY_DRAWS,
    VITA_PERF_OVERLAY_TRIS,
    VITA_PERF_DRAW_US,
    VITA_PERF_TILE_PREP_US,
    VITA_PERF_TILE_UPLOAD_US,
    VITA_PERF_TILE_UPLOAD_BYTES,
    VITA_PERF_GEO_US,
    VITA_PERF_SWAP_US,
    VITA_PERF_METRIC_COUNT
};

#if defined(SEGAMOD2_VITA_PERF)
uint64_t vita_perf_now(void);
void vita_perf_add(enum vita_perf_metric metric, uint64_t value);
void vita_perf_frame_begin(void);
void vita_perf_presented(uint64_t now);
#else
static inline uint64_t vita_perf_now(void) { return 0; }
static inline void vita_perf_add(enum vita_perf_metric metric, uint64_t value)
{
    (void)metric;
    (void)value;
}
static inline void vita_perf_frame_begin(void) {}
static inline void vita_perf_presented(uint64_t now) { (void)now; }
#endif

#endif
