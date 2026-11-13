#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    uint32_t sample_rate;   /* Hz */
    int      channels;
    int      bitrate_kbps;
    uint32_t elapsed_ms;
} mp3_info_t;

/* Sets up the I2S TX channel (Philips format, 16-bit stereo) for an external I2S DAC/amp. Call once. */
esp_err_t mp3_player_init(gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout);

/* Starts playing a file from the already-mounted SD card. Returns immediately; playback runs in
 * two background tasks (SD reader on core 0, decoder + I2S on core 1). */
esp_err_t mp3_player_start(const char *path);

void mp3_player_stop();
bool mp3_player_is_playing();
void mp3_player_set_volume(int percent);   /* 0..100 */

/* Non-blocking snapshot for a display task. Returns false until the first frame has been decoded. */
bool mp3_player_get_info(mp3_info_t *out);

#endif