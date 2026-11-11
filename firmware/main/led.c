#include "led.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "rtsp_events.h"
#include "settings.h"
#include "playback_control.h"

#if CONFIG_LED_STATUS_GPIO >= 0 || CONFIG_LED_ERROR_GPIO >= 0
#include "driver/ledc.h"
#endif

#include <math.h>

// Convert Kconfig boolean values to C macros
#ifdef CONFIG_LED_STATUS_INVERT
#define LED_STATUS_INVERT_VAL 1
#else
#define LED_STATUS_INVERT_VAL 0
#endif

#ifdef CONFIG_LED_ERROR_INVERT
#define LED_ERROR_INVERT_VAL 1
#else
#define LED_ERROR_INVERT_VAL 0
#endif

static const char *TAG = "led";

// Module-level brightness (0–255), shared across all LED types.
// Loaded from NVS in led_init(); updated by led_set_brightness().
static uint8_t s_brightness = CONFIG_LED_STATUS_BRIGHTNESS;

// ============================================================================
// Configuration helpers - map Kconfig to led_mode_t
// ============================================================================

static led_mode_t get_status_mode_playing(void) {
#if defined(CONFIG_LED_STATUS_PLAYING_VU)
  return LED_VU;
#elif defined(CONFIG_LED_STATUS_PLAYING_BLINK_FAST)
  return LED_BLINK_FAST;
#else
  return LED_STEADY;
#endif
}

static led_mode_t get_status_mode_paused(void) {
#if defined(CONFIG_LED_STATUS_PAUSED_OFF)
  return LED_OFF;
#elif defined(CONFIG_LED_STATUS_PAUSED_STEADY)
  return LED_STEADY;
#elif defined(CONFIG_LED_STATUS_PAUSED_BLINK_SLOW)
  return LED_BLINK_SLOW;
#else
  return LED_BLINK_MEDIUM;
#endif
}

static led_mode_t get_status_mode_standby(void) {
#if defined(CONFIG_LED_STATUS_STANDBY_OFF)
  return LED_OFF;
#elif defined(CONFIG_LED_STATUS_STANDBY_BLINK_MEDIUM)
  return LED_BLINK_MEDIUM;
#else
  return LED_BLINK_SLOW;
#endif
}

static led_mode_t get_rgb_mode_playing(void) {
#if defined(CONFIG_LED_RGB_PLAYING_OFF)
  return LED_OFF;
#elif defined(CONFIG_LED_RGB_PLAYING_STEADY)
  return LED_STEADY;
#else
  return LED_VU;
#endif
}

static led_mode_t get_rgb_mode_paused(void) {
#if defined(CONFIG_LED_RGB_PAUSED_OFF)
  return LED_OFF;
#else
  return LED_STEADY;
#endif
}

static led_mode_t get_rgb_mode_standby(void) {
#if defined(CONFIG_LED_RGB_STANDBY_STEADY)
  return LED_STEADY;
#else
  return LED_OFF;
#endif
}

// ============================================================================
// Status LED (single color via LEDC PWM)
// ============================================================================

#if CONFIG_LED_STATUS_GPIO >= 0

#define STATUS_LED_CHANNEL LEDC_CHANNEL_0
#define STATUS_LED_TIMER   LEDC_TIMER_0

// A pattern alternates on/off durations in ms. Blink modes repeat their
// pattern; one-shot events play once and then restore the mode underneath.
typedef struct {
  const uint16_t *ms;
  uint8_t len;
  bool start_on;
} led_pattern_t;

static const uint16_t P_SLOW[] = {100, 2500};
static const uint16_t P_MEDIUM[] = {500, 500};
static const uint16_t P_FAST[] = {250, 250};
static const uint16_t P_FLICKER[] = {60, 140};
static const uint16_t P_DOUBLE[] = {80, 150, 80, 1700};
static const uint16_t P_TRIPLE[] = {80, 120, 80, 120, 80, 1200};
static const uint16_t P_ACK[] = {120, 120, 120, 120};
static const uint16_t P_OK[] = {60, 60, 60, 60, 60, 60, 60, 600};
static const uint16_t P_FAIL[] = {80, 120, 80, 120, 80, 1200,
                                  80, 120, 80, 120, 80, 1200};

