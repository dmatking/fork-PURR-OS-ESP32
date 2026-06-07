#pragma once
// task_sim.h — PC stub for <freertos/task.h>
#include "FreeRTOS_sim.h"
#include "Arduino_sim.h"   // for SIM_SLEEP_MS

static inline void vTaskDelay(TickType_t ticks) { SIM_SLEEP_MS(ticks); }

typedef void (*TaskFunction_t)(void*);
typedef void* TaskHandle_t;

// In the sim, task creation just calls the function directly on a detached thread.
#ifdef _WIN32
#  include <windows.h>
static inline void xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, int,
                                            void* arg, int, TaskHandle_t*, int) {
    HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
    if (h) CloseHandle(h);
}
static inline void xTaskCreate(TaskFunction_t fn, const char*, int,
                                void* arg, int, TaskHandle_t*) {
    HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
    if (h) CloseHandle(h);
}
static inline void vTaskDelete(TaskHandle_t) {}
#else
#  include <pthread.h>
static inline void xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, int,
                                            void* arg, int, TaskHandle_t*, int) {
    pthread_t t; pthread_create(&t, NULL, (void*(*)(void*))fn, arg); pthread_detach(t);
}
static inline void xTaskCreate(TaskFunction_t fn, const char*, int,
                                void* arg, int, TaskHandle_t*) {
    pthread_t t; pthread_create(&t, NULL, (void*(*)(void*))fn, arg); pthread_detach(t);
}
static inline void vTaskDelete(TaskHandle_t) {}
#endif
