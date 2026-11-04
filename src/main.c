#include "sd_card.h"

#include "esp_log.h"

const char *TAG = "Main";

void app_main() {
    esp_err_t err = sd_card_init();
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "SD-Card Initialization Failed");
        return;
    }
}