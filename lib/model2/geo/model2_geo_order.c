#include "model2_geo_order.h"
#include "model2_geo_tex.h"

#include <stdint.h>
#include <string.h>

unsigned model2_geo_zval(float value)
{
    u32 bits, mantissa;
    int exponent;
    memcpy(&bits, &value, sizeof(bits));
    exponent = (int)((bits >> 23) & 0xffu);
    mantissa = (bits & 0x7fffffu) + 0x400u;
    if (mantissa > 0x7fffffu) {
        exponent++;
        mantissa = (mantissa & 0x7fffffu) >> 1;
    }
    mantissa >>= 11;
    if ((int32_t)bits < 0)
        return 0;
    if (exponent < 15)
        return ((unsigned)(exponent + 1) << 12) | mantissa;
    return 0xffffu;
}

int model2_geo_depth_compare(const model2_geo_tri_mat_t *a, unsigned ia,
                             const model2_geo_tri_mat_t *b, unsigned ib)
{
    unsigned za = model2_geo_zval(a->z_sort), zb = model2_geo_zval(b->z_sort);
    if (za != zb)
        return za < zb ? -1 : 1;
    if (za == 0xffffu && a->z_sort != b->z_sort)
        return a->z_sort < b->z_sort ? -1 : 1;
    return ia == ib ? 0 : (ia > ib ? -1 : 1);
}

int model2_geo_order_compare(const model2_geo_tri_mat_t *a, unsigned ia,
                             const model2_geo_tri_mat_t *b, unsigned ib)
{
    int ka = (a->flags & MODEL2_GEO_TEX_TEXTURED)
                 ? ((a->flags & MODEL2_GEO_TEX_CUTOUT) ? 2 : 1) : 0;
    int kb = (b->flags & MODEL2_GEO_TEX_TEXTURED)
                 ? ((b->flags & MODEL2_GEO_TEX_CUTOUT) ? 2 : 1) : 0;
    if ((a->pad & 1u) != (b->pad & 1u))
        return (a->pad & 1u) ? 1 : -1;
    if (ka != kb)
        return ka < kb ? -1 : 1;
    return model2_geo_depth_compare(a, ia, b, ib);
}
