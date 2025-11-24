/**
 * @file site_data.c
 * @brief Site data structures and JSON parsing implementation
 */

#include <string.h>
#include <math.h>
#include <time.h>
#include <inttypes.h>
#include "esp_log.h"
#include "cJSON.h"
#include "site_data.h"

static const char* TAG = "site_data";

// Global site data
site_info_t g_site_info = {0};
site_reading_t g_current_reading = {0};
site_reading_t g_site_readings[MAX_READINGS] = {0};
int g_num_readings = 0;

// Site list for rotation
const char* g_site_list[] = {
    "Sakti", "Likir", "Baroo", "Tuna",
    "Ayee", "Chanigund", "Stakmo", "Igoo"
};
const int g_num_sites = 8;
int g_current_site_index = 0;
bool g_data_loaded = false;

// Current site name
const char* g_site_name = "Sakti";

void convert_unix_time(int32_t unix_time, char* output, size_t output_len)
{
    time_t tm = unix_time;
    struct tm* time_info = gmtime(&tm);
    strftime(output, output_len, "%H:%M %d/%m/%y", time_info);
}

bool parse_site_data(const char* json_str, bool print)
{
    cJSON* root = cJSON_Parse(json_str);
    if (root == NULL) {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error: %s", error_ptr);
        }
        return false;
    }

    // Parse site metadata
    cJSON* site_name = cJSON_GetObjectItem(root, "site_name");
    if (cJSON_IsString(site_name)) {
        strncpy(g_site_info.site_name, site_name->valuestring, MAX_SITE_NAME_LEN - 1);
    }

    cJSON* site_type = cJSON_GetObjectItem(root, "site_type");
    if (cJSON_IsString(site_type)) {
        strncpy(g_site_info.site_type, site_type->valuestring, sizeof(g_site_info.site_type) - 1);
    }

    cJSON* active = cJSON_GetObjectItem(root, "active");
    if (cJSON_IsBool(active)) {
        g_site_info.active = cJSON_IsTrue(active);
    }

    cJSON* tz_offset = cJSON_GetObjectItem(root, "timezone_offset");
    if (cJSON_IsNumber(tz_offset)) {
        g_site_info.timezone_offset = tz_offset->valueint;
    }

    cJSON* query_time = cJSON_GetObjectItem(root, "query_time");
    if (cJSON_IsNumber(query_time)) {
        g_site_info.query_time = query_time->valueint;
    }

    // Parse current reading
    cJSON* current = cJSON_GetObjectItem(root, "current");
    if (current != NULL) {
        cJSON* item;

        item = cJSON_GetObjectItem(current, "dt");
        if (cJSON_IsNumber(item)) g_current_reading.dt = item->valueint;

        item = cJSON_GetObjectItem(current, "timestamp");
        if (cJSON_IsString(item)) {
            strncpy(g_current_reading.timestamp, item->valuestring, MAX_TIMESTAMP_LEN - 1);
        }

        item = cJSON_GetObjectItem(current, "temperature");
        if (cJSON_IsNumber(item)) g_current_reading.temperature = (float)item->valuedouble;

        item = cJSON_GetObjectItem(current, "water_temp");
        if (cJSON_IsNumber(item)) g_current_reading.water_temp = (float)item->valuedouble;

        item = cJSON_GetObjectItem(current, "pressure");
        if (cJSON_IsNumber(item)) g_current_reading.pressure = (float)item->valuedouble;

        item = cJSON_GetObjectItem(current, "voltage");
        if (cJSON_IsNumber(item)) g_current_reading.voltage = (float)item->valuedouble;

        item = cJSON_GetObjectItem(current, "counter");
        if (cJSON_IsNumber(item)) g_current_reading.counter = item->valueint;
    }

    // Parse historical readings array
    cJSON* readings = cJSON_GetObjectItem(root, "readings");
    if (cJSON_IsArray(readings)) {
        int array_size = cJSON_GetArraySize(readings);
        g_num_readings = (array_size < MAX_READINGS) ? array_size : MAX_READINGS;

        for (int r = 0; r < g_num_readings; r++) {
            cJSON* reading = cJSON_GetArrayItem(readings, r);
            if (reading == NULL) continue;

            cJSON* item;

            item = cJSON_GetObjectItem(reading, "dt");
            if (cJSON_IsNumber(item)) g_site_readings[r].dt = item->valueint;

            item = cJSON_GetObjectItem(reading, "timestamp");
            if (cJSON_IsString(item)) {
                strncpy(g_site_readings[r].timestamp, item->valuestring, MAX_TIMESTAMP_LEN - 1);
            }

            item = cJSON_GetObjectItem(reading, "temperature");
            if (cJSON_IsNumber(item)) g_site_readings[r].temperature = (float)item->valuedouble;

            item = cJSON_GetObjectItem(reading, "water_temp");
            if (cJSON_IsNumber(item)) g_site_readings[r].water_temp = (float)item->valuedouble;

            item = cJSON_GetObjectItem(reading, "pressure");
            if (cJSON_IsNumber(item)) g_site_readings[r].pressure = (float)item->valuedouble;

            item = cJSON_GetObjectItem(reading, "voltage");
            if (cJSON_IsNumber(item)) g_site_readings[r].voltage = (float)item->valuedouble;

            item = cJSON_GetObjectItem(reading, "counter");
            if (cJSON_IsNumber(item)) g_site_readings[r].counter = item->valueint;
        }
    }

    cJSON_Delete(root);

    if (print) {
        ESP_LOGI(TAG, "Site: %s | Readings: %d | Current: %.1fC",
                 g_site_info.site_name, g_num_readings, g_current_reading.temperature);
    }

    return true;
}

