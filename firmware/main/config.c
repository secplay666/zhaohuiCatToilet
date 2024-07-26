#include <string.h>
#include "esp_err.h"
#include "esp_check.h"
#include "nvs.h"

#include "config.h"

const char * config_namespace_name = "config";
config_t config = {};

static nvs_handle_t config_handle = 0;
static bool config_initialized = false;
static const char * TAG = "config";

//delete first item in list
esp_err_t pop_item_list(config_item_t ** head_p){
    config_item_t *p = *head_p;
    if (NVS_TYPE_STR  == p->info.type) free(p->str.p);
    if (NVS_TYPE_BLOB == p->info.type) free(p->blob.p);
    *head_p = p->next;
    free(p);
    return ESP_OK;
}

//delete all item list
esp_err_t delete_item_list(config_item_t ** head_p, config_item_t ** tail_p){
    while (*head_p){
        pop_item_list(head_p);
    }
    *tail_p = *head_p;
    return ESP_OK;
}

//fill in actual data for an item
esp_err_t get_item_data(nvs_handle_t handle, config_item_t * item){
    esp_err_t ret = ESP_OK;

    switch (item->info.type){
        case NVS_TYPE_U8:
            ESP_GOTO_ON_ERROR(nvs_get_u8(handle, item->info.key, &(item->u8)), fail, TAG, "nvs_get_u8 failed");
            break;
        case NVS_TYPE_I8:
            ESP_GOTO_ON_ERROR(nvs_get_i8(handle, item->info.key, &(item->i8)), fail, TAG, "nvs_get_i8 failed");
            break;
        case NVS_TYPE_U16:
            ESP_GOTO_ON_ERROR(nvs_get_u16(handle, item->info.key, &(item->u16)), fail, TAG, "nvs_get_u16 failed");
            break;
        case NVS_TYPE_I16:
            ESP_GOTO_ON_ERROR(nvs_get_i16(handle, item->info.key, &(item->i16)), fail, TAG, "nvs_get_i16 failed");
            break;
        case NVS_TYPE_U32:
            ESP_GOTO_ON_ERROR(nvs_get_u32(handle, item->info.key, &(item->u32)), fail, TAG, "nvs_get_u32 failed");
            break;
        case NVS_TYPE_I32:
            ESP_GOTO_ON_ERROR(nvs_get_i32(handle, item->info.key, &(item->i32)), fail, TAG, "nvs_get_i32 failed");
            break;
        case NVS_TYPE_U64:
            ESP_GOTO_ON_ERROR(nvs_get_u64(handle, item->info.key, &(item->u64)), fail, TAG, "nvs_get_u64 failed");
            break;
        case NVS_TYPE_I64:
            ESP_GOTO_ON_ERROR(nvs_get_i64(handle, item->info.key, &(item->i64)), fail, TAG, "nvs_get_i64 failed");
            break;
        case NVS_TYPE_STR:
            ESP_GOTO_ON_ERROR(nvs_get_str(handle, item->info.key, NULL, &(item->str.len)), fail, TAG, "nvs_get_str length failed");
            item->str.p = calloc(1, item->str.len);
            if (NULL == item->str.p) {
                ret = ESP_ERR_NO_MEM;
                goto fail;
            }
            ESP_GOTO_ON_ERROR(nvs_get_str(handle, item->info.key, item->str.p, &(item->str.len)), fail_cleaning, TAG, "nvs_get_str failed");
            break;
        case NVS_TYPE_BLOB:
            ESP_GOTO_ON_ERROR(nvs_get_blob(handle, item->info.key, NULL, &(item->blob.len)), fail, TAG, "nvs_get_blob length failed");
            item->blob.p = calloc(1, item->blob.len);
            if (NULL == item->blob.p) {
                ret = ESP_ERR_NO_MEM;
                goto fail;
            }
            ESP_GOTO_ON_ERROR(nvs_get_blob(handle, item->info.key, item->blob.p, &(item->blob.len)), fail_cleaning, TAG, "nvs_get_blob failed");
            break;
        default:
            //should not happen
            ret = ESP_ERR_INVALID_STATE;
            goto fail;
            break;
    }
    item->ready = true;
    return ESP_OK;
fail_cleaning:
    if (NVS_TYPE_STR  == item->info.type) free(item->str.p);
    if (NVS_TYPE_BLOB == item->info.type) free(item->blob.p);
fail:
    return ret;
}

