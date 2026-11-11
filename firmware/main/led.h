#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  LED_OFF,
  LED_STEADY,
  LED_BLINK_SLOW,   // 100ms on, 2500ms off (standby)
  LED_BLINK_MEDIUM, // 500ms on/off (paused)
  LED_BLINK_FAST,   // 250ms on/off (Bluetooth pairing)
  LED_FLICKER,      // 60ms on, 140ms off (booting, joining Wi-Fi)
  LED_DOUBLE,       // two blinks every 2s (setup AP open, no Wi-Fi)
  LED_TRIPLE,       // three blinks every 1.5s (error)
  LED_VU,           // Audio visualization
} led_mode_t;

/** Network state shown while no phone owns the output. */
typedef enum {
  LED_NET_CONNECTING, // booting or joining Wi-Fi
  LED_NET_SETUP,      // LOVELACE-Setup is the only way in
  LED_NET_READY,      // on the home network
} led_network_t;

/** One-shot patterns played over the current state. */
typedef enum {
  LED_EVENT_ACK,  // button press: two dark gaps
  LED_EVENT_OK,   // Wi-Fi joined, phone connected: four quick blinks
  LED_EVENT_FAIL, // Wi-Fi test failed: error pattern for ~3s
} led_event_t;

/**
 * Initialize LED subsystem and register for RTSP events.
 */
void led_init(void);

/** Update playback indication without emitting an AirPlay protocol event. */
void led_set_playback_status(bool connected, bool playing);

/**
 * Feed audio samples for VU meter mode.
 * Call this from the audio path when playing.
 */
void led_audio_feed(const int16_t *pcm, size_t stereo_samples);

/** Update the network state (Wi-Fi task, any context but ISR). */
void led_set_network(led_network_t state);

/** Bluetooth pairing indication; clears itself after 60 s. */
void led_set_pairing(bool pairing);

/** Play a one-shot pattern, then return to the current state. */
void led_event(led_event_t event);

/**
 * Set error state (e.g., speaker fault, decode failure).
 * Clears automatically on next playback state change.
 */
void led_set_error(bool error);

/**
 * Set LED brightness (0–255). Persists to NVS and takes effect immediately.
 */
esp_err_t led_set_brightness(uint8_t brightness);

/**
 * Get current LED brightness (0–255).
 */
uint8_t led_get_brightness(void);
