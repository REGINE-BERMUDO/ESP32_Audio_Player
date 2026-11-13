#include "mp3_player.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3     /* drop layer 1/2 code */
#define MINIMP3_NO_SIMD      /* no SIMD paths on Xtensa */
#include "minimp3.h"

static const char *TAG = "mp3_player";

/* ---------------------------- tunables ---------------------------- */
#define READ_AHEAD_BYTES     (64 * 1024)   /* compressed data buffered between SD and decoder */
#define READ_CHUNK_BYTES     4096          /* one SD read; multiple of the 512 B sector size */
#define MP3_IN_BYTES         8192          /* decoder input window */
#define MAX_PCM_FRAMES       1152          /* samples per channel in one MPEG-1 layer 3 frame */

#define DECODER_STACK_BYTES  (24 * 1024)   /* minimp3 keeps large scratch data on the stack */
#define DECODER_PRIO         6
#define DECODER_CORE         1
#define READER_STACK_BYTES   4096
#define READER_PRIO          5
#define READER_CORE          0

#define DMA_DESC_NUM         8             /* 8 x 480 frames = ~87 ms of audio at 44.1 kHz */
#define DMA_FRAME_NUM        480

#define INFO_PERIOD_FRAMES   20            /* publish track info about twice per second */
#define LOG_DECODE_TIMING    1             /* log avg/max decode time vs. the per-frame budget */
#define LOG_TIMING_FRAMES    200
/* ------------------------------------------------------------------ */

typedef struct {
    int                  fd;
    StreamBufferHandle_t sb;
    SemaphoreHandle_t    reader_done;
    uint8_t             *chunk;       /* DMA-capable bounce buffer for SD reads */
    volatile bool        eof;         /* reader hit end of file (set after the last byte is queued) */
    volatile bool        stop;        /* decoder tells the reader to quit */
} play_ctx_t;

static i2s_chan_handle_t s_tx;
static uint32_t          s_rate = 44100;
static bool              s_i2s_on;
static volatile bool     s_stop;
static volatile bool     s_playing;
static volatile int      s_vol = 80;
static QueueHandle_t     s_info_q;

/* ------------------------------------------------------------------ */
/* I2S output                                                          */
/* ------------------------------------------------------------------ */

esp_err_t mp3_player_init(gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout)
{
    if (s_tx) {
        return ESP_ERR_INVALID_STATE;
    }

    s_info_q = xQueueCreate(1, sizeof(mp3_info_t));
    ESP_RETURN_ON_FALSE(s_info_q, ESP_ERR_NO_MEM, TAG, "info queue");

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    cc.dma_desc_num  = DMA_DESC_NUM;
    cc.dma_frame_num = DMA_FRAME_NUM;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, NULL), TAG, "new channel");

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk,
            .ws   = ws,
            .dout = dout,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "init std");

    s_rate   = 44100;
    s_i2s_on = false;      /* enabled lazily by set_rate() */
    return ESP_OK;
}

void mp3_player_set_volume(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    s_vol = percent;
}

void mp3_player_stop()       { s_stop = true; }
bool mp3_player_is_playing() { return s_playing; }

bool mp3_player_get_info(mp3_info_t *out)
{
    return s_info_q && xQueuePeek(s_info_q, out, 0) == pdTRUE;
}

/* The I2S clock can only be reconfigured while the channel is disabled. */
static esp_err_t set_rate(uint32_t hz)
{
    if (hz == s_rate && s_i2s_on) {
        return ESP_OK;
    }
    if (s_i2s_on) {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx), TAG, "disable");
        s_i2s_on = false;
    }
    if (hz != s_rate) {
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(hz);
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx, &clk), TAG, "reconfig clock");
        s_rate = hz;
    }
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");
    s_i2s_on = true;
    return ESP_OK;
}