// write an item to nvs storage
esp_err_t write_item_to_nvs(nvs_handle_t handle, const config_item_t * item){
    switch (item->info.type){
        case NVS_TYPE_U8:
            ESP_LOGI(TAG, "writing to key %s value %"PRIu8, item->info.key, item->u8);
            ESP_RETURN_ON_ERROR(nvs_set_u8(handle, item->info.key, item->u8), TAG, "nvs_set_u8 failed");
            break;
        case NVS_TYPE_I8:
            ESP_RETURN_ON_ERROR(nvs_set_i8(handle, item->info.key, item->i8), TAG, "nvs_set_i8 failed");
            break;
        case NVS_TYPE_U16:
            ESP_RETURN_ON_ERROR(nvs_set_u16(handle, item->info.key, item->u16), TAG, "nvs_set_u16 failed");
            break;
        case NVS_TYPE_I16:
            ESP_RETURN_ON_ERROR(nvs_set_i16(handle, item->info.key, item->i16), TAG, "nvs_set_i16 failed");
            break;
        case NVS_TYPE_U32:
            ESP_RETURN_ON_ERROR(nvs_set_u32(handle, item->info.key, item->u32), TAG, "nvs_set_u32 failed");
            break;
        case NVS_TYPE_I32:
            ESP_RETURN_ON_ERROR(nvs_set_i32(handle, item->info.key, item->i32), TAG, "nvs_set_i32 failed");
            break;
        case NVS_TYPE_U64:
            ESP_RETURN_ON_ERROR(nvs_set_u64(handle, item->info.key, item->u64), TAG, "nvs_set_u64 failed");
            break;
        case NVS_TYPE_I64:
            ESP_RETURN_ON_ERROR(nvs_set_i64(handle, item->info.key, item->i64), TAG, "nvs_set_i64 failed");
            break;
        case NVS_TYPE_STR:
            ESP_RETURN_ON_ERROR(nvs_set_str(handle, item->info.key, item->str.p), TAG, "nvs_set_str failed");
            break;
        case NVS_TYPE_BLOB:
            ESP_RETURN_ON_ERROR(nvs_set_blob(handle, item->info.key, item->blob.p, item->blob.len), TAG, "nvs_set_blob failed");
            break;
        default:
            //should not happen
            return ESP_ERR_INVALID_STATE;
            break;
    }
    
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "nvs_commit failed");
    return ESP_OK;
}

//fill in actual data for a list
esp_err_t fill_item_data(config_item_t ** head_p){
    esp_err_t ret = ESP_OK;
    bool done = false;
    config_item_t *ptr, *sample;
    nvs_handle_t handle = 0;

    while (!done){
        done = true;
        sample = NULL;
        //find an unfilled item and get the handle for all entries in its same namespace
        for (ptr = *head_p; ptr != NULL; ptr = ptr->next) {
            if (ptr->ready) continue;

            if (NULL == sample){
                //open new handle
                sample = ptr;
                ESP_LOGI(TAG, "opening namespace: %s", sample->info.namespace_name);
                ESP_GOTO_ON_ERROR(
                        nvs_open(sample->info.namespace_name, NVS_READONLY, &handle),
                        end, TAG, "nfs_open failed");
            }
            else if (0 != strcmp(sample->info.namespace_name, ptr->info.namespace_name)){
                //we have at least one unfilled item
                done = false;
                //item is in a different namespace, skip it for later
                continue;
            }
            
            //get data
            ESP_GOTO_ON_ERROR(get_item_data(handle, ptr),
                    end_cleaning, TAG, "get_item_data failed");
        }
        //close handle
        if (NULL != sample){
            nvs_close(handle);
        }
    }
    return ESP_OK;

end_cleaning:
    nvs_close(handle);
end:
    return ret;
}

// write all items in list to nvs storage
esp_err_t write_list_to_nvs(config_item_t * list_head){
    esp_err_t ret = ESP_OK;
    config_item_t *ptr = list_head, *sample = NULL;
    nvs_handle_t handle = 0;
    bool done = false;
    //set ready=false for written items
    while (!done) {
        done = true;
        for (ptr = list_head; ptr != NULL; ptr = ptr->next){
            if (!ptr->ready) continue;

            if (NULL == sample) {
                //open new handle
                sample = ptr;
                ESP_LOGI(TAG, "opening namespace: %s", sample->info.namespace_name);
                ESP_GOTO_ON_ERROR(
                        nvs_open(sample->info.namespace_name, NVS_READWRITE, &handle),
                        end, TAG, "nfs_open failed");
            }
            else if (0 != strcmp(sample->info.namespace_name, ptr->info.namespace_name)) {
                //we have at least one unfilled item
                done = false;
                //item is in a different namespace, skip it for later
                continue;
            }

            //write data
            ESP_GOTO_ON_ERROR(write_item_to_nvs(handle, ptr),
                    end_cleaning, TAG, "write_item_to_nvs failed");
            ptr->ready = false;
        }
        //close handle
        if (NULL != sample){
            nvs_close(handle);
        }
    }
    //set ready=true for all items
    for (ptr = list_head; ptr != NULL; ptr = ptr->next){
        ptr->ready = true;
    }
    return ESP_OK;
end_cleaning:
    nvs_close(handle);
end:
    return ret;
}

