// MagicMac Settings App
// Manages magicmac.json: boot disk, WiFi, BT, and other emulator settings

#include "app_magicmac_settings.h"
#include "miniwin.h"
#include "miniwin_utilities.h"
#include "gl/gl.h"
#include "purr_apps_common.h"
#include "purr_taskbar.h"
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>

static const char* CONFIG_PATH = "/sdcard/magicmac/magicmac.json";
static const char* MAGICMAC_DIR = "/sdcard/magicmac";

static mw_handle_t s_handle = MW_INVALID_HANDLE;

typedef struct {
    char boot_disk[256];
    bool wifi_enabled;
    bool bt_enabled;
    bool autostart;
} magicmac_config_t;

static magicmac_config_t config = {
    .boot_disk = "/sdcard/magicmac/meow.dsk",
    .wifi_enabled = false,
    .bt_enabled = false,
    .autostart = false
};

// Simple JSON string value extractor
static bool json_get_string(const char* json, const char* key, char* value, size_t len)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":%s*\"", key);
    const char* p = json;
    while ((p = strstr(p, search)) != NULL) {
        p += strlen(key) + 3;  // Skip past "key":
        while (*p && isspace(*p)) p++;
        if (*p == '"') {
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i < len - 1) {
                value[i++] = *p++;
            }
            value[i] = '\0';
            return true;
        }
        p++;
    }
    return false;
}

static bool json_get_bool(const char* json, const char* key)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":%s*", key);
    const char* p = json;
    while ((p = strstr(p, search)) != NULL) {
        p += strlen(key) + 2;  // Skip past "key":
        while (*p && isspace(*p)) p++;
        return strncmp(p, "true", 4) == 0;
    }
    return false;
}

static void load_config()
{
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) {
        return;  // Use defaults
    }

    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    // Simple string-based JSON parsing
    if (!json_get_string(buf, "boot_disk", config.boot_disk, sizeof(config.boot_disk))) {
        strlcpy(config.boot_disk, "/sdcard/magicmac/meow.dsk", sizeof(config.boot_disk));
    }
    config.wifi_enabled = json_get_bool(buf, "wifi_enabled");
    config.bt_enabled = json_get_bool(buf, "bt_enabled");
    config.autostart = json_get_bool(buf, "autostart");
}

static void save_config()
{
    FILE* f = fopen(CONFIG_PATH, "w");
    if (!f) return;

    // Generate JSON manually
    fprintf(f, "{\n");
    fprintf(f, "  \"boot_disk\": \"%s\",\n", config.boot_disk);
    fprintf(f, "  \"wifi_enabled\": %s,\n", config.wifi_enabled ? "true" : "false");
    fprintf(f, "  \"bt_enabled\": %s,\n", config.bt_enabled ? "true" : "false");
    fprintf(f, "  \"autostart\": %s\n", config.autostart ? "true" : "false");
    fprintf(f, "}\n");
    fclose(f);
}

static void get_disk_list(char** disks, int* count)
{
    *count = 0;
    DIR* d = opendir(MAGICMAC_DIR);
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL && *count < 10) {
        const char* ext = strrchr(ent->d_name, '.');
        if (ext && strcmp(ext, ".dsk") == 0) {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", MAGICMAC_DIR, ent->d_name);
            disks[*count] = (char*)malloc(strlen(path) + 1);
            strcpy(disks[*count], path);
            (*count)++;
        }
    }
    closedir(d);
}

static void paint(mw_handle_t h, const mw_gl_draw_info_t *d)
{
    mw_util_rect_t cr = mw_get_window_client_rect(h);

    mw_gl_set_fill(MW_GL_FILL);
    mw_gl_set_border(MW_GL_BORDER_OFF);
    mw_gl_set_solid_fill_colour(WCE_BAR);
    mw_gl_rectangle(d, 0, 0, cr.width, cr.height);

    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_set_bg_transparency(MW_GL_BG_TRANSPARENT);
    mw_gl_set_font(MW_GL_FONT_12);

    mw_gl_string(d, 8, 8, "MagicMac Settings");
    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_hline(d, 4, cr.width - 4, 20);

    // Boot Disk
    mw_gl_set_fg_colour(WCE_TXT);
    mw_gl_string(d, 8, 30, "Boot Disk:");
    const char* disk_name = strrchr(config.boot_disk, '/');
    disk_name = disk_name ? disk_name + 1 : config.boot_disk;
    mw_gl_string(d, 20, 45, disk_name);

    // WiFi
    mw_gl_string(d, 8, 70, "WiFi:");
    mw_gl_string(d, 20, 85, config.wifi_enabled ? "[X] Enabled" : "[ ] Disabled");

    // BT
    mw_gl_string(d, 8, 110, "Bluetooth:");
    mw_gl_string(d, 20, 125, config.bt_enabled ? "[X] Enabled" : "[ ] Disabled");

    // Autostart
    mw_gl_string(d, 8, 150, "Autostart:");
    mw_gl_string(d, 20, 165, config.autostart ? "[X] On Boot" : "[ ] Off");

    mw_gl_set_fg_colour(WCE_SHD);
    mw_gl_string(d, 8, 190, "Click to toggle | [Save]");
}

static void message(const mw_message_t *msg)
{
    switch (msg->message_id) {
    case MW_WINDOW_CREATED_MESSAGE:
        mw_paint_window_frame(msg->recipient_handle, MW_WINDOW_FRAME_COMPONENT_ALL);
        mw_paint_window_client(msg->recipient_handle);
        break;

    case MW_TOUCH_DOWN_MESSAGE:
        // Handle settings clicks
        // For now, just toggle WiFi on any click as a demo
        config.wifi_enabled = !config.wifi_enabled;
        save_config();
        mw_paint_window_client(msg->recipient_handle);
        break;

    case MW_WINDOW_REMOVED_MESSAGE:
        taskbar_unregister(s_handle);
        s_handle = MW_INVALID_HANDLE;
        break;

    default:
        break;
    }
}

void app_magicmac_settings_launch(void)
{
    if (s_handle != MW_INVALID_HANDLE) {
        if (mw_get_window_flags(s_handle) & MW_WINDOW_FLAG_IS_MINIMISED)
            mw_set_window_minimised(s_handle, false);
        mw_bring_window_to_front(s_handle);
        return;
    }

    load_config();

    mw_util_rect_t r;
    mw_util_set_rect(&r, APP_WIN_X(220), 50, 220, 200);
    s_handle = mw_add_window(&r, "MagicMac",
        paint, message, NULL, 0, APP_WIN_FLAGS_TOUCH, NULL);
    taskbar_register(s_handle, "MagicMac");
}
