/**
 * @file http_client.h
 * @brief HTTPS client for Site Data API
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Fetch site data from API
 * @param site_name Site name to query
 * @param count Number of historical readings to fetch
 * @param print If true, print response info
 * @return true on success, false on failure
 */
bool fetch_site_data(const char* site_name, int count, bool print);

#endif // HTTP_CLIENT_H
