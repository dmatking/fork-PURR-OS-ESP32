// SPI.h — Minimal SPI class for Arduino compatibility

#pragma once

#include "Arduino.h"

// SPI modes
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

// SPI clock speeds
#define SPI_CLOCK_DIV2 2
#define SPI_CLOCK_DIV4 4
#define SPI_CLOCK_DIV8 8

extern SPIClass SPI;
