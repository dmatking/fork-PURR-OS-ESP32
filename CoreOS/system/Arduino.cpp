// Arduino.cpp — Arduino API implementation

#include "Arduino.h"
#include <cstdarg>
#include <cstdio>

SerialClass Serial;
SPIClass SPI;

void SerialClass::printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
