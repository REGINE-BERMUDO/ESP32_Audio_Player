#include "sd_card.h"

#include "esp_vfs_fat.h"

// SD-Card SPI host
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

#include "esp_log.h"

#include "sdmmc_cmd.h"

#include "dirent.h"
#include "sys/stat.h"

#define MOUNT_POINT     "/sdcard"
#define MUSIC_DIR       "/sdcard/Music/"
#define MAX_FREQ_KHZ    16000
#define MAX_FILES       3

// SPI configuration for SD-Card
#define SD_SPI_HOST     SPI3_HOST
#define PIN_NUM_MOSI    23
#define PIN_NUM_MISO    19
#define PIN_NUM_CLK     18
#define PIN_NUM_CS      5

static const char       *TAG = "SD-Card";

static sdmmc_card_t     *s_card;
static char             s_mount_point[16];

typedef struct {
    const char *mount_point;
    uint32_t max_freq_khz;
    int max_files;
} sd_audio_config_t;

static esp_err_t sd_spi_bus_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    return spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
}

esp_err_t sd_card_init(void)
{
    sd_audio_config_t cfg = {
        .mount_point = MOUNT_POINT,
        .max_freq_khz = MAX_FREQ_KHZ,
        .max_files = MAX_FILES
    };
    if(cfg.mount_point[0] == '\0' ||
       cfg.max_freq_khz != MAX_FREQ_KHZ ||
       cfg.max_files != MAX_FILES)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if(s_card != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sd_spi_bus_init();
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "SD-Card initialization failed (%s). "
                       "Check SPI wiring, CS pin and card power",
                esp_err_to_name(err));
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = cfg.max_freq_khz;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_NUM_CS;
    slot.host_id = SD_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = cfg.max_files,
        .allocation_unit_size = 0
    };

    err = esp_vfs_fat_sdspi_mount(cfg.mount_point, &host, &slot, &mount_cfg, &s_card);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Mount failed (%s)", esp_err_to_name(err));
        s_card = NULL;
        spi_bus_free(SD_SPI_HOST);
        return err;
    }

    strlcpy(s_mount_point, cfg.mount_point, sizeof(s_mount_point));
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

// List music files from sd-card
esp_err_t sd_card_test_file(void)
{
    if(s_card == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    DIR *d = opendir(MUSIC_DIR);
    if(d == NULL)
    {
        ESP_LOGE(TAG, "Cannot open %s (folder missing or name mismatch)", MUSIC_DIR);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Files in %s:", MUSIC_DIR);

    char path[300];
    unsigned count = 0;
    struct dirent *e;
    while((e = readdir(d)) != NULL)
    {
        if(strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, e->d_name);

        struct stat st;
        if(stat(path, &st) == 0)
        {
            ESP_LOGI(TAG, "\t%s (%lu bytes)", e->d_name, (unsigned long)st.st_size);
        }
        else
        {
            ESP_LOGI(TAG, "\t%s", e->d_name);
        }
        count++;
    }
    closedir(d);

    ESP_LOGI(TAG, "%u item(s) found", count);
    return ESP_OK;
}