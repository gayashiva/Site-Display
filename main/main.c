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
#include "esp_sleep.h"

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

// Per-site data cache (on-demand allocation to save memory)
typedef struct {
    bool has_data;
    site_reading_t* readings;  // Allocated on-demand
    int num_readings;
    char time_str[16];
    char date_str[32];
} site_cache_t;

static site_cache_t* s_site_cache = NULL;

typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_UP,
    BTN_EVENT_DOWN,
    BTN_EVENT_MID,
    BTN_EVENT_MENU,
    BTN_EVENT_EXIT,
} button_event_t;

// Forward declarations
static void setup_gpio(void);
static void setup_time(void);
static bool update_local_time(void);
static void fetch_and_display(void);
static void gpio_isr_handler(void* arg);
static void button_task(void* arg);
static void load_cached_site_data(int site_index);
static void save_current_site_data(int site_index);
static void display_current_site(void);
static void first_boot_fetch_all_sites(void);

void app_main(void)
{
    ESP_LOGI(TAG, "== Site Display v%s ==", VERSION);
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    // Reduce verbosity of certificate validation logs
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);
    esp_log_level_set("wifi", ESP_LOG_WARN);

    // Initialize NVS
    ESP_ERROR_CHECK(nvs_storage_init());

    // Allocate cache for all sites
    s_site_cache = (site_cache_t*)calloc(g_num_sites, sizeof(site_cache_t));
    if (s_site_cache == NULL) {
        ESP_LOGE(TAG, "Failed to allocate site cache");
    }

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

    // Initialize current site - no cached data initially
    g_data_loaded = false;
    g_num_readings = 0;

    // Connect to WiFi
    esp_err_t wifi_ret = wifi_connect();
    if (wifi_ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected");

        // Check if this is first boot - look for any cached site data
        // If no sites have cached data, treat as first boot
        bool has_any_cache = false;
        for (int i = 0; i < g_num_sites && s_site_cache != NULL; i++) {
            if (s_site_cache[i].has_data) {
                has_any_cache = true;
                break;
            }
        }

        bool is_first = !has_any_cache;
        ESP_LOGI(TAG, "=== First boot check: %s (cached sites: %s) ===",
                 is_first ? "YES - will fetch all" : "NO - skip fetch",
                 has_any_cache ? "found" : "none");

        if (is_first) {
            ESP_LOGI(TAG, "First boot detected - fetching data for all sites");
            first_boot_fetch_all_sites();

            // Load cached data for current site
            load_cached_site_data(g_current_site_index);
            display_current_site();
        } else {
            ESP_LOGI(TAG, "Not first boot - showing no data screen");
            display_no_data();
        }
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
        display_wifi_error();
    }

    // Create button task (increased stack for 288 readings)
    s_button_queue = xQueueCreate(10, sizeof(button_event_t));
    xTaskCreate(button_task, "button_task", 8192, NULL, 10, NULL);

    ESP_LOGI(TAG, "System ready - press fetch button to get data");
    ESP_LOGI(TAG, "To force fetch all sites, erase NVS with: idf.py erase-flash");

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
                        (1ULL << CONFIG_BTN_FETCH_PIN) |
                        (1ULL << CONFIG_BTN_MENU_PIN) |
                        (1ULL << CONFIG_BTN_EXIT_PIN),
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
    gpio_isr_handler_add(CONFIG_BTN_FETCH_PIN, gpio_isr_handler, (void*)BTN_EVENT_MID);
    gpio_isr_handler_add(CONFIG_BTN_MENU_PIN, gpio_isr_handler, (void*)BTN_EVENT_MENU);
    gpio_isr_handler_add(CONFIG_BTN_EXIT_PIN, gpio_isr_handler, (void*)BTN_EVENT_EXIT);
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    button_event_t event = (button_event_t)(int)arg;
    xQueueSendFromISR(s_button_queue, &event, NULL);
}

