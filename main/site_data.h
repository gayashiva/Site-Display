/**
 * @file site_data.h
 * @brief Site data structures and API declarations
 */

#ifndef SITE_DATA_H
#define SITE_DATA_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_READINGS 288  // 24 hours at 5-minute intervals
#define MAX_HOURLY_READINGS 24  // 24 hours
#define MAX_SITE_NAME_LEN 32
#define MAX_TIMESTAMP_LEN 32

// Site reading structure
typedef struct {
    int32_t dt;                         // Unix timestamp
    char timestamp[MAX_TIMESTAMP_LEN];  // Human readable timestamp
    float temperature;                  // Ambient temperature
    float water_temp;                   // Water temperature
    float pressure;                     // Pressure reading
    float voltage;                      // Battery/supply voltage
    int32_t counter;                    // Reading counter
} site_reading_t;

// Site metadata structure
typedef struct {
    char site_name[MAX_SITE_NAME_LEN];
    char site_type[16];                 // "air" or "drip"
    bool active;
    int32_t timezone_offset;
    int32_t query_time;
} site_info_t;

// Global site data
extern site_info_t g_site_info;
extern site_reading_t g_current_reading;
extern site_reading_t g_site_readings[MAX_READINGS];
extern int g_num_readings;

// Site list
extern const char* g_site_list[];
extern const int g_num_sites;
extern int g_current_site_index;
extern bool g_data_loaded;

// Current site name (points to g_site_list entry)
extern const char* g_site_name;

/**
 * @brief Parse JSON response from Site Data API
 * @param json_str JSON string to parse
 * @param print If true, print parsed data to console
 * @return true on success, false on parse error
 */
bool parse_site_data(const char* json_str, bool print);

/**
 * @brief Convert Unix timestamp to formatted string
 * @param unix_time Unix timestamp
 * @param output Buffer to store formatted string
 * @param output_len Length of output buffer
 */
void convert_unix_time(int32_t unix_time, char* output, size_t output_len);

/**
 * @brief Calculate Julian date
 * @param d Day
 * @param m Month
 * @param y Year
 * @return Julian date
 */
int julian_date(int d, int m, int y);

/**
 * @brief Calculate normalized moon phase
 * @param d Day
 * @param m Month
 * @param y Year
 * @return Moon phase (0.0 to 1.0)
 */
double normalized_moon_phase(int d, int m, int y);

/**
 * @brief Sum of precipitation values
 * @param data_array Array of precipitation values
 * @param readings Number of readings
 * @return Sum of values
 */
float sum_of_precip(float* data_array, int readings);

/**
 * @brief Aggregate 5-minute readings into hourly averages
 * @param five_min_data Array of 5-minute readings (288 readings for 24hrs)
 * @param num_readings Number of 5-minute readings
 * @param hourly_data Output array for 24 hourly averages
 * @return Number of hourly values created
 */
int aggregate_to_hourly(float* five_min_data, int num_readings, float* hourly_data);

/**
 * @brief Aggregate to full 24 hours with NaN for missing hours
 * @param five_min_data Array of 5-minute readings
 * @param num_readings Number of 5-minute readings
 * @param hourly_data Output array for 24 hourly values (oldest to newest)
 * @param has_data Output array indicating which hours have data
 */
void aggregate_to_24_hours(float* five_min_data, int num_readings,
                           float* hourly_data, bool* has_data);

#endif // SITE_DATA_H
