#include "magidos_cga.h"
#include <string.h>

// CGA 16-colour palette in RGB565 (standard IBM CGA colours)
const uint16_t cga_palette_rgb565[16] = {
    0x0000, // 0  black
    0x0015, // 1  blue
    0x0540, // 2  green
    0x0555, // 3  cyan
    0xA800, // 4  red
    0xA815, // 5  magenta
    0xAAA0, // 6  brown
    0xAD55, // 7  light grey
    0x5AAB, // 8  dark grey
    0x555F, // 9  bright blue
    0x57E0, // 10 bright green
    0x57FF, // 11 bright cyan
    0xF800, // 12 bright red
    0xF81F, // 13 bright magenta
    0xFFE0, // 14 yellow
    0xFFFF, // 15 white
};

// Minimal 8x8 font — IBM CP437 subset, printable ASCII 0x20-0x7E
// Each glyph is 8 bytes (1 bit per pixel, MSB = leftmost pixel).
// This is a placeholder: a full CP437 ROM is 2KB and can be loaded from SPIFFS.
static const uint8_t s_font8x8[96][8] = {
    // 0x20 space
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // 0x21 !
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    // remaining glyphs: fill with zeros for now — replace with full CP437 data
    [2 ... 95] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

void magidos_cga_render(const uint8_t *vram, int cols, int rows,
                         uint16_t *out, int out_w, int out_h)
{
    // Each CGA text cell is 2 bytes: [char][attr]
    // attr: bits 7-4 = background colour index, bits 3-0 = foreground colour index
    // We render each glyph as CGA_CELL_W x CGA_CELL_H pixels, then scale to out_w x out_h.
    // For simplicity: render to a temporary 480x200 buffer then bilinear-scale.
    // On CYD (240x320 window) we letterbox the 80x25 text grid.

    const int cell_w = out_w / cols;    // ~3px per cell at 240px wide
    const int cell_h = out_h / rows;    // ~12px per cell at 320px tall

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            uint8_t ch   = vram[(row * cols + col) * 2 + 0];
            uint8_t attr = vram[(row * cols + col) * 2 + 1];
            uint8_t fg   = attr & 0x0F;
            uint8_t bg   = (attr >> 4) & 0x0F;

            uint16_t fg_col = cga_palette_rgb565[fg];
            uint16_t bg_col = cga_palette_rgb565[bg];

            // Clamp to printable range for the stub font
            const uint8_t *glyph = (ch >= 0x20 && ch <= 0x7E)
                                    ? s_font8x8[ch - 0x20]
                                    : s_font8x8[0];

            for (int py = 0; py < cell_h; py++) {
                int gy = (py * 8) / cell_h;  // map pixel row to glyph row
                uint8_t row_bits = glyph[gy];
                int y_out = row * cell_h + py;
                if (y_out >= out_h) continue;

                for (int px = 0; px < cell_w; px++) {
                    int gx = (px * 8) / cell_w;  // map pixel col to glyph col
                    bool lit = (row_bits >> (7 - gx)) & 1;
                    int x_out = col * cell_w + px;
                    if (x_out >= out_w) continue;
                    out[y_out * out_w + x_out] = lit ? fg_col : bg_col;
                }
            }
        }
    }
}