#define PATTERN(a, on) ((led_pattern_t){a, sizeof(a) / sizeof(a[0]), on})

static led_mode_t s_status_mode = LED_OFF;
static TimerHandle_t s_status_timer = NULL;
static bool s_status_on = false;
static uint8_t s_status_duty = CONFIG_LED_STATUS_BRIGHTNESS;
static led_pattern_t s_pattern;
static uint8_t s_step;
static bool s_oneshot = false;

static void status_led_set_duty(uint8_t duty) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, STATUS_LED_CHANNEL, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, STATUS_LED_CHANNEL);
  ESP_LOGV(TAG, "Status LED duty set to %d", duty);
}

static bool pattern_for_mode(led_mode_t mode, led_pattern_t *out) {
  switch (mode) {
  case LED_BLINK_SLOW:
    *out = PATTERN(P_SLOW, true);
    return true;
  case LED_BLINK_MEDIUM:
    *out = PATTERN(P_MEDIUM, true);
    return true;
  case LED_BLINK_FAST:
    *out = PATTERN(P_FAST, true);
    return true;
  case LED_FLICKER:
    *out = PATTERN(P_FLICKER, true);
    return true;
  case LED_DOUBLE:
    *out = PATTERN(P_DOUBLE, true);
    return true;
  case LED_TRIPLE:
    *out = PATTERN(P_TRIPLE, true);
    return true;
  default:
    return false;
  }
}

static void status_led_show_mode(led_mode_t mode);

// Runs on the timer service task, like every other status LED change.
static void status_timer_cb(TimerHandle_t xTimer) {
  (void)xTimer;
  if (s_step >= s_pattern.len) {
    if (s_oneshot) {
      s_oneshot = false;
      status_led_show_mode(s_status_mode);
      return;
    }
    s_step = 0;
  }
  s_status_on = ((s_step & 1) == 0) == s_pattern.start_on;
  status_led_set_duty(s_status_on ? s_status_duty : 0);
  TickType_t ticks = pdMS_TO_TICKS(s_pattern.ms[s_step++]);
  xTimerChangePeriod(s_status_timer, ticks ? ticks : 1, 0);
}

static void status_led_play(led_pattern_t pattern, bool oneshot) {
  s_pattern = pattern;
  s_step = 0;
  s_oneshot = oneshot;
  if (s_status_timer) {
    status_timer_cb(s_status_timer);
  }
}

static void status_led_show_mode(led_mode_t mode) {
  led_pattern_t pattern;
  if (pattern_for_mode(mode, &pattern)) {
    status_led_play(pattern, false);
    return;
  }
  if (s_status_timer) {
    xTimerStop(s_status_timer, 0);
  }
  s_status_on = mode == LED_STEADY;
  status_led_set_duty(s_status_on ? s_status_duty : 0);
}

