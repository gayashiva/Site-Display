/**
 * @file nvs_storage.c
 * @brief NVS storage implementation
 */

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include "nvs_storage.h"

static const char* TAG = "nvs_storage";
static const char* NVS_NAMESPACE = "site";
static const char* KEY_SITE_INDEX = "index";

esp_err_t nvs_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized");
    } else {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t nvs_save_site_index(int index)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_i32(handle, KEY_SITE_INDEX, index);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write index: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Saved site index: %d", index);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_site_index(int* index)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved site index, using default");
        } else {
            ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    int32_t value;
    ret = nvs_get_i32(handle, KEY_SITE_INDEX, &value);
    if (ret == ESP_OK) {
        *index = (int)value;
        ESP_LOGI(TAG, "Loaded site index: %d", *index);
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved site index");
    } else {
        ESP_LOGE(TAG, "Failed to read index: %s", esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}
