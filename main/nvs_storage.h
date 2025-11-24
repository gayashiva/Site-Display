/**
 * @file nvs_storage.h
 * @brief NVS storage for site selection persistence
 */

#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "esp_err.h"

/**
 * @brief Initialize NVS flash storage
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief Save current site index to NVS
 * @param index Site index to save
 * @return ESP_OK on success
 */
esp_err_t nvs_save_site_index(int index);

/**
 * @brief Load site index from NVS
 * @param index Pointer to store loaded index
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if not set
 */
esp_err_t nvs_load_site_index(int* index);

#endif // NVS_STORAGE_H
