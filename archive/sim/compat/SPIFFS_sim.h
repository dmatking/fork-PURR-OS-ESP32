#pragma once
// SPIFFS_sim.h — stub for <SPIFFS.h>
#include <stdint.h>
#include <stdbool.h>

struct _SimFile {
    operator bool() const { return false; }
    void close() {}
    void printf(const char*, ...) {}
};

struct _SimSPIFFS {
    bool begin(bool = false) { return true; }
    void end() {}
    bool exists(const char*) { return false; }
    _SimFile open(const char*, const char* = "r") { return {}; }
};
static _SimSPIFFS SPIFFS;
