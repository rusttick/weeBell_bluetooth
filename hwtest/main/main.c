// Stage 2 + 3: boot info, PSRAM check, I2C scan on both candidate codec pin pairs, then a numbered
// hello line at CONFIG_HWTEST_LINES_PER_SEC. Consecutive line numbers make a dropped console line visible.
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_flash.h"
#include "esp32/spiram.h"

static const char *TAG = "hwtest";

// Candidate I2C pin pairs for the codec. Never touch GPIO0 (MCLK).
typedef struct {
    const char *name;
    int sda;
    int scl;
} i2c_pair_t;

static const i2c_pair_t I2C_PAIRS[] = {
    {"A (newer module)", 18, 23},
    {"B (older module)", 33, 32},
};

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external pin";
    case ESP_RST_SW:        return "software (esp_restart)";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT:  return "brown-out";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
    }
}

static void log_chip_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "chip: %d cores, revision %d, features 0x%" PRIx32 " (WiFi %d, BT classic %d, BLE %d)",
             chip.cores, chip.revision, (uint32_t)chip.features,
             !!(chip.features & CHIP_FEATURE_WIFI_BGN), !!(chip.features & CHIP_FEATURE_BT),
             !!(chip.features & CHIP_FEATURE_BLE));
    ESP_LOGI(TAG, "flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "reset reason: %s", reset_reason_str(esp_reset_reason()));
}

// Stage 3 step 2: PSRAM size, and a write/read-back of a large block.
static void check_psram(void)
{
    size_t chip_size = esp_spiram_get_size();
    size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "PSRAM chip size: %u bytes (%u MB)", (unsigned)chip_size, (unsigned)(chip_size >> 20));
    ESP_LOGI(TAG, "PSRAM in heap: total %u, free %u, largest block %u bytes",
             (unsigned)heap_total, (unsigned)heap_free, (unsigned)largest);
    if (heap_total == 0) {
        ESP_LOGE(TAG, "PSRAM: FAIL, none in the heap (init failed or not detected; see the boot log above)");
        return;
    }

    // ESP32 maps at most 4 MB of external RAM into the address space, so the heap sees at most 4 MB
    // even on an 8 MB chip. The rest needs himem (bank switching).
    size_t test_size = largest < (1024 * 1024) ? largest : (1024 * 1024);
    uint32_t *buf = heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        ESP_LOGE(TAG, "PSRAM: FAIL, could not allocate %u bytes", (unsigned)test_size);
        return;
    }
    size_t words = test_size / sizeof(uint32_t);
    int64_t t0 = esp_timer_get_time();
    for (size_t i = 0; i < words; i++) {
        buf[i] = (uint32_t)(i * 2654435761u);
    }
    int64_t t1 = esp_timer_get_time();
    size_t bad = 0;
    for (size_t i = 0; i < words; i++) {
        if (buf[i] != (uint32_t)(i * 2654435761u)) {
            bad++;
        }
    }
    int64_t t2 = esp_timer_get_time();
    heap_caps_free(buf);
    ESP_LOGI(TAG, "PSRAM test: %u bytes, %u mismatches, write %.1f MB/s, read %.1f MB/s -> %s",
             (unsigned)test_size, (unsigned)bad,
             (double)test_size / (double)(t1 - t0), (double)test_size / (double)(t2 - t1),
             bad == 0 ? "PASS" : "FAIL");
}

// Probe one address with an empty write; a device that ACKs is present.
static bool i2c_probe(i2c_port_t port, uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

// Stage 3 step 3: scan 0x01..0x7e on one pin pair. Returns the number of devices found.
static int i2c_scan_pair(const i2c_pair_t *pair)
{
    const i2c_port_t port = I2C_NUM_0;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = pair->sda,
        .scl_io_num = pair->scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    if (i2c_param_config(port, &conf) != ESP_OK || i2c_driver_install(port, conf.mode, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "I2C pair %s: driver setup failed", pair->name);
        return -1;
    }

    int found = 0;
    for (uint8_t addr = 0x01; addr < 0x7f; addr++) {
        if (i2c_probe(port, addr)) {
            const char *what = addr == 0x10 ? " (ES8388 codec)" : addr == 0x1a ? " (AC101 codec)" : "";
            ESP_LOGI(TAG, "I2C pair %s SDA %d / SCL %d: device at 0x%02x%s",
                     pair->name, pair->sda, pair->scl, addr, what);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "I2C pair %s SDA %d / SCL %d: no devices", pair->name, pair->sda, pair->scl);
    }
    i2c_driver_delete(port);
    // Release the pins so the next pair starts from idle.
    gpio_reset_pin(pair->sda);
    gpio_reset_pin(pair->scl);
    return found;
}

static void scan_i2c(void)
{
    for (size_t i = 0; i < sizeof(I2C_PAIRS) / sizeof(I2C_PAIRS[0]); i++) {
        i2c_scan_pair(&I2C_PAIRS[i]);
    }
}

void app_main(void)
{
    log_chip_info();
    check_psram();
    scan_i2c();
    ESP_LOGI(TAG, "stage 3 checks done; printing %d line(s) per second", CONFIG_HWTEST_LINES_PER_SEC);

    const TickType_t period = pdMS_TO_TICKS(1000 / CONFIG_HWTEST_LINES_PER_SEC);
    uint32_t n = 0;
    for (;;) {
        printf("hello %" PRIu32 "  uptime %" PRIu64 " ms\n", n++, esp_timer_get_time() / 1000);
        vTaskDelay(period);
    }
}