static void status_led_init(void) {
  s_status_duty = s_brightness;

  ledc_timer_config_t timer_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_num = STATUS_LED_TIMER,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .freq_hz = 1000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  if (ledc_timer_config(&timer_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Status LED timer init failed");
    return;
  }

  ledc_channel_config_t ch_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = STATUS_LED_CHANNEL,
      .timer_sel = STATUS_LED_TIMER,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = CONFIG_LED_STATUS_GPIO,
      .duty = 0,
      .hpoint = 0,
      .flags = {.output_invert = LED_STATUS_INVERT_VAL},
  };
  if (ledc_channel_config(&ch_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Status LED channel init failed");
    return;
  }

  // Explicitly apply initial duty (off)
  ledc_set_duty(LEDC_LOW_SPEED_MODE, STATUS_LED_CHANNEL, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, STATUS_LED_CHANNEL);

  s_status_timer = xTimerCreate("status_led", pdMS_TO_TICKS(500), pdFALSE, NULL,
                                status_timer_cb);
  if (s_status_timer == NULL) {
    ESP_LOGE(TAG, "Failed to create status LED timer");
    return;
  }

  ESP_LOGI(TAG, "Status LED initialized on GPIO %d", CONFIG_LED_STATUS_GPIO);
}

static void status_led_set_mode(led_mode_t mode) {
  if (mode == s_status_mode) {
    return;
  }
  ESP_LOGD(TAG, "Status LED mode change: %d -> %d", s_status_mode, mode);
  s_status_mode = mode;
  if (!s_oneshot) {
    status_led_show_mode(mode);
  }
}

static void status_led_oneshot(led_event_t event) {
  switch (event) {
  case LED_EVENT_ACK:
    status_led_play(PATTERN(P_ACK, false), true);
    break;
  case LED_EVENT_OK:
    status_led_play(PATTERN(P_OK, true), true);
    break;
  case LED_EVENT_FAIL:
    status_led_play(PATTERN(P_FAIL, true), true);
    break;
  }
}

static void status_led_set_vu(float norm) {
  if (s_status_mode != LED_VU) {
    return;
  }
  uint8_t duty = (uint8_t)(norm * (float)s_status_duty);
  status_led_set_duty(duty);
}

#else
static void status_led_init(void) {
}
static void status_led_set_mode(led_mode_t mode) {
  (void)mode;
}
static void status_led_oneshot(led_event_t event) {
  (void)event;
}
static void status_led_set_vu(float norm) {
  (void)norm;
}
#endif

// ============================================================================
// Error LED (simple on/off via LEDC)
// ============================================================================

#if CONFIG_LED_ERROR_GPIO >= 0

#define ERROR_LED_CHANNEL LEDC_CHANNEL_1
#define ERROR_LED_TIMER   LEDC_TIMER_1

static void error_led_init(void) {
  ledc_timer_config_t timer_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_num = ERROR_LED_TIMER,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .freq_hz = 1000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  if (ledc_timer_config(&timer_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Error LED timer init failed");
    return;
  }

  ledc_channel_config_t ch_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = ERROR_LED_CHANNEL,
      .timer_sel = ERROR_LED_TIMER,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = CONFIG_LED_ERROR_GPIO,
      .duty = 0,
      .hpoint = 0,
      .flags = {.output_invert = LED_ERROR_INVERT_VAL},
  };
  if (ledc_channel_config(&ch_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Error LED channel init failed");
    return;
  }

  // Explicitly apply initial duty (off)
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ERROR_LED_CHANNEL, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ERROR_LED_CHANNEL);

  ESP_LOGI(TAG, "Error LED initialized on GPIO %d", CONFIG_LED_ERROR_GPIO);
}

static void error_led_set(bool on) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ERROR_LED_CHANNEL, on ? s_brightness : 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ERROR_LED_CHANNEL);
}

#else
static void error_led_init(void) {
}
static void error_led_set(bool on) {
  (void)on;
}
#endif

// ============================================================================
// RGB LED (WS2812 via led_strip)
// ============================================================================

#if CONFIG_LED_RGB_GPIO >= 0

#include "led_strip.h"

static led_strip_handle_t s_rgb_strip = NULL;
static led_mode_t s_rgb_mode = LED_OFF;

static void rgb_led_init(void) {
  led_strip_config_t strip_cfg = {
      .strip_gpio_num = CONFIG_LED_RGB_GPIO,
      .max_leds = 1,
      .led_model = LED_MODEL_WS2812,
      .flags.invert_out = false,
  };
  led_strip_rmt_config_t rmt_cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10 * 1000 * 1000,
      .flags.with_dma = false,
  };

  if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_rgb_strip) != ESP_OK) {
    ESP_LOGE(TAG, "RGB LED init failed");
    s_rgb_strip = NULL;
    return;
  }

  led_strip_clear(s_rgb_strip);
  ESP_LOGI(TAG, "RGB LED initialized on GPIO %d", CONFIG_LED_RGB_GPIO);
}

static void rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  if (!s_rgb_strip) {
    return;
  }
  led_strip_set_pixel(s_rgb_strip, 0, r, g, b);
  led_strip_refresh(s_rgb_strip);
}

static void rgb_led_clear(void) {
  if (!s_rgb_strip) {
    return;
  }
  led_strip_clear(s_rgb_strip);
  led_strip_refresh(s_rgb_strip);
}

