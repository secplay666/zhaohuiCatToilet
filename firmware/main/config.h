#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// A Wrapper around ESP nvs_flash stroage

typedef struct config_item_t {
    nvs_entry_info_t info;
    bool ready;
    struct config_item_t *next;
    union {
        int8_t i8;
        uint8_t u8;
        int16_t i16;
        uint16_t u16;
        int32_t i32;
        uint32_t u32;
        int64_t i64;
        uint64_t u64;
        struct { size_t len; char * p; } str;
        struct { size_t len; void * p; } blob;
    };
} config_item_t;

typedef struct config_t {
} config_t;

extern config_t config;

esp_err_t config_init();
esp_err_t config_garbage_collect();

esp_err_t set_config();
esp_err_t get_config();
esp_err_t get_config_with_default();

esp_err_t del_config();

extern const char * config_namespace;

#endif


