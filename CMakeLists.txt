# ESP-IDF Site Display Project for ESP32-S3
cmake_minimum_required(VERSION 3.16)

# Set target to ESP32-S3
set(IDF_TARGET esp32s3)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(site-display)
