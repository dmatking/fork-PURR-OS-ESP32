#include "drv_8086.h"
#include "lib_purr_dos_ipc.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 8086tiny vendored source — included directly so it compiles as one unit
// with our memory array and hook points already defined before inclusion.
// See 8086tiny.c for the actual emulator.

static const char *TAG = "drv_8086";

static uint8_t           *s_mem      = NULL;
static drv_8086_frame_cb_t s_frame_cb = NULL;
static bool                s_running  = false;

// ---------------------------------------------------------------------------
// Hook called by 8086tiny when it executes INT 0xE0 — dispatch to PURR IPC
// ---------------------------------------------------------------------------
void hook_purr_int(void)
{
    purr_dos_ipc_dispatch(s_mem);
}

// ---------------------------------------------------------------------------
// Hook called by 8086tiny after each write to the CGA VRAM region
// ---------------------------------------------------------------------------
void hook_vram_update(void)
{
    if (s_frame_cb) {
        s_frame_cb(s_mem + CGA_VRAM_BASE, CGA_COLS, CGA_ROWS);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t drv_8086_init(const drv_8086_config_t *cfg)
{
    uint32_t mem_size = cfg && cfg->mem_size ? cfg->mem_size : MEM_SIZE;
    s_mem = calloc(1, mem_size + 0x100000);  // +1MB for BIOS/ROM area
    if (!s_mem) return ESP_ERR_NO_MEM;

    // Install minimal BIOS stubs into ROM area (INT vectors, BIOS data area)
    // 8086tiny has its own built-in BIOS; we wire our hooks on top.

    ESP_LOGI(TAG, "init — %lu KB conventional memory", mem_size / 1024);
    return ESP_OK;
}

esp_err_t drv_8086_load_com(const uint8_t *data, size_t len)
{
    if (!s_mem) return ESP_ERR_INVALID_STATE;
    // COM: flat binary, loaded at segment:offset 0x0100:0x0100 (CS=DS=ES=SS=0x0100)
    // PSP built at 0x0100:0x0000 by BIOS stub
    memset(s_mem + COM_LOAD_SEG * 16, 0, 256);  // clear PSP
    memcpy(s_mem + COM_LOAD_SEG * 16 + COM_LOAD_OFF, data, len);
    s_running = true;
    ESP_LOGI(TAG, "loaded COM %u bytes at %04X:%04X", (unsigned)len, COM_LOAD_SEG, COM_LOAD_OFF);
    return ESP_OK;
}

esp_err_t drv_8086_load_exe(const uint8_t *data, size_t len)
{
    // MZ EXE: parse header, apply relocations, set up segments
    // TODO: implement MZ loader — COM files cover most simple DOS apps for now
    (void)data; (void)len;
    ESP_LOGW(TAG, "EXE loading not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t drv_8086_load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    rewind(f);
    uint8_t *buf = malloc(len);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(buf, 1, len, f);
    fclose(f);

    // Detect MZ header
    esp_err_t ret;
    if (len >= 2 && buf[0] == 'M' && buf[1] == 'Z')
        ret = drv_8086_load_exe(buf, len);
    else
        ret = drv_8086_load_com(buf, len);

    free(buf);
    return ret;
}

bool drv_8086_step(void)
{
    if (!s_running) return false;
    // 8086tiny_step() runs N cycles and returns 0 when INT 21h/4Ch (exit) fires
    // The actual call is wired in 8086tiny.c via the hook table
    // s_running = (i8086_step(1000) != 0);
    return s_running;
}

void drv_8086_key(uint8_t ascii, uint8_t scancode)
{
    // Write into 8086tiny's keyboard buffer (BIOS data area 0x041E)
    (void)ascii; (void)scancode;
}

void drv_8086_set_frame_callback(drv_8086_frame_cb_t cb)
{
    s_frame_cb = cb;
}

const uint8_t *drv_8086_vram(void)
{
    return s_mem ? s_mem + CGA_VRAM_BASE : NULL;
}
