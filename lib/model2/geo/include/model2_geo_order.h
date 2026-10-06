#ifndef MODEL2_GEO_ORDER_H
#define MODEL2_GEO_ORDER_H

#include "model2_geo.h"

unsigned model2_geo_zval(float value);
int model2_geo_depth_compare(const model2_geo_tri_mat_t *a, unsigned ia,
                             const model2_geo_tri_mat_t *b, unsigned ib);
int model2_geo_order_compare(const model2_geo_tri_mat_t *a, unsigned ia,
                             const model2_geo_tri_mat_t *b, unsigned ib);

#endif
