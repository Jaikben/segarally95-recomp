#ifndef TEST_VITA2D_H
#define TEST_VITA2D_H

#include <stddef.h>

typedef int SceUID;
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 0x09408060
#define SCE_GXM_TEXTURE_ALIGNMENT 16u
#define SCE_GXM_MEMORY_ATTRIB_READ 1u
#define RGBA8(r,g,b,a) ((unsigned)(r) | ((unsigned)(g) << 8) | ((unsigned)(b) << 16) | ((unsigned)(a) << 24))
enum {
    SCE_GXM_TEXTURE_FILTER_POINT,
    SCE_GXM_TEXTURE_FORMAT_P8_ABGR,
    SCE_GXM_TEXTURE_ADDR_MIRROR,
    SCE_GXM_TEXTURE_ADDR_REPEAT,
    SCE_GXM_PRIMITIVE_TRIANGLES,
    SCE_GXM_DEPTH_FUNC_ALWAYS,
    SCE_GXM_DEPTH_WRITE_DISABLED
};
typedef struct { void *palette; unsigned u, v, w, h; const void *data; } SceGxmTexture;
typedef struct { int unused; } SceGxmContext;
typedef struct {
    SceGxmTexture gxm_tex;
    unsigned w, h, stride, format;
    void *data;
} vita2d_texture;
typedef struct { float x, y, z, u, v; } vita2d_texture_vertex;
typedef struct { float x, y, z; unsigned color; } vita2d_color_vertex;

int vita2d_init_advanced(unsigned size);
void vita2d_set_vblank_wait(int enable);
void vita2d_set_clear_color(unsigned color);
vita2d_texture *vita2d_create_empty_texture(unsigned w, unsigned h);
vita2d_texture *vita2d_create_empty_texture_format(unsigned w, unsigned h, unsigned format);
void vita2d_texture_set_filters(vita2d_texture *t, unsigned a, unsigned b);
void vita2d_free_texture(vita2d_texture *t);
unsigned vita2d_texture_get_stride(const vita2d_texture *t);
void *vita2d_texture_get_datap(const vita2d_texture *t);
void *vita2d_pool_memalign(unsigned size, unsigned alignment);
unsigned vita2d_pool_free_space(void);
int sceGxmTextureSetPalette(SceGxmTexture *t, void *palette);
int sceGxmTextureInitLinear(SceGxmTexture *t, const void *data, unsigned format,
    unsigned w, unsigned h, unsigned mip_count);
SceUID sceKernelAllocMemBlock(const char *name, unsigned type, unsigned size, const void *options);
int sceKernelGetMemBlockBase(SceUID uid, void **base);
int sceKernelFreeMemBlock(SceUID uid);
int sceGxmMapMemory(void *base, unsigned size, unsigned attributes);
int sceGxmUnmapMemory(void *base);
int sceGxmTextureSetUAddrMode(SceGxmTexture *t, unsigned mode);
int sceGxmTextureSetVAddrMode(SceGxmTexture *t, unsigned mode);
void vita2d_draw_array_textured(const vita2d_texture *t, unsigned mode,
    const vita2d_texture_vertex *v, size_t n, unsigned color);
void vita2d_draw_array(unsigned mode, const vita2d_color_vertex *v, size_t n);
void vita2d_wait_rendering_done(void);
void vita2d_start_drawing(void);
void vita2d_end_drawing(void);
void vita2d_swap_buffers(void);
void vita2d_clear_screen(void);
SceGxmContext *vita2d_get_context(void);
void sceGxmSetFrontDepthFunc(SceGxmContext *c, unsigned mode);
void sceGxmSetBackDepthFunc(SceGxmContext *c, unsigned mode);
void sceGxmSetFrontDepthWriteEnable(SceGxmContext *c, unsigned mode);
void sceGxmSetBackDepthWriteEnable(SceGxmContext *c, unsigned mode);
void vita2d_draw_texture_scale(const vita2d_texture *t, float x, float y, float sx, float sy);
void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned color);
void vita2d_draw_texture_tint_part_scale(const vita2d_texture *t, float x, float y,
    float tx, float ty, float w, float h, float sx, float sy, unsigned color);
int vita2d_fini(void);

#endif