static void rgb_led_set_mode(led_mode_t mode) {
  s_rgb_mode = mode;

  switch (mode) {
  case LED_OFF:
    rgb_led_clear();
    break;
  case LED_STEADY: {
    // Color depends on current state - handled by on_rtsp_event
    break;
  }
  case LED_VU:
    // Handled by led_audio_feed
    break;
  default:
    break;
  }
}

static void rgb_led_set_vu(float norm, float bass_ratio) {
  if (s_rgb_mode != LED_VU || !s_rgb_strip) {
    return;
  }

  if (norm <= 0.0f || s_brightness == 0) {
    rgb_led_clear();
    return;
  }

  uint8_t val = (uint8_t)(norm * (float)s_brightness);
  if (val < 1) {
    val = 1;
  }

  // Map to HSV hue: 170 (blue, quiet) -> 85 (green, medium) -> 0 (red, loud)
  uint16_t hue = (uint16_t)(170.0f * (1.0f - norm));

  // Shift towards purple/magenta when bassy
  if (bass_ratio > 0.3f) {
    hue = (uint16_t)(hue + (uint16_t)(bass_ratio * 60.0f));
    if (hue > 255) {
      hue = 255;
    }
  }

  // High saturation, reduce slightly at very high energy for warm white
  uint8_t sat = 255;
  if (norm > 0.85f) {
    sat = (uint8_t)(255 - (uint8_t)((norm - 0.85f) / 0.15f * 80.0f));
  }

  led_strip_set_pixel_hsv(s_rgb_strip, 0, hue, sat, val);
  led_strip_refresh(s_rgb_strip);
}

#else
static void rgb_led_init(void) {
}
static void rgb_led_set_mode(led_mode_t mode) {
  (void)mode;
}
static void rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  (void)r;
  (void)g;
  (void)b;
}
static void rgb_led_clear(void) {
}
static void rgb_led_set_vu(float norm, float bass_ratio) {
  (void)norm;
  (void)bass_ratio;
}
#endif

// ============================================================================
// RTSP Event Handler
// ============================================================================

typedef enum {
  STATE_STANDBY,
  STATE_PAUSED,
  STATE_PLAYING,
  STATE_ERROR,
} led_state_t;

static led_state_t s_prev_state = STATE_STANDBY;
static led_state_t s_current_state = STATE_STANDBY;
static led_network_t s_network = LED_NET_CONNECTING;
static bool s_pairing = false;
static TimerHandle_t s_pairing_timer = NULL;

#define PAIRING_TIMEOUT_MS 60000

// Without a phone the LED tells the network story; pairing beats both.
static led_mode_t status_mode_for(led_state_t state) {
  if (state == STATE_ERROR) {
    return LED_TRIPLE;
  }
  if (s_pairing && state != STATE_PLAYING) {
    return LED_BLINK_FAST;
  }
  switch (state) {
  case STATE_PLAYING:
    return get_status_mode_playing();
  case STATE_PAUSED:
    return get_status_mode_paused();
  default:
    break;
  }
  switch (s_network) {
  case LED_NET_CONNECTING:
    return LED_FLICKER;
  case LED_NET_SETUP:
    return LED_DOUBLE;
  default:
    return get_status_mode_standby();
  }
}

static uint8_t scale_bright(uint8_t v) {
  return (uint8_t)((uint16_t)v * s_brightness / 255);
}

