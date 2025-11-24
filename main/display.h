/**
 * @file display.h
 * @brief High-level display functions for site data using bb_epaper
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>

/**
 * @brief Initialize the display subsystem
 */
void display_init(void);

/**
 * @brief Display site data on e-paper
 */
void display_site_data(void);

/**
 * @brief Display "no data" placeholder screen
 */
void display_no_data(void);

/**
 * @brief Display WiFi error screen
 */
void display_wifi_error(void);

/**
 * @brief Power off display for deep sleep
 */
void display_power_off(void);

/**
 * @brief Draw a graph on the display
 * @param x_pos X position
 * @param y_pos Y position
 * @param width Graph width
 * @param height Graph height
 * @param y_min Minimum Y value
 * @param y_max Maximum Y value
 * @param title Graph title
 * @param data Data array
 * @param readings Number of data points
 * @param auto_scale Enable auto-scaling
 * @param bar_chart Draw as bar chart
 * @param has_data Array indicating which readings have valid data
 */
void display_draw_graph(int x_pos, int y_pos, int width, int height,
                        float y_min, float y_max, const char* title,
                        float* data, int readings,
                        bool auto_scale, bool bar_chart, bool* has_data);

#endif // DISPLAY_H
