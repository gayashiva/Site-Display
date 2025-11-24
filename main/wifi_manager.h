/**
 * @file wifi_manager.h
 * @brief WiFi connection management for ESP-IDF
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize WiFi in station mode
 * @return ESP_OK on success
 */
esp_err_t wifi_init(void);

/**
 * @brief Connect to WiFi network
 * @return ESP_OK on successful connection, ESP_FAIL otherwise
 */
esp_err_t wifi_connect(void);

/**
 * @brief Disconnect from WiFi
 */
void wifi_disconnect(void);

/**
 * @brief Stop WiFi to save power
 */
void wifi_stop(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected
 */
bool wifi_is_connected(void);

/**
 * @brief Get WiFi signal strength (RSSI)
 * @return RSSI value in dBm
 */
int8_t wifi_get_rssi(void);

#endif // WIFI_MANAGER_H