static void render_state(led_state_t state) {
  switch (state) {
  case STATE_PLAYING:
    rgb_led_set_mode(get_rgb_mode_playing());
    error_led_set(false);
    break;

  case STATE_PAUSED:
    rgb_led_set_mode(get_rgb_mode_paused());
    if (get_rgb_mode_paused() == LED_STEADY) {
#ifdef CONFIG_LED_RGB_COLOR_PAUSED
      uint32_t c = CONFIG_LED_RGB_COLOR_PAUSED;
      rgb_led_set_color(scale_bright((c >> 16) & 0xFF),
                        scale_bright((c >> 8) & 0xFF), scale_bright(c & 0xFF));
#else
      rgb_led_set_color(0, 0, scale_bright(0x33));
#endif
    }
    error_led_set(false);
    break;

  case STATE_STANDBY:
    rgb_led_set_mode(get_rgb_mode_standby());
    if (get_rgb_mode_standby() == LED_STEADY) {
#ifdef CONFIG_LED_RGB_COLOR_STANDBY
      uint32_t c = CONFIG_LED_RGB_COLOR_STANDBY;
      rgb_led_set_color(scale_bright((c >> 16) & 0xFF),
                        scale_bright((c >> 8) & 0xFF), scale_bright(c & 0xFF));
#else
      rgb_led_set_color(0, scale_bright(0x11), 0);
#endif
    }
    error_led_set(false);
    break;

  case STATE_ERROR:
    rgb_led_set_color(scale_bright(0x80), 0, 0);
    error_led_set(true);
    break;
  }
#if CONFIG_LED_ERROR_GPIO >= 0
  // Dedicated error LED - turn off status to avoid mixed signals
  status_led_set_mode(state == STATE_ERROR ? LED_OFF : status_mode_for(state));
#else
  status_led_set_mode(status_mode_for(state));
#endif
}

// Every LED change runs on the timer service task, so the Wi-Fi, Bluetooth,
// button and audio tasks never race on the pattern state.
static void pended_render(void *arg, uint32_t unused) {
  (void)arg;
  (void)unused;
  render_state(s_current_state);
}

static void pended_event(void *arg, uint32_t event) {
  (void)arg;
  status_led_oneshot((led_event_t)event);
}

static void request_render(void) {
  if (xTimerPendFunctionCall(pended_render, NULL, 0, 0) != pdPASS) {
    ESP_LOGW(TAG, "LED update dropped");
  }
}

static void pairing_timeout_cb(TimerHandle_t timer) {
  (void)timer;
  s_pairing = false;
  render_state(s_current_state);
}

void led_set_network(led_network_t state) {
  if (state == s_network) {
    return;
  }
  ESP_LOGI(TAG, "LED network state %d -> %d", s_network, state);
  s_network = state;
  request_render();
}

void led_set_pairing(bool pairing) {
  if (pairing == s_pairing) {
    return;
  }
  s_pairing = pairing;
  if (s_pairing_timer) {
    if (pairing) {
      xTimerReset(s_pairing_timer, 0);
    } else {
      xTimerStop(s_pairing_timer, 0);
    }
  }
  request_render();
}

void led_event(led_event_t event) {
  xTimerPendFunctionCall(pended_event, NULL, (uint32_t)event, 0);
}

static void apply_state(led_state_t state) {
  s_prev_state = s_current_state;
  s_current_state = state;

  ESP_LOGI(TAG, "LED state change: %d -> %d", s_prev_state, state);
  request_render();
}

void led_set_playback_status(bool connected, bool playing) {
  apply_state(!connected ? STATE_STANDBY : playing ? STATE_PLAYING : STATE_PAUSED);
}

static void on_rtsp_event(rtsp_event_t event, const rtsp_event_data_t *data,
                          void *user_data) {
  ESP_LOGD(TAG, "RTSP event: %d", event);
  if ((playback_control_get_source() == PLAYBACK_SOURCE_SPOTIFY ||
       playback_control_get_source() == PLAYBACK_SOURCE_BLUETOOTH) &&
      event != RTSP_EVENT_PLAYING) {
    return;
  }
  switch (event) {
  case RTSP_EVENT_CLIENT_CONNECTED:
    apply_state(STATE_PAUSED);
    break;
  case RTSP_EVENT_PLAYING:
    apply_state(STATE_PLAYING);
    break;
  case RTSP_EVENT_PAUSED:
    apply_state(STATE_PAUSED);
    break;
  case RTSP_EVENT_DISCONNECTED:
    apply_state(STATE_STANDBY);
    break;
  case RTSP_EVENT_METADATA:
    break;
  }
}

// ============================================================================
// Audio VU Processing
// ============================================================================

#define SILENCE_THRESH     200
#define UPDATE_INTERVAL_US (1000000 / 30) // ~30 Hz

