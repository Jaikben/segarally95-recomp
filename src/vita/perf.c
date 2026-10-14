#include "perf.h"
#include "startup_log.h"

#include <stdio.h>
#include <string.h>

static uint64_t g_totals[VITA_PERF_METRIC_COUNT];
static uint64_t g_window_start, g_last_present, g_interval_sum, g_interval_max;
static unsigned g_frames, g_intervals;
static int g_frame_active;

void vita_perf_add(enum vita_perf_metric metric, uint64_t value)
{
    g_totals[metric] += value;
}

void vita_perf_frame_begin(void)
{
    g_frame_active = 1;
}

void vita_perf_presented(uint64_t now)
{
    char line[1536];

    /* Menu/loading presents must not contaminate gameplay windows. */
    if (!g_frame_active) {
        memset(g_totals, 0, sizeof(g_totals));
        g_window_start = g_last_present = g_interval_sum = g_interval_max = 0;
        g_frames = g_intervals = 0;
        return;
    }
    g_frame_active = 0;
    if (!g_window_start)
        g_window_start = now;
    if (g_last_present) {
        uint64_t interval = now - g_last_present;

        g_interval_sum += interval;
        if (interval > g_interval_max)
            g_interval_max = interval;
        g_intervals++;
    }
    g_last_present = now;
    g_frames++;
    if (now - g_window_start < 1000000u)
        return;

#define P(metric) ((unsigned long long)g_totals[VITA_PERF_##metric])
    snprintf(line, sizeof(line),
             "VITA-PERF window_us=%llu frames=%u interval_avg_us=%llu "
             "interval_max_us=%llu CPU_TOTAL_US sort=%llu pack=%llu "
             "texture_bake_upload=%llu draw_submit=%llu tile_prep=%llu "
             "tile_convert_upload=%llu geo_layer=%llu swap=%llu "
             "CACHE hits=%llu misses=%llu evictions=%llu uploads=%llu "
             "upload_bytes=%llu tile_upload_bytes=%llu "
             "DRAWS/TRIS solid=%llu/%llu opaque=%llu/%llu "
             "cutout=%llu/%llu overlay=%llu/%llu\n",
             (unsigned long long)(now - g_window_start), g_frames,
             (unsigned long long)(g_intervals ? g_interval_sum / g_intervals : 0),
             (unsigned long long)g_interval_max,
             P(SORT_US), P(PACK_US), P(TEXTURE_US), P(DRAW_US),
             P(TILE_PREP_US), P(TILE_UPLOAD_US), P(GEO_US), P(SWAP_US),
             P(CACHE_HITS), P(CACHE_MISSES), P(CACHE_EVICTIONS), P(UPLOADS),
             P(UPLOAD_BYTES), P(TILE_UPLOAD_BYTES),
             P(SOLID_DRAWS), P(SOLID_TRIS), P(OPAQUE_DRAWS), P(OPAQUE_TRIS),
             P(CUTOUT_DRAWS), P(CUTOUT_TRIS), P(OVERLAY_DRAWS), P(OVERLAY_TRIS));
#undef P
    vita_startup_log(line);
    memset(g_totals, 0, sizeof(g_totals));
    g_window_start = now;
    g_interval_sum = g_interval_max = 0;
    g_frames = g_intervals = 0;
}
