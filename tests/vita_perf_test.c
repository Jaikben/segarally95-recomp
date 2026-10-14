#include "perf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL: %s\n", message);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    FILE *file;
    char output[8192];
    size_t length;
    const char *second;
    const char *third;

    require(argc == 2, "expected log path");
    require(freopen(argv[1], "w", stderr) != NULL, "redirecting performance log");

    vita_perf_frame_begin();
    vita_perf_add(VITA_PERF_CACHE_HITS, 3);
    vita_perf_add(VITA_PERF_SORT_US, 25);
    vita_perf_add(VITA_PERF_SOLID_DRAWS, 2);
    vita_perf_add(VITA_PERF_SOLID_TRIS, 12);
    vita_perf_presented(100);
    vita_perf_frame_begin();
    vita_perf_presented(400100);
    require(fflush(stderr) == 0 && ftell(stderr) == 0,
            "no summary before one second");
    vita_perf_frame_begin();
    vita_perf_presented(1000100);

    vita_perf_frame_begin();
    vita_perf_add(VITA_PERF_CACHE_MISSES, 1);
    vita_perf_presented(2000100);

    /* A menu present resets counters and excludes its long interval. */
    vita_perf_add(VITA_PERF_CACHE_HITS, 99);
    vita_perf_presented(5000100);
    vita_perf_frame_begin();
    vita_perf_presented(9000100);
    vita_perf_frame_begin();
    vita_perf_presented(10000100);

    require(fflush(stderr) == 0, "flushing performance log");
    file = fopen(argv[1], "r");
    require(file != NULL, "reading performance log");
    length = fread(output, 1, sizeof(output) - 1, file);
    output[length] = '\0';
    require(!ferror(file), "reading complete performance log");
    fclose(file);
    require(strstr(output, "window_us=1000000 frames=3 interval_avg_us=500000 "
                   "interval_max_us=600000") != NULL, "window and intervals");
    require(strstr(output, "sort=25") != NULL, "timing total");
    require(strstr(output, "CACHE hits=3 misses=0") != NULL, "cache total");
    require(strstr(output, "solid=2/12") != NULL, "draw and triangle totals");
    second = strstr(output, "\n");
    require(second != NULL, "first summary");
    second++;
    require(strstr(second, "frames=1 interval_avg_us=1000000") != NULL,
            "interval across adjacent windows");
    require(strstr(second, "CACHE hits=0 misses=1") != NULL, "window reset");
    third = strstr(second, "\n");
    require(third != NULL, "second summary");
    third++;
    require(strstr(third, "frames=2 interval_avg_us=1000000") != NULL,
            "menu interval excluded");
    require(strstr(third, "CACHE hits=0 misses=0") != NULL, "menu counters reset");
    require(strstr(third, "hits=99") == NULL, "no menu contamination");
    require(fclose(stderr) == 0, "closing performance log");
    require(remove(argv[1]) == 0, "removing test log");
    puts("Vita performance logging tests passed");
    return 0;
}