/* Interleaved 16-bit PCM (mono or stereo) -> volume -> stereo -> I2S DMA ring. */
static esp_err_t write_pcm(const int16_t *pcm, int frames, int ch)
{
    static int16_t out[MAX_PCM_FRAMES * 2];
    if (frames > MAX_PCM_FRAMES || (ch != 1 && ch != 2)) {
        return ESP_ERR_INVALID_ARG;
    }

    const int gain = (s_vol * 256) / 100;
    for (int i = 0; i < frames; i++) {
        int16_t l = pcm[i * ch];
        int16_t r = (ch == 2) ? pcm[i * ch + 1] : l;
        out[2 * i]     = (int16_t)((l * gain) >> 8);
        out[2 * i + 1] = (int16_t)((r * gain) >> 8);
    }

    const size_t bytes = (size_t)frames * 4;
    size_t off = 0;
    while (off < bytes && !s_stop) {
        size_t written = 0;
        /* Blocks while the DMA ring is full: this paces the decoder to real time. */
        esp_err_t err = i2s_channel_write(s_tx, (uint8_t *)out + off, bytes - off, &written, 1000);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            return err;
        }
        off += written;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* SD reader task (core 0): file -> read-ahead stream buffer          */
/* ------------------------------------------------------------------ */

static void reader_task(void *arg)
{
    play_ctx_t *c = arg;

    while (!c->stop && !s_stop) {
        if (xStreamBufferSpacesAvailable(c->sb) >= READ_CHUNK_BYTES) {
            ssize_t n = read(c->fd, c->chunk, READ_CHUNK_BYTES);
            if (n > 0) {
                /* Space was checked above and only we add data, so this never blocks. */
                xStreamBufferSend(c->sb, c->chunk, (size_t)n, 0);
                continue;
            }
            if (n < 0) {
                ESP_LOGE(TAG, "read() failed");
            }
            c->eof = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    xSemaphoreGive(c->reader_done);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* Decoder task (core 1): stream buffer -> PCM -> I2S                 */
/* ------------------------------------------------------------------ */

static inline bool drained(const play_ctx_t *c)
{
    return c->eof && xStreamBufferIsEmpty(c->sb);
}

/* Reads up to len bytes, waiting for the reader; returns the number of bytes obtained. */
static size_t stream_read_fully(play_ctx_t *c, uint8_t *dst, size_t len)
{
    size_t got = 0;
    while (got < len && !s_stop) {
        size_t n = xStreamBufferReceive(c->sb, dst + got, len - got, pdMS_TO_TICKS(200));
        got += n;
        if (n == 0 && drained(c)) {
            break;
        }
    }
    return got;
}

/* Skips an ID3v2 tag (cover art can be hundreds of KB). Leaves any bytes already read in `in`. */
static void skip_id3(play_ctx_t *c, uint8_t *in, size_t *have)
{
    *have = stream_read_fully(c, in, 10);
    if (*have < 10 || memcmp(in, "ID3", 3) != 0) {
        return;
    }

    uint32_t size = ((in[6] & 0x7F) << 21) | ((in[7] & 0x7F) << 14) |
                    ((in[8] & 0x7F) << 7)  |  (in[9] & 0x7F);
    if (in[5] & 0x10) {
        size += 10;   /* footer present */
    }
    ESP_LOGI(TAG, "Skipping ID3v2 tag (%u bytes)", (unsigned)size);

    *have = 0;
    while (size > 0 && !s_stop) {
        size_t want = size > MP3_IN_BYTES ? MP3_IN_BYTES : size;
        size_t n = stream_read_fully(c, in, want);
        size -= n;
        if (n < want) {
            break;   /* end of file inside the tag */
        }
    }
}

static void decode_loop(play_ctx_t *c)
{
    mp3dec_t *dec = malloc(sizeof(*dec));
    uint8_t  *in  = malloc(MP3_IN_BYTES);
    int16_t  *pcm = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    if (!dec || !in || !pcm) {
        ESP_LOGE(TAG, "out of memory for decoder buffers");
        goto out;
    }
    mp3dec_init(dec);

    size_t   have = 0;
    uint64_t total_samples = 0;     /* per channel, for the elapsed-time counter */
    uint32_t frame_count = 0;
    int64_t  t_sum = 0, t_max = 0;
    mp3_info_t pub = {0};

    skip_id3(c, in, &have);

    while (!s_stop) {
        if (have < MP3_IN_BYTES) {
            have += xStreamBufferReceive(c->sb, in + have, MP3_IN_BYTES - have, pdMS_TO_TICKS(100));
        }
        if (have == 0) {
            if (drained(c)) break;
            continue;
        }

        mp3dec_frame_info_t fi;
        int64_t t0 = esp_timer_get_time();
        int samples = mp3dec_decode_frame(dec, in, (int)have, pcm, &fi);
        int64_t dt = esp_timer_get_time() - t0;

        if (fi.frame_bytes == 0) {
            /* No complete frame in the window. */
            if (drained(c)) break;                 /* trailing garbage at end of file */
            if (have == MP3_IN_BYTES) have = 0;    /* full window with no frame: resync */
            continue;
        }

        have -= (size_t)fi.frame_bytes;
        memmove(in, in + fi.frame_bytes, have);

        if (samples <= 0) {
            continue;   /* skipped junk or a frame that produced no audio */
        }

        frame_count++;
        total_samples += (uint64_t)samples;

#if LOG_DECODE_TIMING
        t_sum += dt;
        if (dt > t_max) t_max = dt;
        if (frame_count % LOG_TIMING_FRAMES == 0) {
            int64_t budget_us = (int64_t)samples * 1000000 / fi.hz;
            int64_t avg_us = t_sum / LOG_TIMING_FRAMES;
            ESP_LOGI(TAG, "decode avg %lld us, max %lld us, budget %lld us/frame (avg load %lld%%)",
                     (long long)avg_us, (long long)t_max, (long long)budget_us,
                     (long long)(avg_us * 100 / budget_us));
            t_sum = 0;
            t_max = 0;
        }
#else
        (void)dt; (void)t_sum; (void)t_max;
#endif

        if (frame_count == 1 || frame_count % INFO_PERIOD_FRAMES == 0) {
            pub.sample_rate  = (uint32_t)fi.hz;
            pub.channels     = fi.channels;
            pub.bitrate_kbps = fi.bitrate_kbps;
            pub.elapsed_ms   = (uint32_t)(total_samples * 1000ULL / (uint64_t)fi.hz);
            xQueueOverwrite(s_info_q, &pub);       /* never blocks; display peeks at its own pace */
        }

        if (set_rate((uint32_t)fi.hz) != ESP_OK ||
            write_pcm(pcm, samples, fi.channels) != ESP_OK) {
            ESP_LOGE(TAG, "I2S output error, stopping");
            break;
        }
    }

out:
    free(dec);
    free(in);
    free(pcm);
}

static void ctx_free(play_ctx_t *c)
{
    if (c->fd >= 0)   close(c->fd);
    if (c->chunk)     heap_caps_free(c->chunk);
    if (c->sb)        vStreamBufferDelete(c->sb);
    if (c->reader_done) vSemaphoreDelete(c->reader_done);
    free(c);
}

static void decoder_task(void *arg)
{
    play_ctx_t *c = arg;

    decode_loop(c);

    /* Stop the reader and wait for it to exit. */
    c->stop = true;
    xSemaphoreTake(c->reader_done, portMAX_DELAY);

    if (s_i2s_on) {                      /* flush a little silence, then stop the clocks */
        static const int16_t zeros[1024];
        size_t w;
        i2s_channel_write(s_tx, zeros, sizeof(zeros), &w, 100);
        i2s_channel_disable(s_tx);
        s_i2s_on = false;
    }

    ESP_LOGI(TAG, "playback ended (decoder stack high-water mark: %u bytes free)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    ctx_free(c);
    s_playing = false;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */

esp_err_t mp3_player_start(const char *path)
{
    if (!s_tx || !s_info_q) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playing) {
        return ESP_ERR_INVALID_STATE;
    }

    play_ctx_t *c = calloc(1, sizeof(*c));
    if (!c) {
        return ESP_ERR_NO_MEM;
    }
    c->fd = open(path, O_RDONLY);        /* read-only */
    if (c->fd < 0) {
        ESP_LOGE(TAG, "open(%s) failed", path);
        free(c);
        return ESP_ERR_NOT_FOUND;
    }

    c->chunk       = heap_caps_malloc(READ_CHUNK_BYTES, MALLOC_CAP_DMA);
    c->sb          = xStreamBufferCreate(READ_AHEAD_BYTES, 1);
    c->reader_done = xSemaphoreCreateBinary();
    if (!c->chunk || !c->sb || !c->reader_done) {
        ctx_free(c);
        return ESP_ERR_NO_MEM;
    }

    s_stop = false;
    s_playing = true;
    xQueueReset(s_info_q);

    if (xTaskCreatePinnedToCore(reader_task, "mp3_reader", READER_STACK_BYTES, c,
                                READER_PRIO, NULL, READER_CORE) != pdPASS) {
        s_playing = false;
        ctx_free(c);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(decoder_task, "mp3_decoder", DECODER_STACK_BYTES, c,
                                DECODER_PRIO, NULL, DECODER_CORE) != pdPASS) {
        c->stop = true;
        xSemaphoreTake(c->reader_done, portMAX_DELAY);
        s_playing = false;
        ctx_free(c);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "playing %s", path);
    return ESP_OK;
}