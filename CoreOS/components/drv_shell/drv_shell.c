#include "drv_shell.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#  include "driver/usb_serial_jtag.h"
#  define SHELL_USE_USB_JTAG 1
#else
#  include "driver/uart.h"
#  define SHELL_USE_USB_JTAG 0
#endif

static const char *TAG = "drv_shell";

// ── Command table ─────────────────────────────────────────────────────────────

typedef void (*shell_fn_t)(int argc, char **argv);

typedef struct {
    const char   *name;
    const char   *help;
    shell_fn_t    fn;
} shell_cmd_t;

// Declared in shell_cmds_*.c
void cmd_gpio_get(int argc, char **argv);
void cmd_gpio_set(int argc, char **argv);
void cmd_gpio_pulse(int argc, char **argv);
void cmd_display_init(int argc, char **argv);
void cmd_display_reset(int argc, char **argv);
void cmd_display_color(int argc, char **argv);
void cmd_display_text(int argc, char **argv);
void cmd_display_bl(int argc, char **argv);
void cmd_sys_info(int argc, char **argv);
void cmd_sys_tasks(int argc, char **argv);
void cmd_sys_heap(int argc, char **argv);
void cmd_sys_nvs_erase(int argc, char **argv);
void cmd_sys_reboot(int argc, char **argv);
void cmd_sys_panic(int argc, char **argv);
void cmd_mw_paint (int argc, char **argv);
void cmd_mw_tick  (int argc, char **argv);
void cmd_mw_rect  (int argc, char **argv);
void cmd_mw_init  (int argc, char **argv);
void cmd_mw_touch (int argc, char **argv);

// Declared in shell_cmds_conman.cpp
void cmd_wifi_status    (int argc, char **argv);
void cmd_wifi_scan      (int argc, char **argv);
void cmd_wifi_connect   (int argc, char **argv);
void cmd_wifi_disconnect(int argc, char **argv);
void cmd_wifi_forget    (int argc, char **argv);
#ifdef SHELL_HAS_BT
void cmd_bt_status  (int argc, char **argv);
void cmd_bt_scan    (int argc, char **argv);
void cmd_bt_devices (int argc, char **argv);
void cmd_bt_pair    (int argc, char **argv);
void cmd_bt_unpair  (int argc, char **argv);
#endif

static const shell_cmd_t s_cmds[] = {
    { "gpio-get",      "gpio-get <pin>              Read GPIO level",                    cmd_gpio_get      },
    { "gpio-set",      "gpio-set <pin> <0|1>        Drive GPIO high or low",             cmd_gpio_set      },
    { "gpio-pulse",    "gpio-pulse <pin> <ms>        Pulse GPIO LOW for N ms",           cmd_gpio_pulse    },
    { "display-init",  "display-init                Re-run display init sequence",       cmd_display_init  },
    { "display-reset", "display-reset <rst_pin>     Hardware-reset display via RST pin", cmd_display_reset },
    { "display-color", "display-color <hex>         Fill screen (0xF800=red 0x0000=blk)",cmd_display_color },
    { "display-text",  "display-text <row> <text>   Print text at row",                  cmd_display_text  },
    { "display-bl",    "display-bl <0-255>          Set backlight brightness",           cmd_display_bl    },
    { "info",          "info                        Chip, app, memory info",             cmd_sys_info      },
    { "tasks",         "tasks                       List FreeRTOS tasks",                cmd_sys_tasks     },
    { "heap",          "heap                        Show heap usage",                    cmd_sys_heap      },
    { "nvs-erase",     "nvs-erase                   Erase NVS partition",               cmd_sys_nvs_erase },
    { "reboot",        "reboot                      Restart the ESP32",                 cmd_sys_reboot    },
    { "panic",         "panic [blue|red] [code] [msg]  Trigger kernel panic screen",   cmd_sys_panic     },
    { "mw-paint",          "mw-paint                    Queue MiniWin full repaint",              cmd_mw_paint       },
    { "mw-tick",           "mw-tick [N]                 Process N messages (default 200)",        cmd_mw_tick        },
    { "mw-rect",           "mw-rect x y w h 0xRRGGBB    Draw rect via HAL directly",             cmd_mw_rect        },
    { "mw-init",           "mw-init                     Re-run mw_init()",                        cmd_mw_init        },
    { "mw-touch",          "mw-touch [N]                Poll touch N ticks (20ms, default 150)",  cmd_mw_touch       },
    { "wifi-status",       "wifi-status                 Show WiFi state and SSID",                cmd_wifi_status    },
    { "wifi-scan",         "wifi-scan                   Scan for nearby networks",                cmd_wifi_scan      },
    { "wifi-connect",      "wifi-connect <ssid> [pass]  Connect to a network",                   cmd_wifi_connect   },
    { "wifi-disconnect",   "wifi-disconnect             Disconnect from current network",         cmd_wifi_disconnect},
    { "wifi-forget",       "wifi-forget <ssid>          Remove saved credentials",               cmd_wifi_forget    },
#ifdef SHELL_HAS_BT
    { "bt-status",         "bt-status                   Show BT state and paired count",          cmd_bt_status      },
    { "bt-scan",           "bt-scan [ms]                Discover nearby devices (default 8s)",    cmd_bt_scan        },
    { "bt-devices",        "bt-devices                  List paired devices",                     cmd_bt_devices     },
    { "bt-pair",           "bt-pair <idx>               Pair a discovered device",                cmd_bt_pair        },
    { "bt-unpair",         "bt-unpair <idx>             Unpair a paired device",                  cmd_bt_unpair      },
#endif
};
static const int s_ncmds = sizeof(s_cmds) / sizeof(s_cmds[0]);