esp_err_t append_item_list(const nvs_iterator_t iter, config_item_t ** head_p, config_item_t ** tail_p){
    esp_err_t ret = ESP_OK;
    if ((NULL == *tail_p && NULL != *head_p) || (NULL != *tail_p && NULL == *head_p)){
        ESP_LOGE(TAG, "Invalid list arguments");
        return ESP_ERR_INVALID_ARG;
    }
    config_item_t * list_ptr = NULL;
    list_ptr = (config_item_t *)calloc(1, sizeof(config_item_t));
    if (NULL == list_ptr) {
        ESP_LOGE(TAG, "Insufficient memory");
        return ESP_ERR_NO_MEM;
    }
    //fill in item info
    list_ptr->next = NULL;
    list_ptr->ready = false;
    ESP_GOTO_ON_ERROR(nvs_entry_info(iter, &(list_ptr->info)),
            fail, TAG, "nvs_entry_info failed");

    if (NULL != *tail_p) {
        (*tail_p)->next = list_ptr;
        *tail_p = list_ptr;
    } else {
        *head_p = *tail_p = list_ptr;
    }
    return ESP_OK;
fail:
    free(list_ptr);
    return ret;
}

esp_err_t config_garbage_collect(){
    esp_err_t ret = ESP_OK;

    nvs_iterator_t iter = NULL;
    config_item_t *list_head=NULL, *list_tail=NULL;

    //backup data
    ESP_LOGI(TAG, "Backup nvs data..");
    //find first entry
    ret = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &iter);
    //loop through entries and get item info
    while (ESP_OK == ret){
        //record data
        ESP_GOTO_ON_ERROR(append_item_list(iter, &list_head, &list_tail),
                fail, TAG, "Append to item list failed");
        ret = nvs_entry_next(&iter);
    }
    //loop should end with ESP_ERR_NVS_NOT_FOUND 
    if (ESP_ERR_NVS_NOT_FOUND != ret) {
        ESP_LOGE(TAG, "Error finding nvs entries");
        //don't release iterator if ESP_ERR_INVALID_ARG is returned
        if (ESP_ERR_INVALID_ARG != ret) {
            nvs_release_iterator(iter);
        }
        goto fail;
    }

    //populate item data
    ESP_GOTO_ON_ERROR(fill_item_data(&list_head),
            fail, TAG, "get_item_data failed");

    //erase partition, will deinitialize nvs_flash partition
    ESP_LOGI(TAG, "Erase nvs partition..");
    ESP_GOTO_ON_ERROR(nvs_flash_erase(), 
            fail, TAG, "nvs flash erase failed");

    //reinitializing 
    ESP_LOGI(TAG, "Reinitialize nvs flash..");
    ESP_GOTO_ON_ERROR(nvs_flash_init(), 
            fail, TAG, "nvs flash init failed");

    //restore data
    ESP_LOGI(TAG, "Restore nvs data..");

    //write to nvs
    ESP_GOTO_ON_ERROR(write_list_to_nvs(list_head),
            fail, TAG, "Append to item list failed");

    //finish
    ESP_LOGI(TAG, "Nvs data refreshed successfully");

fail:
    //free used memory
    delete_item_list(&list_head, &list_tail);
    return ret;
}

esp_err_t config_init(){
    esp_err_t ret = ESP_OK;

    //initialze nvs_flash
    ret = nvs_flash_init();
    if (ESP_OK == ret) {
        // do nothing
    } else if (ESP_ERR_NVS_NO_FREE_PAGES == ret) {
        ESP_LOGW(TAG, "nvs flash has no free space, erasing nvs partition for space");
        ESP_RETURN_ON_ERROR(config_garbage_collect(), TAG, "config garbage collecting failed");
    } else {
        ESP_LOGE(TAG, "nvs flash initialization failed");
        return ret;
    }
    
    //open config namespace
    ESP_GOTO_ON_ERROR(
        nvs_open(config_namespace_name, NVS_READWRITE, &config_handle),
        fail, TAG, "nfs_open failed");

    //finish initialization
    config_initialized = true;
    return ESP_OK;

fail:
    nvs_flash_deinit();
    return ret;
}
