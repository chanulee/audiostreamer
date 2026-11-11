/**
 * Helpers for creating FreeRTOS tasks.
 *
 * On ESP32 with SPIRAM, task stacks must remain in internal RAM because
 * SPI flash operations disable the cache, making SPIRAM inaccessible.
 * Placing stacks there causes esp_task_stack_is_sane_cache_disabled()
 * asserts.  These wrappers keep the call-site API stable regardless.
 *
 * Usage:
 *   Permanent tasks (never deleted):
 *     task_create_spiram(fn, "name", depth, param, prio, &handle, NULL);
 *
 *   Transient tasks (deleted later):
 *     spiram_task_mem_t mem;
 *     task_create_spiram(fn, "name", depth, param, prio, &handle, &mem);
 *     // ... later, after the task has exited:
 *     task_free_spiram(&mem);
 */
#pragma once

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

typedef struct {
  void *stack;
  void *tcb;
} spiram_task_mem_t;

static inline BaseType_t task_create_spiram(TaskFunction_t fn, const char *name,
                                            uint32_t depth, void *param,
                                            UBaseType_t prio,
                                            TaskHandle_t *handle,
                                            spiram_task_mem_t *mem) {
  if (mem) {
    mem->stack = NULL;
    mem->tcb = NULL;
  }
  return xTaskCreate(fn, name, depth, param, prio, handle);
}

static inline BaseType_t
task_create_pinned_spiram(TaskFunction_t fn, const char *name, uint32_t depth,
                          void *param, UBaseType_t prio, TaskHandle_t *handle,
                          BaseType_t core, spiram_task_mem_t *mem) {
  if (mem) {
    mem->stack = NULL;
    mem->tcb = NULL;
  }
  return xTaskCreatePinnedToCore(fn, name, depth, param, prio, handle, core);
}

static inline void task_free_spiram(spiram_task_mem_t *mem) {
  (void)mem;
}

/*
 * AirPlay session tasks (receiver, control, timing, event port) never touch
 * flash, so their stacks can live in PSRAM. With Wi-Fi + Bluetooth up only
 * ~20 KB of byte-addressable internal RAM is left, too little for them.
 * A task created this way must exit with task_delete_self_psram(), never
 * vTaskDelete(NULL), or its stack leaks.
 */
static inline BaseType_t task_create_psram(TaskFunction_t fn, const char *name,
                                           uint32_t depth, void *param,
                                           UBaseType_t prio,
                                           TaskHandle_t *handle,
                                           BaseType_t core) {
  return xTaskCreatePinnedToCoreWithCaps(fn, name, depth, param, prio, handle,
                                         core,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static inline void task_delete_self_psram(void) {
  vTaskDeleteWithCaps(NULL);
}
