/**
 * @file display.cpp
 * @brief High-level display functions implementation using bb_epaper
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cinttypes>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "bb_epaper.h"

// Include bb_epaper fonts
#include "nicoclean_8.h"
#include "Inter_14.h"
#include "Inter_18.h"
#include "Roboto_20.h"
#include "Roboto_Black_16.h"
#include "Roboto_Black_24.h"

extern "C" {
#include "display.h"
#include "site_data.h"
#include "lang.h"

// Time and date strings (extern from main.c)
extern char g_time_str[16];
extern char g_date_str[32];
}

static const char* TAG = "display";

// Pin definitions from Kconfig
#define EPD_PWR_PIN   CONFIG_EPD_PWR_PIN
#define EPD_BUSY_PIN  CONFIG_EPD_BUSY_PIN
#define EPD_RST_PIN   CONFIG_EPD_RST_PIN
#define EPD_DC_PIN    CONFIG_EPD_DC_PIN
#define EPD_CS_PIN    CONFIG_EPD_CS_PIN
#define EPD_MOSI_PIN  CONFIG_EPD_MOSI_PIN
#define EPD_CLK_PIN   CONFIG_EPD_SCK_PIN

#define SCREEN_WIDTH  CONFIG_SCREEN_WIDTH
#define SCREEN_HEIGHT CONFIG_SCREEN_HEIGHT

// Global e-paper display object (C++ class)
static BBEPAPER* epd = nullptr;

// Helper function declarations
static void draw_heading_section(void);
static void draw_graph_section(int x, int y);
static void epd_power_control(bool on);
static void draw_string_centered(const char* text, int y, const uint8_t* font);

static void epd_power_control(bool on)
{
    static bool gpio_configured = false;
    if (!gpio_configured) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << EPD_PWR_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_configured = true;
    }
    gpio_set_level((gpio_num_t)EPD_PWR_PIN, on ? 1 : 0);
    if (on) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Wait for power to stabilize
    }
}

// Helper to draw centered text using custom font
static void draw_string_centered(const char* text, int y, const uint8_t* font)
{
    BB_RECT rect;
    epd->setFont(font);
    epd->getStringBox(text, &rect);
    int x = (SCREEN_WIDTH - rect.w) / 2;
    epd->drawString(text, x, y);
}

extern "C" void display_init(void)
{
    ESP_LOGI(TAG, "Initializing display with bb_epaper");

    // Create display object for 4.2" 400x300 e-paper (GDEY042T81)
    epd = new BBEPAPER(EP42B_400x300);

    // Turn on power to the e-paper display
    epd_power_control(true);

    // Initialize e-paper display I/O
    ESP_LOGI(TAG, "Initializing EPD I/O...");
    epd->initIO(EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN, EPD_CS_PIN,
                EPD_MOSI_PIN, EPD_CLK_PIN, 10000000);

    // Allocate frame buffer
    ESP_LOGI(TAG, "Allocating buffer...");
    if (epd->allocBuffer() != BBEP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to allocate buffer!");
        return;
    }

    ESP_LOGI(TAG, "Display initialized: %dx%d", epd->width(), epd->height());
}

extern "C" void display_site_data(void)
{
    ESP_LOGI(TAG, "Drawing site data");

    epd_power_control(true);

    // Clear screen to white
    epd->fillScreen(BBEP_WHITE);
    epd->setTextColor(BBEP_BLACK, BBEP_WHITE);

    // Draw all sections
    draw_heading_section();
    draw_graph_section(0, 0);  // Graphs fill the screen below header

    // Write buffer to display and refresh
    ESP_LOGI(TAG, "Updating display...");
    epd->writePlane();
    epd->refresh(REFRESH_FULL, true);

    ESP_LOGI(TAG, "Display updated! Data time: %d ms, Op time: %d ms",
             epd->dataTime(), epd->opTime());

    // Put display to sleep
    epd->sleep(DEEP_SLEEP);
    epd_power_control(false);
}

extern "C" void display_no_data(void)
{
    ESP_LOGI(TAG, "Drawing no data screen");

    epd_power_control(true);

    epd->fillScreen(BBEP_WHITE);
    epd->setTextColor(BBEP_BLACK, BBEP_WHITE);

    draw_heading_section();

    // Show "Press button to fetch data" message - centered
    epd->setFont(FONT_12x16);
    const char* msg1 = "Press button to";
    const char* msg2 = "fetch data";
    int msg1_x = (SCREEN_WIDTH - strlen(msg1) * 12) / 2;
    int msg2_x = (SCREEN_WIDTH - strlen(msg2) * 12) / 2;
    epd->drawString(msg1, msg1_x, 120);
    epd->drawString(msg2, msg2_x, 150);

    epd->writePlane();
    epd->refresh(REFRESH_FULL, true);

    epd->sleep(DEEP_SLEEP);
    epd_power_control(false);
}

extern "C" void display_wifi_error(void)
{
    ESP_LOGI(TAG, "Drawing WiFi error screen");

    epd_power_control(true);

    epd->fillScreen(BBEP_WHITE);
    epd->setTextColor(BBEP_BLACK, BBEP_WHITE);

    draw_heading_section();

    // Show WiFi error message
    epd->setFont(FONT_12x16);
    const char* msg1 = "WiFi Error";
    const char* msg2 = "Connect to:";
    int msg1_x = (SCREEN_WIDTH - strlen(msg1) * 12) / 2;
    int msg2_x = (SCREEN_WIDTH - strlen(msg2) * 12) / 2;
    int ssid_x = (SCREEN_WIDTH - strlen(CONFIG_WIFI_SSID) * 12) / 2;

    epd->drawString(msg1, msg1_x, 100);
    epd->setFont(FONT_8x8);
    int msg2_x_8 = (SCREEN_WIDTH - strlen(msg2) * 8) / 2;
    epd->drawString(msg2, msg2_x_8, 135);
    epd->setFont(FONT_12x16);
    epd->drawString(CONFIG_WIFI_SSID, ssid_x, 160);

    epd->writePlane();
    epd->refresh(REFRESH_FULL, true);

    epd->sleep(DEEP_SLEEP);
    epd_power_control(false);
}

extern "C" void display_power_off(void)
{
    if (epd) {
        epd->sleep(DEEP_SLEEP);
    }
    epd_power_control(false);
}

static void draw_heading_section(void)
{
    // Time on left with FONT_12x16 (one size smaller than title)
    epd->setFont(FONT_12x16);
    char short_time[6] = {0};
    strncpy(short_time, g_time_str, 5);
    epd->drawString(short_time, 4, 6);

    // Site name centered with FONT_16x16 (large and prominent)
    char title[64];
    snprintf(title, sizeof(title), "%s, Ladakh", g_site_name);
    epd->setFont(FONT_16x16);

    // Calculate center position for title (FONT_16x16 is 16 pixels per char)
    int title_len = strlen(title);
    int title_x = (SCREEN_WIDTH - title_len * 16) / 2;
    epd->drawString(title, title_x, 6);

    // Date on right with FONT_12x16 (one size smaller than title)
    char short_date[16] = {0};
    if (strlen(g_date_str) > 12) {
        char day[3] = {0};
        char mon[4] = {0};
        strncpy(day, g_date_str + 5, 2);
        strncpy(mon, g_date_str + 9, 3);
        snprintf(short_date, sizeof(short_date), "%s-%s", day, mon);
    } else {
        strcpy(short_date, g_date_str);
    }

    epd->setFont(FONT_12x16);
    int date_len = strlen(short_date);
    epd->drawString(short_date, SCREEN_WIDTH - date_len * 12 - 4, 6);

    // Double line separator below header (FONT_16x16 is 16px tall + margin)
    epd->drawLine(0, 26, SCREEN_WIDTH, 26, BBEP_BLACK);
    epd->drawLine(0, 28, SCREEN_WIDTH, 28, BBEP_BLACK);
}

static void draw_graph_section(int x, int y)
{
    // Allocate on heap to avoid stack overflow
    float* temperature_readings = (float*)malloc(MAX_READINGS * sizeof(float));
    float* water_temp_readings = (float*)malloc(MAX_READINGS * sizeof(float));
    float* pressure_readings = (float*)malloc(MAX_READINGS * sizeof(float));
    float* hourly_temp = (float*)malloc(MAX_HOURLY_READINGS * sizeof(float));
    float* hourly_water = (float*)malloc(MAX_HOURLY_READINGS * sizeof(float));
    float* hourly_pressure = (float*)malloc(MAX_HOURLY_READINGS * sizeof(float));

    if (!temperature_readings || !water_temp_readings || !pressure_readings ||
        !hourly_temp || !hourly_water || !hourly_pressure) {
        ESP_LOGE(TAG, "Failed to allocate memory for graph data");
        // Free any allocated memory
        free(temperature_readings);
        free(water_temp_readings);
        free(pressure_readings);
        free(hourly_temp);
        free(hourly_water);
        free(hourly_pressure);
        return;
    }

    // Prepare 5-minute data arrays (reverse order - oldest first)
    for (int r = 0; r < g_num_readings; r++) {
        int idx = g_num_readings - 1 - r;
        temperature_readings[r] = g_site_readings[idx].temperature;
        water_temp_readings[r] = g_site_readings[idx].water_temp;
        pressure_readings[r] = g_site_readings[idx].pressure;
    }

    // Allocate arrays for data availability flags
    bool* has_temp_data = (bool*)malloc(MAX_HOURLY_READINGS * sizeof(bool));
    bool* has_water_data = (bool*)malloc(MAX_HOURLY_READINGS * sizeof(bool));
    bool* has_pressure_data = (bool*)malloc(MAX_HOURLY_READINGS * sizeof(bool));

    if (!has_temp_data || !has_water_data || !has_pressure_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for data flags");
        free(has_temp_data);
        free(has_water_data);
        free(has_pressure_data);
        free(temperature_readings);
        free(water_temp_readings);
        free(pressure_readings);
        free(hourly_temp);
        free(hourly_water);
        free(hourly_pressure);
        return;
    }

    // Aggregate to full 24 hours with missing data marked
    aggregate_to_24_hours(temperature_readings, g_num_readings, hourly_temp, has_temp_data);
    aggregate_to_24_hours(water_temp_readings, g_num_readings, hourly_water, has_water_data);
    aggregate_to_24_hours(pressure_readings, g_num_readings, hourly_pressure, has_pressure_data);

    int available_hours = 0;
    for (int i = 0; i < MAX_HOURLY_READINGS; i++) {
        if (has_temp_data[i]) available_hours++;
    }
    ESP_LOGI(TAG, "Aggregated %d readings into %d hours (out of 24)", g_num_readings, available_hours);

    // Full screen layout - 3 graphs stacked vertically
    int start_y = 32;  // Just below header (FONT_16x16=16px + margins + double line at 28px)
    int graph_h = 86;  // Height for each graph
    int graph_spacing = 4;  // Spacing between graphs
    int graph_w = SCREEN_WIDTH - 12;  // Full width with margins

    // Air Temperature Graph (bar chart) - always 24 hours
    display_draw_graph(5, start_y, graph_w, graph_h, -10, 10,
                       "Air Temp", hourly_temp, 24,
                       true, true, has_temp_data);

    // Water Temperature Graph (bar chart) - always 24 hours
    display_draw_graph(5, start_y + graph_h + graph_spacing, graph_w, graph_h, 0, 10,
                       "Water Temp", hourly_water, 24,
                       true, true, has_water_data);

    // Pressure Graph (bar chart) - always 24 hours
    display_draw_graph(5, start_y + 2 * (graph_h + graph_spacing), graph_w, graph_h, 0, 2,
                       "Pressure", hourly_pressure, 24,
                       true, true, has_pressure_data);

    // Free allocated memory
    free(temperature_readings);
    free(water_temp_readings);
    free(pressure_readings);
    free(hourly_temp);
    free(hourly_water);
    free(hourly_pressure);
    free(has_temp_data);
    free(has_water_data);
    free(has_pressure_data);
}

extern "C" void display_draw_graph(int x_pos, int y_pos, int gwidth, int gheight,
                                   float y_min, float y_max, const char* title,
                                   float* data, int readings,
                                   bool auto_scale, bool bar_chart, bool* has_data)
{
    const int margin_left = 4;   // Small left margin
    const int margin_top = 18;   // Space for title row with FONT_8x8 (8px + margins)
    const int margin_right = 4;  // Small right margin
    const int margin_bottom = 12; // Space for x-axis hour labels (FONT_6x8 = 8px + margin)

    float max_y = -10000;
    float min_y = 10000;
    float current_value = (readings > 0) ? data[readings - 1] : 0;  // Last reading is current

    if (auto_scale && readings > 0) {
        for (int i = 0; i < readings; i++) {
            if (data[i] > max_y) max_y = data[i];
            if (data[i] < min_y) min_y = data[i];
        }
        y_max = ceilf(max_y + 0.5f);
        y_min = floorf(min_y - 0.5f);
    }

    // Avoid division by zero
    if (y_max == y_min) {
        y_max = y_min + 1;
    }

    // Draw outer frame with thicker border
    epd->drawRect(x_pos, y_pos, gwidth, gheight, BBEP_BLACK);
    epd->drawRect(x_pos + 1, y_pos + 1, gwidth - 2, gheight - 2, BBEP_BLACK);

    // All text in same row with FONT_8x8
    epd->setFont(FONT_8x8);
    int text_y = y_pos + 6;

    // Left side: "Max:xx.x Min:xx.x"
    char left_str[32];
    snprintf(left_str, sizeof(left_str), "Max:%.1f Min:%.1f", y_max, y_min);
    epd->drawString(left_str, x_pos + 4, text_y);

    // Center: Title (graph name)
    int title_len = strlen(title);
    int title_x = x_pos + (gwidth - title_len * 8) / 2;
    epd->drawString(title, title_x, text_y);

    // Right side: Current value
    char curr_str[16];
    snprintf(curr_str, sizeof(curr_str), "%.1f", current_value);
    int curr_len = strlen(curr_str);
    epd->drawString(curr_str, x_pos + gwidth - curr_len * 8 - 4, text_y);

    // Draw separator line below title row
    epd->drawLine(x_pos + 2, y_pos + margin_top - 2, x_pos + gwidth - 2, y_pos + margin_top - 2, BBEP_BLACK);

    // Draw graph area
    int graph_x = x_pos + margin_left;
    int graph_y = y_pos + margin_top;
    int graph_w = gwidth - margin_left - margin_right;
    int graph_h = gheight - margin_top - margin_bottom;

    if (readings < 1) return;

    if (bar_chart) {
        // Always draw 24 bars for 24 hours
        const int num_bars = 24;
        int bar_spacing = 3;
        int available_width = graph_w - (num_bars + 1) * bar_spacing;
        int bar_width = available_width / num_bars;

        // Ensure minimum bar width
        if (bar_width < 6) {
            bar_width = 6;
            bar_spacing = (graph_w - (bar_width * num_bars)) / (num_bars + 1);
            if (bar_spacing < 1) bar_spacing = 1;
        }

        // Calculate starting x position to center all bars
        int total_bars_width = num_bars * bar_width + (num_bars - 1) * bar_spacing;
        int x_start = graph_x + (graph_w - total_bars_width) / 2;

        for (int i = 0; i < num_bars; i++) {
            int bar_x = x_start + i * (bar_width + bar_spacing);
            int bar_center_x = bar_x + bar_width / 2;
            int bar_center_y = graph_y + graph_h / 2;

            // Check if this hour has data
            bool has_value = (has_data != NULL) ? has_data[i] : true;

            if (has_value) {
                // Draw bar for valid data
                float constrained = data[i];
                if (constrained < y_min) constrained = y_min;
                if (constrained > y_max) constrained = y_max;

                int bar_height = (int)((constrained - y_min) / (y_max - y_min) * graph_h);
                if (bar_height < 1 && constrained > y_min) bar_height = 1;

                int bar_y = graph_y + graph_h - bar_height;

                if (bar_height > 0) {
                    epd->fillRect(bar_x, bar_y, bar_width, bar_height, BBEP_BLACK);
                }

                // Mark current (last) bar with circle
                if (i == num_bars - 1) {
                    epd->fillCircle(bar_center_x, bar_y - 2, 2, BBEP_BLACK);
                }
            } else {
                // Draw X mark for missing data
                int x_size = bar_width / 2;
                if (x_size > 8) x_size = 8;  // Limit X size
                if (x_size < 4) x_size = 4;

                // Draw X using two diagonal lines
                epd->drawLine(bar_center_x - x_size, bar_center_y - x_size,
                             bar_center_x + x_size, bar_center_y + x_size, BBEP_BLACK);
                epd->drawLine(bar_center_x - x_size, bar_center_y + x_size,
                             bar_center_x + x_size, bar_center_y - x_size, BBEP_BLACK);
            }

            // Draw hour labels (23, 22, 21, ..., 1, 0)
            // Draw every 2nd label to avoid overlap (show 12 labels total)
            if (i % 2 == 0) {
                int hour_label = 23 - i;

                // Draw single digit at a time for reliability
                epd->setFont(FONT_8x8);

                char digit_str[2];
                digit_str[1] = '\0';

                int text_x = bar_x + (bar_width / 2);  // Start at center

                if (hour_label >= 10) {
                    // Two digit number - draw each digit separately
                    digit_str[0] = '0' + (hour_label / 10);
                    epd->drawString(digit_str, text_x - 8, y_pos + gheight - margin_bottom + 4);

                    digit_str[0] = '0' + (hour_label % 10);
                    epd->drawString(digit_str, text_x, y_pos + gheight - margin_bottom + 4);
                } else {
                    // Single digit number - center it
                    digit_str[0] = '0' + hour_label;
                    epd->drawString(digit_str, text_x - 4, y_pos + gheight - margin_bottom + 4);
                }
            }
        }
    }
}