static void button_task(void* arg)
{
    button_event_t event;
    TickType_t last_event_time[5] = {0};  // Track debounce per button type (5 buttons now)
    const TickType_t debounce_ticks = pdMS_TO_TICKS(500);  // Increased debounce time

    while (1) {
        if (xQueueReceive(s_button_queue, &event, portMAX_DELAY)) {
            // Debounce per button type
            TickType_t now = xTaskGetTickCount();
            int button_idx = (int)event - 1;  // Convert event to index (0-4)

            if (button_idx >= 0 && button_idx < 5) {
                if ((now - last_event_time[button_idx]) < debounce_ticks) {
                    continue;
                }
                last_event_time[button_idx] = now;
            }

            switch (event) {
                case BTN_EVENT_UP:
                    ESP_LOGI(TAG, "Rotary: UP");
                    g_current_site_index = (g_current_site_index + 1) % g_num_sites;
                    g_site_name = g_site_list[g_current_site_index];
                    nvs_save_site_index(g_current_site_index);
                    ESP_LOGI(TAG, "Site changed to: %s", g_site_name);

                    // Clear any pending button events while display is updating
                    xQueueReset(s_button_queue);

                    // Load cached data for this site and display
                    load_cached_site_data(g_current_site_index);
                    display_current_site();
                    break;

                case BTN_EVENT_DOWN:
                    ESP_LOGI(TAG, "Rotary: DOWN");
                    g_current_site_index = (g_current_site_index - 1 + g_num_sites) % g_num_sites;
                    g_site_name = g_site_list[g_current_site_index];
                    nvs_save_site_index(g_current_site_index);
                    ESP_LOGI(TAG, "Site changed to: %s", g_site_name);

                    // Clear any pending button events while display is updating
                    xQueueReset(s_button_queue);

                    // Load cached data for this site and display
                    load_cached_site_data(g_current_site_index);
                    display_current_site();
                    break;

                case BTN_EVENT_MID:
                    ESP_LOGI(TAG, "Rotary: MID (fetch current site)");

                    // Clear any pending button events while fetching and displaying
                    xQueueReset(s_button_queue);
                    fetch_and_display();
                    break;

                case BTN_EVENT_MENU:
                    ESP_LOGI(TAG, "Menu: PRESS (fetch all sites)");

                    // Clear any pending button events while fetching all sites
                    xQueueReset(s_button_queue);

                    // Check WiFi connection
                    if (!wifi_is_connected()) {
                        ESP_LOGI(TAG, "WiFi not connected, reconnecting...");
                        if (wifi_connect() != ESP_OK) {
                            ESP_LOGE(TAG, "WiFi reconnect failed - cannot fetch all sites");
                            display_wifi_error();
                            break;
                        }
                        ESP_LOGI(TAG, "WiFi reconnected");
                    }

                    // Setup time and fetch all sites
                    setup_time();
                    if (update_local_time()) {
                        first_boot_fetch_all_sites();
                        // Load cached data for current site and display
                        load_cached_site_data(g_current_site_index);
                        display_current_site();
                    } else {
                        ESP_LOGE(TAG, "NTP time sync failed - cannot fetch all sites");
                    }
                    break;

                case BTN_EVENT_EXIT:
                    ESP_LOGI(TAG, "Exit: PRESS (entering light sleep)");

                    // Power off display
                    display_power_off();

                    // Configure GPIO1 as wake-up source (low level = button pressed)
                    ESP_ERROR_CHECK(gpio_wakeup_enable(CONFIG_BTN_EXIT_PIN, GPIO_INTR_LOW_LEVEL));
                    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

                    // Enter light sleep (can wake up from GPIO)
                    ESP_LOGI(TAG, "Entering light sleep... Press EXIT button to wake");
                    esp_light_sleep_start();

                    // Woken up
                    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
                    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
                        ESP_LOGI(TAG, "Woken up by EXIT button");
                    }

                    // Small delay for button release
                    vTaskDelay(pdMS_TO_TICKS(500));
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
        ESP_LOGD(TAG, "Time: %s", g_time_str);

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

            // Save data to cache
            save_current_site_data(g_current_site_index);

            display_site_data();
        } else {
            ESP_LOGE(TAG, "Failed to fetch data");
        }
    } else {
        ESP_LOGE(TAG, "NTP time sync failed");
    }
}

static void load_cached_site_data(int site_index)
{
    if (s_site_cache == NULL || site_index < 0 || site_index >= g_num_sites) {
        g_data_loaded = false;
        g_num_readings = 0;
        memset(g_time_str, 0, sizeof(g_time_str));
        memset(g_date_str, 0, sizeof(g_date_str));
        return;
    }

    site_cache_t* cache = &s_site_cache[site_index];

    if (cache->has_data && cache->readings != NULL && cache->num_readings > 0) {
        // Restore cached data to global variables
        memcpy(g_site_readings, cache->readings, sizeof(site_reading_t) * cache->num_readings);
        g_num_readings = cache->num_readings;
        strncpy(g_time_str, cache->time_str, sizeof(g_time_str));
        strncpy(g_date_str, cache->date_str, sizeof(g_date_str));
        g_data_loaded = true;
        ESP_LOGI(TAG, "Loaded cached data for %s (%d readings)", g_site_name, g_num_readings);
    } else {
        // No cached data - clear everything
        g_data_loaded = false;
        g_num_readings = 0;
        memset(g_time_str, 0, sizeof(g_time_str));
        memset(g_date_str, 0, sizeof(g_date_str));
        ESP_LOGD(TAG, "No cached data for %s", g_site_name);
    }
}