int julian_date(int d, int m, int y)
{
    int mm, yy, k1, k2, k3, j;
    yy = y - (int)((12 - m) / 10);
    mm = m + 9;
    if (mm >= 12) mm = mm - 12;
    k1 = (int)(365.25 * (yy + 4712));
    k2 = (int)(30.6001 * mm + 0.5);
    k3 = (int)((int)((yy / 100) + 49) * 0.75) - 38;
    j = k1 + k2 + d + 59 + 1;
    if (j > 2299160) j = j - k3;
    return j;
}

float sum_of_precip(float* data_array, int readings)
{
    float sum = 0;
    for (int i = 0; i < readings; i++) {
        sum += data_array[i];
    }
    return sum;
}

double normalized_moon_phase(int d, int m, int y)
{
    int j = julian_date(d, m, y);
    double phase = (j + 4.867) / 29.53059;
    return phase - (int)phase;
}

int aggregate_to_hourly(float* five_min_data, int num_readings, float* hourly_data)
{
    // 12 readings per hour (5-minute intervals)
    const int readings_per_hour = 12;
    int hours = 24;

    // Limit to available data
    if (num_readings < readings_per_hour) {
        return 0;
    }

    // Calculate how many complete hours we have
    int available_hours = num_readings / readings_per_hour;
    if (available_hours > hours) {
        available_hours = hours;
    }

    // Aggregate into hourly averages (oldest to newest)
    for (int h = 0; h < available_hours; h++) {
        float sum = 0;
        int count = 0;

        // Average the readings for this hour
        for (int r = 0; r < readings_per_hour; r++) {
            int idx = h * readings_per_hour + r;
            if (idx < num_readings) {
                sum += five_min_data[idx];
                count++;
            }
        }

        hourly_data[h] = (count > 0) ? (sum / count) : 0;
    }

    return available_hours;
}

void aggregate_to_24_hours(float* five_min_data, int num_readings,
                           float* hourly_data, bool* has_data)
{
    const int readings_per_hour = 12;  // 5-minute intervals
    const int total_hours = 24;

    // Initialize all hours as missing
    for (int h = 0; h < total_hours; h++) {
        hourly_data[h] = 0.0f;
        has_data[h] = false;
    }

    if (num_readings < readings_per_hour) {
        return;  // Not enough data for even one hour
    }

    // Calculate how many complete hours we have
    int available_hours = num_readings / readings_per_hour;
    if (available_hours > total_hours) {
        available_hours = total_hours;
    }

    // Aggregate available hours (oldest to newest)
    // Data in five_min_data is already oldest first
    for (int h = 0; h < available_hours; h++) {
        float sum = 0;
        int count = 0;

        // Average the readings for this hour
        for (int r = 0; r < readings_per_hour; r++) {
            int idx = h * readings_per_hour + r;
            if (idx < num_readings) {
                sum += five_min_data[idx];
                count++;
            }
        }

        if (count > 0) {
            // Place data in correct position (fill from the right/newest)
            // If we have 8 hours, they go in positions 16-23 (24-8=16)
            int pos = total_hours - available_hours + h;
            hourly_data[pos] = sum / count;
            has_data[pos] = true;
        }
    }
}