static int64_t s_last_update_us = 0;

void led_audio_feed(const int16_t *pcm, size_t stereo_samples) {
  if (stereo_samples == 0 || s_current_state != STATE_PLAYING) {
    return;
  }

  // Rate limit to ~30 Hz
  int64_t now = esp_timer_get_time();
  if (now - s_last_update_us < UPDATE_INTERVAL_US) {
    return;
  }
  s_last_update_us = now;

  size_t total = stereo_samples * 2;

  // Compute RMS energy
  uint64_t sum_sq = 0;
  for (size_t i = 0; i < total; i++) {
    int32_t s = pcm[i];
    sum_sq += (uint64_t)(s * s);
  }
  float rms = sqrtf((float)sum_sq / (float)total);

  // Simple bass energy estimate
  uint64_t diff_sum = 0;
  for (size_t i = 2; i < total; i += 2) {
    int32_t d = (int32_t)pcm[i] - (int32_t)pcm[i - 2];
    diff_sum += (uint64_t)(d < 0 ? -d : d);
  }
  float high_energy = (float)diff_sum / ((float)total / 2.0f);

  float bass_ratio = 0.0f;
  if (rms > SILENCE_THRESH) {
    bass_ratio = 1.0f - (high_energy / (rms * 2.0f + 1.0f));
    if (bass_ratio < 0.0f) {
      bass_ratio = 0.0f;
    }
    if (bass_ratio > 1.0f) {
      bass_ratio = 1.0f;
    }
  }

  float norm = 0.0f;
  if (rms >= SILENCE_THRESH) {
    norm = (rms - SILENCE_THRESH) / (16000.0f - SILENCE_THRESH);
    if (norm > 1.0f) {
      norm = 1.0f;
    }
  }

  status_led_set_vu(norm);
  rgb_led_set_vu(norm, bass_ratio);
}

// ============================================================================
// Public API
// ============================================================================

void led_init(void) {
  ESP_LOGI(TAG, "Initializing LED subsystem");
  ESP_LOGI(TAG, "  Status LED GPIO: %d", CONFIG_LED_STATUS_GPIO);
  ESP_LOGI(TAG, "  Error LED GPIO: %d", CONFIG_LED_ERROR_GPIO);
  ESP_LOGI(TAG, "  RGB LED GPIO: %d", CONFIG_LED_RGB_GPIO);
  ESP_LOGI(TAG, "  LED brightness: %d", CONFIG_LED_STATUS_BRIGHTNESS);

  uint8_t saved;
  if (settings_get_led_brightness(&saved) == ESP_OK) {
    s_brightness = saved;
  }

  status_led_init();
  error_led_init();
  rgb_led_init();

  s_pairing_timer = xTimerCreate("led_pair", pdMS_TO_TICKS(PAIRING_TIMEOUT_MS),
                                 pdFALSE, NULL, pairing_timeout_cb);
  rtsp_events_register(on_rtsp_event, NULL);

  // Start in standby
  ESP_LOGI(TAG, "Starting in STANDBY state");
  apply_state(STATE_STANDBY);

  ESP_LOGI(TAG, "LED subsystem initialized");
}

void led_set_error(bool error) {
  if (error) {
    if (s_current_state != STATE_ERROR) {
      apply_state(STATE_ERROR);
    } else {
      request_render();
    }
  } else if (s_current_state == STATE_ERROR) {
    apply_state(s_prev_state);
  }
}

esp_err_t led_set_brightness(uint8_t brightness) {
  esp_err_t err = settings_set_led_brightness(brightness);
  if (err != ESP_OK) {
    return err;
  }

  s_brightness = brightness;
#if CONFIG_LED_STATUS_GPIO >= 0
  s_status_duty = brightness;
  if (s_status_mode == LED_STEADY ||
      (s_status_mode >= LED_BLINK_SLOW && s_status_on)) {
    status_led_set_duty(s_status_duty);
  }
#endif
  // Re-render without changing previous/current state history.
  request_render();
  return ESP_OK;
}

uint8_t led_get_brightness(void) {
  return s_brightness;
}
