#pragma once
// Preferences_sim.h — stub for <Preferences.h> (NVS key-value store)
#include <stdint.h>
#include <stdbool.h>
#include <string>

class Preferences {
public:
    bool  begin(const char*, bool = false) { return true; }
    void  end() {}
    bool  getBool(const char*, bool def = false) const    { return def; }
    int   getInt(const char*, int def = 0) const          { return def; }
    float getFloat(const char*, float def = 0.f) const    { return def; }
    uint8_t getUChar(const char*, uint8_t def = 0) const  { return def; }
    std::string getString(const char*, const char* def = "") const { return def; }
    void  putBool(const char*, bool)    {}
    void  putInt(const char*, int)      {}
    void  putFloat(const char*, float)  {}
    void  putUChar(const char*, uint8_t){}
    void  putString(const char*, const char*) {}
    void  remove(const char*) {}
    void  clear() {}
};
