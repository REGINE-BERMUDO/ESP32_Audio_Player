#include "sd_card.h"
#include "mp3_player.h"

#include "esp_log.h"

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
}
