/**
 * @file main.c
 * @brief ESP32-S3 Site Display - Main application
 *
 * E-paper weather display that fetches IoT sensor data from
 * a REST API and displays it on a 4.2" EPD display.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"

#include "wifi_manager.h"
#include "http_client.h"
#include "site_data.h"
#include "nvs_storage.h"
#include "display.h"
#include "lang.h"

static const char* TAG = "main";

// Version
#define VERSION "1.0.0-idf"

// Time strings (global for display access)
char g_time_str[16] = {0};
char g_date_str[32] = {0};

// Button event queue
static QueueHandle_t s_button_queue = NULL;

typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_UP,
    BTN_EVENT_DOWN,
    BTN_EVENT_FETCH,
} button_event_t;

// Forward declarations
static void setup_gpio(void);
static void setup_time(void);
static bool update_local_time(void);
static void fetch_and_display(void);
static void gpio_isr_handler(void* arg);
static void button_task(void* arg);

void app_main(void)
{
    ESP_LOGI(TAG, "== Site Display v%s ==", VERSION);
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    // Initialize NVS
    ESP_ERROR_CHECK(nvs_storage_init());

    // Load saved site index
    int saved_index = CONFIG_DEFAULT_SITE_INDEX;
    if (nvs_load_site_index(&saved_index) == ESP_OK) {
        g_current_site_index = saved_index;
    }

    // Validate index
    if (g_current_site_index < 0 || g_current_site_index >= g_num_sites) {
        g_current_site_index = 0;
    }

    // Set current site name
    g_site_name = g_site_list[g_current_site_index];
    ESP_LOGI(TAG, "Site: %s (index %d)", g_site_name, g_current_site_index);

    // Initialize display
    display_init();

    // Setup GPIO for buttons
    setup_gpio();

    // Connect to WiFi
    esp_err_t wifi_ret = wifi_connect();
    if (wifi_ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected");
        display_no_data();
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
        display_wifi_error();
    }

    // Create button task (increased stack for 288 readings)
    s_button_queue = xQueueCreate(10, sizeof(button_event_t));
    xTaskCreate(button_task, "button_task", 8192, NULL, 10, NULL);

    ESP_LOGI(TAG, "System ready - press fetch button to get data");

    // Main loop is handled by FreeRTOS tasks
    // The button_task handles user input
}

static void setup_gpio(void)
{
    ESP_LOGD(TAG, "Setting up GPIO");

    // Configure button pins as inputs with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_ROT_UP_PIN) |
                        (1ULL << CONFIG_ROT_DOWN_PIN) |
                        (1ULL << CONFIG_BTN_FETCH_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    // Install GPIO ISR service
    gpio_install_isr_service(0);

    // Add ISR handlers
    gpio_isr_handler_add(CONFIG_ROT_UP_PIN, gpio_isr_handler, (void*)BTN_EVENT_UP);
    gpio_isr_handler_add(CONFIG_ROT_DOWN_PIN, gpio_isr_handler, (void*)BTN_EVENT_DOWN);
    gpio_isr_handler_add(CONFIG_BTN_FETCH_PIN, gpio_isr_handler, (void*)BTN_EVENT_FETCH);
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    button_event_t event = (button_event_t)(int)arg;
    xQueueSendFromISR(s_button_queue, &event, NULL);
}

static void button_task(void* arg)
{
    button_event_t event;
    TickType_t last_event_time = 0;
    const TickType_t debounce_ticks = pdMS_TO_TICKS(200);

    while (1) {
        if (xQueueReceive(s_button_queue, &event, portMAX_DELAY)) {
            // Debounce
            TickType_t now = xTaskGetTickCount();
            if ((now - last_event_time) < debounce_ticks) {
                continue;
            }
            last_event_time = now;

            switch (event) {
                case BTN_EVENT_UP:
                    ESP_LOGI(TAG, "Button: UP");
                    g_current_site_index = (g_current_site_index + 1) % g_num_sites;
                    g_site_name = g_site_list[g_current_site_index];
                    g_data_loaded = false;
                    nvs_save_site_index(g_current_site_index);
                    ESP_LOGI(TAG, "Site changed to: %s", g_site_name);
                    display_no_data();
                    break;

                case BTN_EVENT_DOWN:
                    ESP_LOGI(TAG, "Button: DOWN");
                    g_current_site_index = (g_current_site_index - 1 + g_num_sites) % g_num_sites;
                    g_site_name = g_site_list[g_current_site_index];
                    g_data_loaded = false;
                    nvs_save_site_index(g_current_site_index);
                    ESP_LOGI(TAG, "Site changed to: %s", g_site_name);
                    display_no_data();
                    break;

                case BTN_EVENT_FETCH:
                    ESP_LOGI(TAG, "Button: FETCH");
                    fetch_and_display();
                    break;

                default:
                    break;
            }
        }
    }
}

static void fetch_and_display(void)
{
    if (!wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi not connected, reconnecting...");
        if (wifi_connect() != ESP_OK) {
            ESP_LOGE(TAG, "WiFi reconnect failed");
            display_wifi_error();
            return;
        }
        ESP_LOGI(TAG, "WiFi reconnected");
    }

    setup_time();

    if (update_local_time()) {
        ESP_LOGI(TAG, "Time: %s", g_time_str);

        bool rx_data = false;
        for (int i = 0; i < 2 && !rx_data; i++) {
            rx_data = fetch_site_data(g_site_name, CONFIG_SITE_READING_COUNT, true);
            if (!rx_data) {
                ESP_LOGW(TAG, "Fetch attempt %d failed, retrying...", i + 1);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        if (rx_data) {
            g_data_loaded = true;
            display_site_data();
        } else {
            ESP_LOGE(TAG, "Failed to fetch data");
        }
    } else {
        ESP_LOGE(TAG, "NTP time sync failed");
    }
}

static void time_sync_notification_cb(struct timeval* tv)
{
    ESP_LOGI(TAG, "Time synchronized");
}

static void setup_time(void)
{
    ESP_LOGI(TAG, "Setting up time with NTP");

    // Initialize SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_NTP_SERVER);
    esp_sntp_setservername(1, "time.cloudflare.com");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Set timezone
    setenv("TZ", CONFIG_TIMEZONE, 1);
    tzset();

    // Wait for time to sync (max 10 seconds)
    int retry = 0;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < 20) {
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d)", retry);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static bool update_local_time(void)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    // Check if time is valid (year > 2020)
    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGE(TAG, "Time not synchronized yet");
        return false;
    }

    // Format time string (HH:MM:SS)
    strftime(g_time_str, sizeof(g_time_str), "%H:%M:%S", &timeinfo);

    // Format date string (Sun, 23. Nov 2025)
    snprintf(g_date_str, sizeof(g_date_str), "%s, %02d. %s %04d",
             WEEKDAY_D[timeinfo.tm_wday],
             timeinfo.tm_mday,
             MONTH_M[timeinfo.tm_mon],
             timeinfo.tm_year + 1900);

    ESP_LOGI(TAG, "Local time: %s %s", g_date_str, g_time_str);
    return true;
}
