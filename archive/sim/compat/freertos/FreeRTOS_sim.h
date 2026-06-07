#pragma once
// FreeRTOS_sim.h — PC stub for <freertos/FreeRTOS.h>
#include <stdint.h>
typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
#define portMAX_DELAY       0xFFFFFFFFUL
#define configTICK_RATE_HZ  1000
#define pdMS_TO_TICKS(ms)   ((TickType_t)(ms))
