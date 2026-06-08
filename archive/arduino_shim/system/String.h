// String.h — Minimal Arduino String class

#pragma once

#include <string.h>
#include <stdio.h>
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

    void toCharArray(char* buf, size_t bufsize) const {
        if (!buf || bufsize == 0) return;
        size_t copy_len = (len < bufsize - 1) ? len : bufsize - 1;
        if (buffer) memcpy(buf, buffer, copy_len);
        buf[copy_len] = '\0';
    }

    int indexOf(char c) const {
        if (!buffer) return -1;
        const char* p = strchr(buffer, c);
        return p ? (int)(p - buffer) : -1;
    }

    String substring(size_t start, size_t end = (size_t)-1) const {
        if (!buffer || start >= len) return String();
        size_t e = (end == (size_t)-1 || end > len) ? len : end;
        if (start >= e) return String();
        size_t slen = e - start;
        char* tmp = new char[slen + 1];
        memcpy(tmp, buffer + start, slen);
        tmp[slen] = '\0';
        String res(tmp);
        delete[] tmp;
        return res;
    }

    String& operator+=(const char* str) {
        if (!str) return *this;
        size_t slen = strlen(str);
        resize(len + slen + 1);
        strcat(buffer, str);
        len += slen;
        return *this;
    }

    String& operator+=(const String& other) {
        return *this += other.c_str();
    }

    String& operator+=(char c) {
        char tmp[2] = {c, 0};
        return *this += tmp;
    }
};
