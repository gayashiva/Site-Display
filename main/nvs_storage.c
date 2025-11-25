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
static const char* KEY_FIRST_BOOT = "first_boot";

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

        // Check if first_boot flag exists, if not create it
        nvs_handle_t handle;
        ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (ret == ESP_OK) {
            int32_t value;
            ret = nvs_get_i32(handle, KEY_FIRST_BOOT, &value);
            if (ret == ESP_ERR_NVS_NOT_FOUND) {
                // First boot flag doesn't exist - this is first boot, create it
                nvs_set_i32(handle, KEY_FIRST_BOOT, 1);
                nvs_commit(handle);
                ESP_LOGI(TAG, "Set first boot flag");
            }
            nvs_close(handle);
        }
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

bool nvs_is_first_boot(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        // If namespace doesn't exist, it's first boot
        ESP_LOGI(TAG, "First boot: namespace doesn't exist");
        return true;
    }

    int32_t value = 1;  // Default to first boot
    ret = nvs_get_i32(handle, KEY_FIRST_BOOT, &value);
    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // Key doesn't exist, first boot
        ESP_LOGI(TAG, "First boot: key not found");
        return true;
    }

    if (ret != ESP_OK) {
        // Error reading, assume first boot to be safe
        ESP_LOGI(TAG, "First boot: read error");
        return true;
    }

    // Value of 0 means already booted before
    bool is_first = (value != 0);
    ESP_LOGI(TAG, "First boot check: %s (value=%d)", is_first ? "YES" : "NO", (int)value);
    return is_first;
}

esp_err_t nvs_clear_first_boot(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_i32(handle, KEY_FIRST_BOOT, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write first_boot flag: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "First boot flag cleared");
    }

    nvs_close(handle);
    return ret;
}
