#include "wifi_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <Preferences.h>

static bool wifi_on       = false;
static bool wifi_yielded  = false;
static bool scan_pending  = false;
static bool scan_complete = false;
static int  scan_results  = 0;

static Preferences prefs;

void wifi_manager_init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    wifi_on = true;

    // Attempt reconnect to last saved network
    prefs.begin("wifi_cfg", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.length() > 0) {
        Serial.printf("[wifi] reconnecting to %s\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
    }

    Serial.println("[wifi] manager init OK");
}

void wifi_manager_update() {
    if (scan_pending && !scan_complete) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            scan_results  = n;
            scan_complete = true;
            scan_pending  = false;
        }
    }
}

void wifi_manager_deinit() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_on = false;
}

bool wifi_manager_enabled()  { return wifi_on && !wifi_yielded; }

void wifi_manager_enable() {
    WiFi.mode(WIFI_STA);
    wifi_on = true;
}

void wifi_manager_disable() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_on = false;
}

void wifi_manager_scan_start() {
    if (!wifi_on || wifi_yielded) return;
    WiFi.scanNetworks(true);
    scan_pending  = true;
    scan_complete = false;
    scan_results  = 0;
}

bool wifi_manager_scan_done()  { return scan_complete; }
int  wifi_manager_scan_count() { return scan_results; }

void wifi_manager_scan_get_ssid(int index, char* buf, size_t len) {
    if (index < 0 || index >= scan_results) { buf[0] = '\0'; return; }
    strncpy(buf, WiFi.SSID(index).c_str(), len - 1);
    buf[len - 1] = '\0';
}

int  wifi_manager_scan_get_rssi(int index) {
    return (index >= 0 && index < scan_results) ? WiFi.RSSI(index) : 0;
}

bool wifi_manager_scan_get_secured(int index) {
    return (index >= 0 && index < scan_results) && (WiFi.encryptionType(index) != WIFI_AUTH_OPEN);
}

void wifi_manager_connect(const char* ssid, const char* password) {
    if (!wifi_on || wifi_yielded) return;
    WiFi.begin(ssid, password);

    prefs.begin("wifi_cfg", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();

    Serial.printf("[wifi] connecting to %s\n", ssid);
}

void wifi_manager_disconnect() {
    WiFi.disconnect();
}

bool wifi_manager_connected() {
    return wifi_on && !wifi_yielded && (WiFi.status() == WL_CONNECTED);
}

void wifi_manager_get_ssid(char* buf, size_t len) {
    strncpy(buf, WiFi.SSID().c_str(), len - 1);
    buf[len - 1] = '\0';
}

int wifi_manager_rssi() {
    return WiFi.RSSI();
}

void wifi_manager_forget(const char* ssid) {
    prefs.begin("wifi_cfg", false);
    String saved = prefs.getString("ssid", "");
    if (saved == ssid) {
        prefs.remove("ssid");
        prefs.remove("pass");
    }
    prefs.end();
    WiFi.disconnect();
}

void wifi_manager_yield() {
    wifi_yielded = true;
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("[wifi] yielded to firmware");
}

void wifi_manager_reclaim() {
    wifi_yielded = false;
    WiFi.mode(WIFI_STA);
    Serial.println("[wifi] reclaimed from firmware");

    // Restore last saved connection
    prefs.begin("wifi_cfg", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();
    if (ssid.length() > 0) WiFi.begin(ssid.c_str(), pass.c_str());
}

bool wifi_manager_yielded() { return wifi_yielded; }
