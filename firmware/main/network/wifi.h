#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>

/**
 * Initialize WiFi in both AP and STA modes
 * @param ap_ssid AP SSID (if NULL, uses default)
 * @param ap_password AP password (if NULL, uses default or open)
 */
void wifi_init_apsta(const char *ap_ssid, const char *ap_password);

/**
 * Block until WiFi is connected and has an IP address
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return true if connected, false if timeout
 */
bool wifi_wait_connected(uint32_t timeout_ms);

/**
 * Get the device MAC address as a string (XX:XX:XX:XX:XX:XX)
 */
void wifi_get_mac_str(char *mac_str, size_t len);

/**
 * Check if WiFi STA is connected
 */
bool wifi_is_connected(void);

/**
 * Get current IP address as string
 * @param ip_str Output buffer
 * @param len Buffer size
 * @return ESP_OK on success
 */
esp_err_t wifi_get_ip_str(char *ip_str, size_t len);

/**
 * Scan for available WiFi networks
 * @param ap_list Output array of AP info (caller must free)
 * @param ap_count Output: number of APs found
 * @return ESP_OK on success
 */
esp_err_t wifi_scan(wifi_ap_record_t **ap_list, uint16_t *ap_count);

/**
 * Disconnect and stop WiFi
 */
void wifi_stop(void);

/**
 * Set the DHCP hostname from the given device name.
 * Sanitizes to a valid DNS label (lowercase, hyphens for spaces/symbols).
 * Takes effect on the next DHCP transaction.
 */
void wifi_set_hostname(const char *device_name);

typedef enum {
  WIFI_SETUP_IDLE,
  WIFI_SETUP_TESTING,   // joining the new network, setup AP still up
  WIFI_SETUP_CONNECTED, // joined and saved; setup AP closes shortly
  WIFI_SETUP_FAILED,    // not saved; previous settings restored
} wifi_setup_state_t;

typedef struct {
  wifi_setup_state_t state;
  char ssid[33];
  char ip[16];
  int reason; // last wifi_err_reason_t from the attempt, 0 if none
} wifi_setup_status_t;

/**
 * Try new credentials without rebooting. The setup AP stays up during the
 * attempt; credentials are saved only once the station gets an IP.
 * @return ESP_ERR_INVALID_STATE while another attempt is running
 */
esp_err_t wifi_setup_start(const char *ssid, const char *password);

/** Progress of the last wifi_setup_start(). */
void wifi_setup_get_status(wifi_setup_status_t *status);
