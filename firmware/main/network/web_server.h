#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Web server for control panel
 * Provides:
 * - WiFi configuration
 * - Device name configuration
 * - OTA update
 */

/**
 * Initialize and start the web server
 * @param port HTTP server port (default: 80)
 */
esp_err_t web_server_start(uint16_t port);

/**
 * Stop the web server
 */
void web_server_stop(void);

/**
 * Push the now-playing state to open /ws/nowplaying pages. Call on any
 * source or play/pause change; safe from any task (not from an ISR).
 */
void web_server_notify_nowplaying(bool playing);

/** Session tag of /ws/nowplaying clients (log_stream skips them). */
extern const char web_server_nowplaying_tag;
