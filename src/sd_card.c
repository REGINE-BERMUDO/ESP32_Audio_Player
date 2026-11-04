#include "sd_card.h"

#include "esp_vfs_fat.h"

#define MOUNT_POINT "/sdcard"
#define MAX_FREQ_KHZ 40000
#define MAX_FILES 3
#define INTERNAL_PULLUPS false

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

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = cfg.max_files,
        .allocation_unit_size = 0
    };

    esp_err_t esp_vfs_fat_sdmmc_mount(, , , &mount_cfg, );
}