static void save_current_site_data(int site_index)
{
    if (s_site_cache == NULL || site_index < 0 || site_index >= g_num_sites || g_num_readings == 0) {
        return;
    }

    site_cache_t* cache = &s_site_cache[site_index];

    // Free old cache if it exists
    if (cache->readings != NULL) {
        free(cache->readings);
        cache->readings = NULL;
    }

    // Allocate memory for readings
    cache->readings = (site_reading_t*)malloc(sizeof(site_reading_t) * g_num_readings);
    if (cache->readings == NULL) {
        ESP_LOGE(TAG, "Failed to allocate cache for %s", g_site_name);
        cache->has_data = false;
        return;
    }

    // Save current data to cache
    memcpy(cache->readings, g_site_readings, sizeof(site_reading_t) * g_num_readings);
    cache->num_readings = g_num_readings;
    strncpy(cache->time_str, g_time_str, sizeof(cache->time_str));
    strncpy(cache->date_str, g_date_str, sizeof(cache->date_str));
    cache->has_data = true;

    ESP_LOGI(TAG, "Cached data for %s (%d readings)", g_site_name, g_num_readings);
}

static void display_current_site(void)
{
    if (g_data_loaded) {
        display_site_data();
    } else {
        display_no_data();
    }
}

static void first_boot_fetch_all_sites(void)
{
    // Setup time once for all fetches
    setup_time();

    if (!update_local_time()) {
        ESP_LOGE(TAG, "NTP time sync failed - skipping first boot fetch");
        return;
    }

    ESP_LOGI(TAG, "Fetching data for all %d sites...", g_num_sites);

    // Save original site index
    int original_site = g_current_site_index;

    // Fetch data for each site
    for (int i = 0; i < g_num_sites; i++) {
        g_current_site_index = i;
        g_site_name = g_site_list[i];

        ESP_LOGI(TAG, "[%d/%d] Fetching %s...", i + 1, g_num_sites, g_site_name);

        bool success = false;
        for (int retry = 0; retry < 2 && !success; retry++) {
            success = fetch_site_data(g_site_name, CONFIG_SITE_READING_COUNT, false);
            if (!success && retry < 1) {
                ESP_LOGW(TAG, "Retry fetching %s...", g_site_name);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        if (success) {
            g_data_loaded = true;
            save_current_site_data(i);

            // Display the fetched data immediately
            display_site_data();

            ESP_LOGI(TAG, "Successfully cached and displayed %s", g_site_name);
        } else {
            ESP_LOGW(TAG, "Failed to fetch %s", g_site_name);

            // Show "no data" screen for failed sites
            g_data_loaded = false;
            display_no_data();
        }

        // Small delay between fetches to allow display update and avoid server overload
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Restore original site
    g_current_site_index = original_site;
    g_site_name = g_site_list[original_site];

    ESP_LOGI(TAG, "First boot fetch completed");
}

static bool s_sntp_initialized = false;

static void time_sync_notification_cb(struct timeval* tv)
{
    ESP_LOGD(TAG, "Time synchronized");
}

static void setup_time(void)
{
    if (!s_sntp_initialized) {
        ESP_LOGD(TAG, "Setting up time with NTP");

        // Initialize SNTP
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, CONFIG_NTP_SERVER);
        esp_sntp_setservername(1, "time.cloudflare.com");
        sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        esp_sntp_init();

        // Set timezone
        setenv("TZ", CONFIG_TIMEZONE, 1);
        tzset();

        s_sntp_initialized = true;
    }

    // Wait for time to sync (max 10 seconds)
    int retry = 0;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < 20) {
        ESP_LOGD(TAG, "Waiting for NTP sync... (%d)", retry);
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

    ESP_LOGD(TAG, "Local time: %s %s", g_date_str, g_time_str);
    return true;
}
