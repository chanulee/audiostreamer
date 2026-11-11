#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* A2DP/SBC receiver. Wi-Fi services stay available while Bluetooth is connected. */
esp_err_t bt_a2dp_sink_init(const char *device_name);
bool bt_a2dp_sink_is_connected(void);
void bt_a2dp_sink_set_discoverable(bool discoverable);
/* Disconnect the current phone (if any) and become discoverable. */
void bt_a2dp_sink_pairing_mode(void);
size_t bt_a2dp_sink_read(int16_t *pcm, size_t max_frames);
uint32_t bt_a2dp_sink_sample_rate(void);
int32_t bt_a2dp_sink_volume_q15(void);
int bt_a2dp_sink_volume_percent(void);
void bt_a2dp_sink_yield(void);
/* Buffer health for the status API: drift between the phone's clock and the
 * I2S clock shows up as a FIFO level that creeps, then drops or underruns. */
typedef struct {
  uint32_t buffer_ms;   // PCM queued in the FIFO now
  uint32_t latency_ms;  // reported to the phone (prefill + DMA + one frame)
  uint32_t underruns;   // FIFO ran dry while playing
  uint32_t dropped;     // callbacks dropped because the FIFO was full
} bt_a2dp_stats_t;
void bt_a2dp_sink_stats(bt_a2dp_stats_t *stats);
void bt_a2dp_send_playpause(void);
void bt_a2dp_send_next(void);
void bt_a2dp_send_prev(void);
void bt_a2dp_send_volume_up(void);
void bt_a2dp_send_volume_down(void);
