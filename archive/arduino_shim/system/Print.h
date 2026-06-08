// Print.h — Minimal Print class for Arduino compatibility

#pragma once

#include <cstdint>
#include <cstdio>

class Print {
public:
    virtual ~Print() = default;

    virtual size_t write(uint8_t c) {
        putchar(c);
        return 1;
    }

    size_t write(const char* str) {
        if (!str) return 0;
        size_t n = 0;
        while (*str) {
            n += write((uint8_t)*str++);
        }
        return n;
    }

    size_t write(const uint8_t* buffer, size_t size) {
        for (size_t i = 0; i < size; i++) {
            write(buffer[i]);
        }
        return size;
    }

    size_t print(const char* str) { return write(str); }
    size_t println(const char* str) {
        size_t n = write(str);
        n += write((uint8_t)'\n');
        return n;
    }

    size_t print(int val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", val);
        return write(buf);
    }
};
