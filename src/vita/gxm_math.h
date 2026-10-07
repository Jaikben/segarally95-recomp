#ifndef SEGAMOD2_GXM_MATH_H
#define SEGAMOD2_GXM_MATH_H

#include <math.h>
#include <string.h>
#include "model2_geo.h"

typedef struct {
    float x, y, z, u, v;
} vita_camera_vertex_t;

/* Screen-space attributes remain divided by Z until tessellation. */
typedef struct {
    float x, y, q, uq, vq;
} vita_screen_vertex_t;

static inline unsigned vita_clip_near(const vita_camera_vertex_t in[3],
                                      vita_camera_vertex_t out[4])
{
    unsigned i, n = 0;
    const float near_z = 0.01f;
    if (in[0].z >= near_z && in[1].z >= near_z && in[2].z >= near_z) {
        memcpy(out, in, 3u * sizeof(*out));
        return 3;
    }
    for (i = 0; i < 3; i++) {
        const vita_camera_vertex_t *a = &in[i], *b = &in[(i + 1) % 3];
        if (a->z >= near_z)
            out[n++] = *a;
        if ((a->z >= near_z) != (b->z >= near_z)) {
            float t = (near_z - a->z) / (b->z - a->z);
            out[n++] = (vita_camera_vertex_t){
                a->x + t * (b->x - a->x), a->y + t * (b->y - a->y),
                near_z, a->u + t * (b->u - a->u), a->v + t * (b->v - a->v)
            };
        }
    }
    return n;
}

static inline vita_screen_vertex_t vita_project(vita_camera_vertex_t v,
                                                const model2_geo_projection_t *p)
{
    float q = 1.0f / v.z;
    return (vita_screen_vertex_t){
        (float)p->center[0] + v.x * q,
        (float)p->center[1] - v.y * q, q, v.u * q, v.v * q
    };
}

static inline vita_screen_vertex_t vita_screen_mix(vita_screen_vertex_t a,
                                                   vita_screen_vertex_t b, float t)
{
    return (vita_screen_vertex_t){
        a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
        a.q + (b.q - a.q) * t, a.uq + (b.uq - a.uq) * t,
        a.vq + (b.vq - a.vq) * t
    };
}

/* Near-plane clipping can produce a quad; four screen planes add at most four vertices. */
static inline unsigned vita_clip_screen(vita_screen_vertex_t *vertices, unsigned n,
                                        const model2_geo_projection_t *p)
{
    vita_screen_vertex_t tmp[12];
    unsigned plane, i;
    for (i = 0; i < n; i++)
        if (!(vertices[i].x >= p->viewport[0] && vertices[i].y >= p->viewport[1]
            && vertices[i].x <= p->viewport[2] && vertices[i].y <= p->viewport[3]))
            break;
    if (i == n)
        return n;
    for (plane = 0; plane < 4 && n; plane++) {
        unsigned count = 0;
        float edge = (float)p->viewport[plane];
        for (i = 0; i < n; i++) {
            vita_screen_vertex_t a = vertices[i], b = vertices[(i + 1) % n];
            float av = (plane & 1) ? a.y : a.x;
            float bv = (plane & 1) ? b.y : b.x;
            int ai = plane < 2 ? av >= edge : av <= edge;
            int bi = plane < 2 ? bv >= edge : bv <= edge;
            if (ai)
                tmp[count++] = a;
            if (ai != bi)
                tmp[count++] = vita_screen_mix(a, b, (edge - av) / (bv - av));
        }
        memcpy(vertices, tmp, count * sizeof(*vertices));
        n = count;
    }
    return n;
}

static inline vita_screen_vertex_t vita_barycentric(const vita_screen_vertex_t v[3],
                                                    float b, float c)
{
    float a = 1.0f - b - c;
    return (vita_screen_vertex_t){
        a * v[0].x + b * v[1].x + c * v[2].x,
        a * v[0].y + b * v[1].y + c * v[2].y,
        a * v[0].q + b * v[1].q + c * v[2].q,
        a * v[0].uq + b * v[1].uq + c * v[2].uq,
        a * v[0].vq + b * v[1].vq + c * v[2].vq
    };
}

static inline unsigned vita_perspective_subdivisions(const vita_screen_vertex_t v[3],
                                                     float sx, float sy)
{
    if (v[0].q == v[1].q && v[1].q == v[2].q)
        return 1;
    float min_q = v[0].q, max_q = v[0].q;
    float min_x = v[0].x, max_x = v[0].x;
    float min_y = v[0].y, max_y = v[0].y;
    unsigned i;
    for (i = 1; i < 3; i++) {
        min_q = fminf(min_q, v[i].q);
        max_q = fmaxf(max_q, v[i].q);
        min_x = fminf(min_x, v[i].x);
        max_x = fmaxf(max_x, v[i].x);
        min_y = fminf(min_y, v[i].y);
        max_y = fmaxf(max_y, v[i].y);
    }
    /* Retain the existing quality floor; increase detail for steep road spans. */
    if (max_q > 3.0f * min_q
        && fmaxf((max_x - min_x) * sx, (max_y - min_y) * sy) > 320.0f)
        return 8;
    return 4;
}

static inline unsigned vita_swap_rb(unsigned p)
{
    return (p & 0xff00ff00u) | ((p & 0xffu) << 16) | ((p >> 16) & 0xffu);
}

#endif
