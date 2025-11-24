/**
 * @file http_client.c
 * @brief HTTPS client implementation for Site Data API
 */

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "http_client.h"
#include "site_data.h"

static const char* TAG = "http_client";

#define MAX_HTTP_OUTPUT_BUFFER 49152  // 48KB for 288 readings

// Response buffer
static char* s_response_buffer = NULL;
static int s_response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t* evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER: %s: %s",
                     evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (s_response_buffer != NULL) {
                // Check buffer overflow
                if (s_response_len + evt->data_len < MAX_HTTP_OUTPUT_BUFFER) {
                    memcpy(s_response_buffer + s_response_len, evt->data, evt->data_len);
                    s_response_len += evt->data_len;
                    s_response_buffer[s_response_len] = '\0';
                } else {
                    ESP_LOGW(TAG, "Response buffer overflow");
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;

        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

bool fetch_site_data(const char* site_name, int count, bool print)
{
    bool success = false;

    // Allocate response buffer
    s_response_buffer = malloc(MAX_HTTP_OUTPUT_BUFFER);
    if (s_response_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return false;
    }
    s_response_len = 0;
    memset(s_response_buffer, 0, MAX_HTTP_OUTPUT_BUFFER);

    // Build URL
    char url[256];
    snprintf(url, sizeof(url), "https://%s%s?site_name=%s&count=%d",
             CONFIG_SITE_API_SERVER, CONFIG_SITE_API_PATH, site_name, count);

    ESP_LOGI(TAG, "Fetching: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(s_response_buffer);
        s_response_buffer = NULL;
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        ESP_LOGI(TAG, "HTTP Status = %d, Content-Length = %d",
                 status_code, content_length);

        if (status_code == 200 && s_response_len > 0) {
            if (print) {
                ESP_LOGD(TAG, "Response: %s", s_response_buffer);
            }

            // Parse JSON response
            success = parse_site_data(s_response_buffer, print);
            if (success) {
                ESP_LOGI(TAG, "Data parsed successfully");
            } else {
                ESP_LOGE(TAG, "Failed to parse JSON response");
            }
        } else {
            ESP_LOGE(TAG, "HTTP error: status=%d, len=%d", status_code, s_response_len);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(s_response_buffer);
    s_response_buffer = NULL;

    return success;
}
