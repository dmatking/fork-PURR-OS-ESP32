// String.h — Minimal Arduino String class

#pragma once

#include <cstring>
#include <cstdio>

class String {
private:
    char* buffer = nullptr;
    size_t capacity = 0;
    size_t len = 0;

    void resize(size_t new_cap) {
        if (new_cap <= capacity) return;
        char* new_buf = new char[new_cap];
        if (buffer) {
            strcpy(new_buf, buffer);
            delete[] buffer;
        }
        buffer = new_buf;
        capacity = new_cap;
    }

public:
    String() = default;

    String(const char* str) {
        if (!str) return;
        len = strlen(str);
        capacity = len + 1;
        buffer = new char[capacity];
        strcpy(buffer, str);
    }

    String(const String& other) {
        if (other.buffer) {
            len = other.len;
            capacity = other.capacity;
            buffer = new char[capacity];
            strcpy(buffer, other.buffer);
        }
    }

    ~String() {
        if (buffer) delete[] buffer;
    }

    String& operator=(const String& other) {
        if (this != &other) {
            if (buffer) delete[] buffer;
            if (other.buffer) {
                len = other.len;
                capacity = other.capacity;
                buffer = new char[capacity];
                strcpy(buffer, other.buffer);
            } else {
                buffer = nullptr;
                len = 0;
                capacity = 0;
            }
        }
        return *this;
    }

    String& operator=(const char* str) {
        if (buffer) delete[] buffer;
        if (str) {
            len = strlen(str);
            capacity = len + 1;
            buffer = new char[capacity];
            strcpy(buffer, str);
        } else {
            buffer = nullptr;
            len = 0;
            capacity = 0;
        }
        return *this;
    }

    const char* c_str() const { return buffer ? buffer : ""; }
    size_t length() const { return len; }
    bool isEmpty() const { return len == 0; }

    char operator[](size_t i) const { return buffer ? buffer[i] : 0; }

    String operator+(const String& other) const {
        String result;
        size_t total = len + other.len;
        result.resize(total + 1);
        if (buffer) strcpy(result.buffer, buffer);
        if (other.buffer) strcat(result.buffer, other.buffer);
        result.len = total;
        return result;
    }

    String operator+(const char* str) const {
        return *this + String(str);
    }

    bool operator==(const String& other) const {
        return strcmp(c_str(), other.c_str()) == 0;
    }

    bool operator==(const char* str) const {
        return strcmp(c_str(), str ? str : "") == 0;
    }
};
