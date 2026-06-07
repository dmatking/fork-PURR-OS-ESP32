// wifi_manager.cpp — WiFi management (pure ESP-IDF)

#include "wifi_manager.h"
#include "../purr_idf_compat.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char* TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events = NULL;
static bool   wifi_on       = false;
static bool   wifi_yielded  = false;
static bool   scan_complete = false;
static int    scan_results  = 0;
static wifi_ap_record_t* s_ap_records = NULL;

static void wifi_event_handler(void* arg, esp_event_base_t base,
                                int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_events) xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (s_wifi_events) xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        esp_wifi_scan_get_ap_num((uint16_t*)&scan_results);
        if (scan_results > 0) {
            free(s_ap_records);
            s_ap_records = (wifi_ap_record_t*)malloc(scan_results * sizeof(wifi_ap_record_t));
            if (s_ap_records) {
                uint16_t n = (uint16_t)scan_results;
                esp_wifi_scan_get_ap_records(&n, s_ap_records);
                scan_results = n;
            }
        }
        scan_complete = true;
    }
}

void wifi_manager_init() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        wifi_event_handler, NULL, NULL);

    s_wifi_events = xEventGroupCreate();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    wifi_on = true;

    // Reconnect to last saved network
    char ssid[64] = "", pass[64] = "";
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t sl = sizeof(ssid), pl = sizeof(pass);
        nvs_get_str(h, "ssid", ssid, &sl);
        nvs_get_str(h, "pass", pass, &pl);
        nvs_close(h);
    }
    if (strlen(ssid) > 0) {
        wifi_config_t wcfg = {};
        strlcpy((char*)wcfg.sta.ssid,     ssid, sizeof(wcfg.sta.ssid));
        strlcpy((char*)wcfg.sta.password, pass, sizeof(wcfg.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &wcfg);
        esp_wifi_connect();
        ESP_LOGI(TAG, "reconnecting to %s", ssid);
    }
    ESP_LOGI(TAG, "manager init OK");
}

void wifi_manager_update() {}  // events handled asynchronously

void wifi_manager_deinit() {
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_on = false;
}

bool wifi_manager_enabled()  { return wifi_on && !wifi_yielded; }
void wifi_manager_enable()   { esp_wifi_start(); wifi_on = true; }
void wifi_manager_disable()  { esp_wifi_stop();  wifi_on = false; }

void wifi_manager_scan_start() {
    if (!wifi_on || wifi_yielded) return;
    scan_complete = false;
    scan_results  = 0;
    wifi_scan_config_t scan_cfg = {};
    esp_wifi_scan_start(&scan_cfg, false);
}

bool wifi_manager_scan_done()  { return scan_complete; }
int  wifi_manager_scan_count() { return scan_results; }

void wifi_manager_scan_get_ssid(int i, char* buf, size_t len) {
    if (!s_ap_records || i < 0 || i >= scan_results) { buf[0] = '\0'; return; }
    strlcpy(buf, (char*)s_ap_records[i].ssid, len);
}
int wifi_manager_scan_get_rssi(int i) {
    if (!s_ap_records || i < 0 || i >= scan_results) return 0;
    return s_ap_records[i].rssi;
}
bool wifi_manager_scan_get_secured(int i) {
    if (!s_ap_records || i < 0 || i >= scan_results) return false;
    return s_ap_records[i].authmode != WIFI_AUTH_OPEN;
}

void wifi_manager_connect(const char* ssid, const char* password) {
    if (!wifi_on || wifi_yielded) return;
    wifi_config_t wcfg = {};
    strlcpy((char*)wcfg.sta.ssid,     ssid,     sizeof(wcfg.sta.ssid));
    strlcpy((char*)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_connect();

    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", password);
        nvs_commit(h); nvs_close(h);
    }
    ESP_LOGI(TAG, "connecting to %s", ssid);
}

void wifi_manager_disconnect() { esp_wifi_disconnect(); }

bool wifi_manager_connected() {
    if (!wifi_on || wifi_yielded || !s_wifi_events) return false;
    return (xEventGroupGetBits(s_wifi_events) & WIFI_CONNECTED_BIT) != 0;
}

void wifi_manager_get_ssid(char* buf, size_t len) {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK)
        strlcpy(buf, (char*)info.ssid, len);
    else if (len) buf[0] = '\0';
}

int wifi_manager_rssi() {
    wifi_ap_record_t info;
    return (esp_wifi_sta_get_ap_info(&info) == ESP_OK) ? info.rssi : 0;
}

void wifi_manager_forget(const char* ssid) {
    char saved[64] = "";
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &h) == ESP_OK) {
        size_t sl = sizeof(saved);
        nvs_get_str(h, "ssid", saved, &sl);
        if (strcmp(saved, ssid) == 0) {
            nvs_erase_key(h, "ssid");
            nvs_erase_key(h, "pass");
            nvs_commit(h);
        }
        nvs_close(h);
    }
    esp_wifi_disconnect();
}

void wifi_manager_yield() {
    wifi_yielded = true;
    esp_wifi_disconnect();
    esp_wifi_stop();
    ESP_LOGI(TAG, "yielded to firmware");
}

void wifi_manager_reclaim() {
    wifi_yielded = false;
    esp_wifi_start();
    ESP_LOGI(TAG, "reclaimed from firmware");

    char ssid[64] = "", pass[64] = "";
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t sl = sizeof(ssid), pl = sizeof(pass);
        nvs_get_str(h, "ssid", ssid, &sl);
        nvs_get_str(h, "pass", pass, &pl);
        nvs_close(h);
    }
    if (strlen(ssid) > 0) {
        wifi_config_t wcfg = {};
        strlcpy((char*)wcfg.sta.ssid,     ssid, sizeof(wcfg.sta.ssid));
        strlcpy((char*)wcfg.sta.password, pass, sizeof(wcfg.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &wcfg);
        esp_wifi_connect();
    }
}

bool wifi_manager_yielded() { return wifi_yielded; }
