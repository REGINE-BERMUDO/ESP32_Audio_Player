#include "sd_card.h"
#include "mp3_player.h"

#include "esp_log.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const char *TAG = "Main";

void app_main() {
    esp_err_t err = sd_card_init();
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "SD-Card Initialization Failed");
        return;
    }

    sd_card_test_file();

    ESP_ERROR_CHECK(mp3_player_init(GPIO_NUM_27, GPIO_NUM_25, GPIO_NUM_26));  // BCLK, WS, DOUT
    mp3_player_set_volume(60);
    mp3_player_start("/sdcard/Music/Die With A Smile.mp3");
    
    mp3_info_t info;
    int64_t  wall_prev  = 0;
    uint32_t audio_prev = 0;

    while(mp3_player_is_playing())
    {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if(mp3_player_get_info(&info))
        {
            int64_t wall = esp_timer_get_time() / 1000;
            if(wall_prev != 0)
            {
                ESP_LOGI(TAG, "wall +%lld ms, audio +%u ms (%u Hz, %d kbps)",
                         (long long)(wall - wall_prev),
                         (unsigned)(info.elapsed_ms - audio_prev),
                         (unsigned)info.sample_rate, info.bitrate_kbps);
            }
            wall_prev  = wall;
            audio_prev = info.elapsed_ms;
        }
    }
    ESP_LOGI(TAG, "playback finished");
}