#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Render a CGA text-mode framebuffer (80x25 char+attr pairs) into an RGB565
// pixel buffer sized to the MiniWin window dimensions.
// out_w / out_h: destination pixel dimensions (the window's draw area).
void magidos_cga_render(const uint8_t *vram, int cols, int rows,
                         uint16_t *out, int out_w, int out_h);

// Standard CGA 16-colour palette as RGB565
extern const uint16_t cga_palette_rgb565[16];

#ifdef __cplusplus
}
#endif
