#include "wifi_manager.h"
#ifdef SHELL_HAS_BT
#  include "bt_manager.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern "C" {

// ── WiFi ─────────────────────────────────────────────────────────────────────

void cmd_wifi_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!wifi_manager_enabled()) {
        printf("WiFi: disabled\n");
        return;
    }
    if (wifi_manager_connected()) {
        char ssid[33] = {};
        wifi_manager_get_ssid(ssid, sizeof(ssid));
        printf("WiFi: connected  SSID=%-32s  RSSI=%ddBm\n",
               ssid, wifi_manager_rssi());
    } else {
        printf("WiFi: enabled, not connected\n");
    }
}

void cmd_wifi_scan(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!wifi_manager_enabled()) {
        printf("WiFi disabled\n");
        return;
    }
    wifi_manager_scan_start();
    printf("Scanning");
    fflush(stdout);
    for (int i = 0; i < 80; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (wifi_manager_scan_done()) break;
        if (i % 5 == 0) { printf("."); fflush(stdout); }
    }
    printf("\n");
    int n = wifi_manager_scan_count();
    if (n == 0) { printf("No networks found\n"); return; }
    printf("%-24s  %5s  %s\n", "SSID", "RSSI", "Auth");
    for (int i = 0; i < n; i++) {
        char ssid[33] = {};
        wifi_manager_scan_get_ssid(i, ssid, sizeof(ssid));
        printf("[%2d] %-24s  %5ddBm  %s\n",
               i, ssid,
               wifi_manager_scan_get_rssi(i),
               wifi_manager_scan_get_secured(i) ? "WPA" : "open");
    }
}

void cmd_wifi_connect(int argc, char **argv)
{
    if (argc < 2) { printf("Usage: wifi-connect <ssid> [password]\n"); return; }
    const char *pass = (argc >= 3) ? argv[2] : "";
    wifi_manager_connect(argv[1], pass);
    printf("Connecting to '%s'...\n", argv[1]);
    for (int i = 0; i < 100; i++) {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (wifi_manager_connected()) {
            char ssid[33] = {};
            wifi_manager_get_ssid(ssid, sizeof(ssid));
            printf("Connected: %s  RSSI=%ddBm\n", ssid, wifi_manager_rssi());
            return;
        }
    }
    printf("Timeout: not connected\n");
}

void cmd_wifi_disconnect(int argc, char **argv)
{
    (void)argc; (void)argv;
    wifi_manager_disconnect();
    printf("WiFi disconnected\n");
}

void cmd_wifi_forget(int argc, char **argv)
{
    if (argc < 2) { printf("Usage: wifi-forget <ssid>\n"); return; }
    wifi_manager_forget(argv[1]);
    printf("Forgot '%s'\n", argv[1]);
}

// ── Bluetooth ─────────────────────────────────────────────────────────────────

#ifdef SHELL_HAS_BT

void cmd_bt_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!bt_manager_enabled()) { printf("BT: disabled\n"); return; }
    printf("BT: enabled  paired=%d\n", bt_manager_paired_count());
}

void cmd_bt_scan(int argc, char **argv)
{
    uint32_t ms = (argc >= 2) ? (uint32_t)atoi(argv[1]) : 8000;
    if (ms < 1000)  ms = 1000;
    if (ms > 30000) ms = 30000;
    bt_manager_start_discovery(ms);
    printf("Scanning for %lums...\n", (unsigned long)ms);
    for (uint32_t i = 0; i < ms / 500 + 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!bt_manager_discovery_active()) break;
        printf("  [%d devices so far]\r", bt_manager_discovered_count());
        fflush(stdout);
    }
    printf("\n");
    int n = bt_manager_discovered_count();
    if (n == 0) { printf("No devices found\n"); return; }
    for (int i = 0; i < n; i++) {
        char name[33] = {}, addr[18] = {};
        bt_manager_get_discovered_name(i, name, sizeof(name));
        bt_manager_get_discovered_addr(i, addr, sizeof(addr));
        printf("[%2d] %-32s  %s\n", i, name[0] ? name : "(unknown)", addr);
    }
}

void cmd_bt_devices(int argc, char **argv)
{
    (void)argc; (void)argv;
    int n = bt_manager_paired_count();
    if (n == 0) { printf("No paired devices\n"); return; }
    for (int i = 0; i < n; i++) {
        char name[33] = {}, addr[18] = {};
        bt_manager_get_paired_name(i, name, sizeof(name));
        bt_manager_get_paired_addr(i, addr, sizeof(addr));
        printf("[%2d] %-32s  %s  %s\n",
               i, name, addr,
               bt_manager_device_connected(i) ? "connected" : "paired");
    }
}

void cmd_bt_pair(int argc, char **argv)
{
    if (argc < 2) { printf("Usage: bt-pair <index>  (from bt-scan)\n"); return; }
    int idx = atoi(argv[1]);
    if (idx < 0 || idx >= bt_manager_discovered_count()) {
        printf("Index out of range\n"); return;
    }
    bt_manager_pair(idx);
    printf("Pairing initiated with device [%d]\n", idx);
}

void cmd_bt_unpair(int argc, char **argv)
{
    if (argc < 2) { printf("Usage: bt-unpair <index>  (from bt-devices)\n"); return; }
    int idx = atoi(argv[1]);
    if (idx < 0 || idx >= bt_manager_paired_count()) {
        printf("Index out of range\n"); return;
    }
    bt_manager_unpair(idx);
    printf("Unpaired device [%d]\n", idx);
}

#endif // SHELL_HAS_BT

} // extern "C"
