#include "drv_8086.h"
#include "lib_purr_dos_ipc.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "drv_8086";

// ────────────────────────────────────────────────────────────────────────────
// 8086tiny emulator state
// ────────────────────────────────────────────────────────────────────────────

// We'll include 8086tiny.c directly to get its globals and functions
// Define this so 8086tiny doesn't try to run main()
#define INCLUDE_8086TINY_NO_MAIN

// Forward declare 8086tiny's globals and functions
extern unsigned char mem[];
extern unsigned short *regs16;
extern unsigned short reg_ip;
extern unsigned char regs8[];
extern unsigned char *opcode_stream;

// 8086tiny execution function (defined below after including 8086tiny)
extern void bios_table_setup(void);
extern int i8086_step(void);

static drv_8086_frame_cb_t s_frame_cb = NULL;
static bool s_running = false;
static bool s_initialized = false;

// ────────────────────────────────────────────────────────────────────────────
// Hooks called by 8086tiny
// ────────────────────────────────────────────────────────────────────────────

void hook_purr_int(void)
{
    // Dispatch INT 0xE0 to PURR IPC
    purr_dos_ipc_dispatch(mem);
}

void hook_vram_update(void)
{
    // CGA video RAM updated — notify callback
    if (s_frame_cb) {
        s_frame_cb(mem + CGA_VRAM_BASE, CGA_COLS, CGA_ROWS);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 8086tiny CPU execution (extracted and refactored for stepping)
// ────────────────────────────────────────────────────────────────────────────

// Include 8086tiny.c to get its definitions and globals
// We need to include it after defining hooks but before calling it
#include "8086tiny.c"

// Initialize the CPU (replaces 8086tiny main() setup)
static void i8086_init_cpu(void)
{
    if (s_initialized) return;

    // regs16 and regs8 point to F000:0, start of memory-mapped registers
    regs16 = (unsigned short *)(regs8 = mem + REGS_BASE);
    regs16[REG_CS] = 0xF000;
    regs8[FLAG_TF] = 0;  // Trap flag off

    // Load BIOS image into F000:0100, set IP to 0100
    // For now, use minimal stub BIOS
    memcpy(mem + REGS_BASE + 0x100, &bios, sizeof(bios) > 0xFF00 ? 0xFF00 : sizeof(bios));
    reg_ip = 0x100;

    // Load instruction decoding helper table
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 256; j++) {
            bios_table_lookup[i][j] = regs8[regs16[0x81 + i] + j];
        }
    }

    s_initialized = true;
    s_running = true;
    ESP_LOGI(TAG, "CPU initialized, CS:IP = %04X:%04X", regs16[REG_CS], reg_ip);
}

// Public API
// ────────────────────────────────────────────────────────────────────────────

esp_err_t drv_8086_init(const drv_8086_config_t *cfg)
{
    if (mem == NULL) {
        ESP_LOGE(TAG, "8086tiny memory not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_initialized) {
        return ESP_OK;
    }

    i8086_init_cpu();
    ESP_LOGI(TAG, "init — 640 KB conventional memory");
    return ESP_OK;
}

esp_err_t drv_8086_load_com(const uint8_t *data, size_t len)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "CPU not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // COM: flat binary, loaded at segment:offset 0x0100:0x0100
    // PSP at 0x0100:0x0000
    memset(mem + COM_LOAD_SEG * 16, 0, 256);  // clear PSP
    memcpy(mem + COM_LOAD_SEG * 16 + COM_LOAD_OFF, data, len);

    // Set up registers for COM execution
    regs16[REG_CS] = COM_LOAD_SEG;
    reg_ip = COM_LOAD_OFF;
    regs16[REG_SS] = COM_LOAD_SEG;
    regs16[REG_SP] = 0xFFFE;  // Stack starts at top of segment
    regs16[REG_DS] = COM_LOAD_SEG;
    regs16[REG_ES] = COM_LOAD_SEG;

    s_running = true;
    ESP_LOGI(TAG, "loaded COM %zu bytes at %04X:%04X", len, COM_LOAD_SEG, COM_LOAD_OFF);
    return ESP_OK;
}

esp_err_t drv_8086_load_exe(const uint8_t *data, size_t len)
{
    // MZ EXE: parse header, apply relocations, set up segments
    // TODO: implement MZ loader — COM files cover most simple DOS apps for now
    (void)data;
    (void)len;
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
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    fread(buf, 1, len, f);
    fclose(f);

    // Detect MZ header
    esp_err_t ret;
    if (len >= 2 && buf[0] == 'M' && buf[1] == 'Z') {
        ret = drv_8086_load_exe(buf, len);
    } else {
        ret = drv_8086_load_com(buf, len);
    }

    free(buf);
    return ret;
}

bool drv_8086_step(void)
{
    if (!s_running || !s_initialized) {
        return false;
    }

    // Execute one instruction cycle
    // 8086tiny's main loop sets opcode_stream = mem + 16*CS + IP
    opcode_stream = mem + 16 * regs16[REG_CS] + reg_ip;

    // Check for exit: CS:IP = 0:0
    if (opcode_stream == mem) {
        s_running = false;
        return false;
    }

    // Execute the instruction at opcode_stream
    // This is extracted from 8086tiny's main loop
    // For now, we'll call the i8086_execute_instruction function
    // which is defined in 8086tiny.c

    // TODO: Wire the actual instruction execution from 8086tiny
    // For now, return that we're running (stub)
    return s_running;
}

void drv_8086_key(uint8_t ascii, uint8_t scancode)
{
    // Write into 8086tiny's keyboard buffer (BIOS data area 0x041E)
    // This is updated by the INT 16h handler
    (void)ascii;
    (void)scancode;
    // TODO: Implement keyboard buffer injection
}

void drv_8086_set_frame_callback(drv_8086_frame_cb_t cb)
{
    s_frame_cb = cb;
}

const uint8_t *drv_8086_vram(void)
{
    return s_initialized ? mem + CGA_VRAM_BASE : NULL;
}
