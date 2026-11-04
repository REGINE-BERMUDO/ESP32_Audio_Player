#include "sd_card.h"

#include "esp_vfs_fat.h"

#include "driver/sdmmc_host.h"

#include "esp_log.h"

#define MOUNT_POINT "/sdcard"
#define MAX_FREQ_KHZ 40000
#define MAX_FILES 3
#define INTERNAL_PULLUPS false

#define BUS_WIDTH 4

static const char *TAG = "SD-Card";

static sdmmc_card_t *s_card;

typedef struct {
    const char *mount_point;
    uint32_t max_freq_khz;
    int max_files;
    bool internal_pullups;
} sd_audio_config_t;

esp_err_t sd_card_init()
{
    sd_audio_config_t cfg = {
        .mount_point = MOUNT_POINT,
        .max_freq_khz = MAX_FREQ_KHZ,
        .max_files = MAX_FILES,
        .internal_pullups = INTERNAL_PULLUPS
    };
    if(cfg.mount_point[0] == '\0' ||
       cfg.max_freq_khz != MAX_FREQ_KHZ ||
       cfg.max_files != MAX_FILES ||
       cfg.internal_pullups != INTERNAL_PULLUPS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if(s_card != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = cfg.max_freq_khz;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = BUS_WIDTH;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = cfg.max_files,
        .allocation_unit_size = 0
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(cfg.mount_point, &host, &slot, &mount_cfg, &s_card);
    if(err != ESP_OK)
    {
        if(err == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Partition cannot be mounted");
        }
        else
        {
            ESP_LOGE(TAG, "SD-Card initialization failed (%s). "
                          "Check wiring and the 10k pull-ups on CMD/D0-D3",
                          esp_err_to_name(err)
            );
        }
        s_card = NULL;
        return err;
    }
}