static int tokenize(char *line, char **argv, int max_argv)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max_argv) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

// ── Serial I/O init ───────────────────────────────────────────────────────────

static void _serial_init(void)
{
#if SHELL_USE_USB_JTAG
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = {
            .rx_buffer_size = 256,
            .tx_buffer_size = 512,
        };
        esp_err_t err = usb_serial_jtag_driver_install(&cfg);
        if (err != ESP_OK)
            ESP_LOGW(TAG, "usb_serial_jtag_driver_install: %s", esp_err_to_name(err));
    }
#else
    esp_err_t err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        ESP_LOGW(TAG, "uart_driver_install: %s", esp_err_to_name(err));
#endif
}

static void _serial_write(const char *s, int len)
{
#if SHELL_USE_USB_JTAG
    usb_serial_jtag_write_bytes(s, len, pdMS_TO_TICKS(20));
#else
    uart_write_bytes(UART_NUM_0, s, len);
#endif
}

static int _serial_read_byte(uint8_t *ch, TickType_t timeout)
{
#if SHELL_USE_USB_JTAG
    return usb_serial_jtag_read_bytes(ch, 1, timeout);
#else
    return uart_read_bytes(UART_NUM_0, ch, 1, timeout);
#endif
}

// ── Line reader (blocking, with echo + backspace) ─────────────────────────────

static int _read_line(char *buf, int maxlen)
{
    int pos = 0;
    for (;;) {
        uint8_t ch = 0;
        if (_serial_read_byte(&ch, portMAX_DELAY) <= 0)
            continue;

        if (ch == '\r' || ch == '\n') {
            _serial_write("\r\n", 2);
            break;
        }

        if (ch == 0x7f || ch == '\b') {
            if (pos > 0) { pos--; _serial_write("\b \b", 3); }
            continue;
        }

        if (ch < 0x20 || pos >= maxlen - 1) continue;

        _serial_write((const char *)&ch, 1);
        buf[pos++] = (char)ch;
    }
    buf[pos] = '\0';
    return pos;
}

// ── Help ──────────────────────────────────────────────────────────────────────

static void print_help(void)
{
    printf("\n  PURR OS kernel shell -- commands:\n\n");
    for (int i = 0; i < s_ncmds; i++) {
        printf("  %s\n", s_cmds[i].help);
    }
    printf("\n");
}

// ── Shell task ────────────────────────────────────────────────────────────────

static void _shell_task(void *arg)
{
    (void)arg;
    char line[256];
    char *argv[16];

    vTaskDelay(pdMS_TO_TICKS(500));
    _serial_init();

    printf("\n\n  PURR OS kernel shell  (type 'help' for commands)\n");
    printf("  -------------------------------------------------\n\n");
    fflush(stdout);

    while (1) {
        _serial_write("purr> ", 6);

        if (_read_line(line, sizeof(line)) == 0)
            continue;

        if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
            print_help();
            continue;
        }

        int argc = tokenize(line, argv, 16);
        if (argc == 0) continue;

        int found = 0;
        for (int i = 0; i < s_ncmds; i++) {
            if (strcmp(argv[0], s_cmds[i].name) == 0) {
                s_cmds[i].fn(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("Unknown command '%s'. Type 'help'.\n", argv[0]);
        }
    }
}

void purr_shell_start(void)
{
#if SHELL_USE_USB_JTAG
    ESP_LOGI(TAG, "starting shell on USB-Serial/JTAG");
#else
    ESP_LOGI(TAG, "starting shell on UART0");
#endif
    xTaskCreatePinnedToCore(_shell_task, "purr_shell", 4096, NULL, 1, NULL, 1);
}

// ── purr_shell_run — capture output into buffer ───────────────────────────────

static SemaphoreHandle_t s_run_mutex = NULL;
static char             *s_cap_buf   = NULL;
static size_t            s_cap_max   = 0;
static size_t            s_cap_len   = 0;

static int _cap_vprintf(const char *fmt, va_list args)
{
    if (!s_cap_buf) return 0;
    size_t remaining = s_cap_max - s_cap_len - 1;
    if (remaining == 0) return 0;
    int written = vsnprintf(s_cap_buf + s_cap_len, remaining + 1, fmt, args);
    if (written > 0) {
        s_cap_len += (written > (int)remaining) ? remaining : (size_t)written;
        s_cap_buf[s_cap_len] = '\0';
    }
    return written;
}

int purr_shell_run(const char *line, char *out_buf, size_t out_max)
{
    if (!s_run_mutex) s_run_mutex = xSemaphoreCreateMutex();
    xSemaphoreTake(s_run_mutex, portMAX_DELAY);

    s_cap_buf = out_buf;
    s_cap_max = out_max;
    s_cap_len = 0;
    if (out_buf && out_max > 0) out_buf[0] = '\0';

    // esp_log_set_vprintf covers both ESP_LOG* and printf on ESP-IDF
    esp_log_set_vprintf(_cap_vprintf);

    char tmp[256];
    strncpy(tmp, line, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *argv[16];
    int argc = tokenize(tmp, argv, 16);
    int found = 0;

    if (argc > 0) {
        if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
            print_help();
            found = 1;
        } else {
            for (int i = 0; i < s_ncmds; i++) {
                if (strcmp(argv[0], s_cmds[i].name) == 0) {
                    s_cmds[i].fn(argc, argv);
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            snprintf(out_buf + s_cap_len, out_max - s_cap_len - 1,
                     "Unknown: '%s'\n", argv[0]);
            s_cap_len = strlen(out_buf);
        }
    }

    esp_log_set_vprintf(vprintf);
    s_cap_buf = NULL;
    int ret = (int)s_cap_len;
    xSemaphoreGive(s_run_mutex);
    return ret;
}
