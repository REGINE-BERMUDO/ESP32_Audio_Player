#include "sd_card.h"
#include "mp3_player.h"

#include "esp_log.h"
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
    mp3_player_set_volume(100);

    mp3_player_start("/sdcard/Music/Lonely Is The Night.mp3");
    if(mp3_player_is_playing()) vTaskDelay(pdMS_TO_TICKS(10000));
    mp3_player_stop();
    while(mp3_player_is_playing()) vTaskDelay(pdMS_TO_TICKS(100));

    mp3_player_start("/sdcard/Music/Die With A Smile.mp3");
    if(mp3_player_is_playing()) vTaskDelay(pdMS_TO_TICKS(10000));
    mp3_player_stop();
    while(mp3_player_is_playing()) vTaskDelay(pdMS_TO_TICKS(100));
    
   ESP_LOGI(TAG, "Music Finished"); 
}
