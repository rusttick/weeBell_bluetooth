// System-level tests carried over from stages 2 and 3: chip info, PSRAM, I2C scan, console stress.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp32/spiram.h"
#include "cmds.h"

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

const char *reset_reason_str(esp_reset_reason_t r)
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

static int cmd_info(int argc, char **argv)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    printf("chip: %d cores, revision %d, features 0x%" PRIx32 " (WiFi %d, BT classic %d, BLE %d)\n",
           chip.cores, chip.revision, (uint32_t)chip.features,
           !!(chip.features & CHIP_FEATURE_WIFI_BGN), !!(chip.features & CHIP_FEATURE_BT),
           !!(chip.features & CHIP_FEATURE_BLE));
    printf("flash: %" PRIu32 " MB\n", flash_size / (1024 * 1024));
    printf("free heap: %" PRIu32 " bytes\n", esp_get_free_heap_size());
    printf("reset reason: %s\n", reset_reason_str(esp_reset_reason()));
    printf("uptime: %" PRIu64 " ms\n", esp_timer_get_time() / 1000);
    return 0;
}

// Stage 3: PSRAM size, and a write/read-back of a large block.
static int cmd_psram(int argc, char **argv)
{
    size_t chip_size = esp_spiram_get_size();
    size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    printf("PSRAM chip size: %u bytes (%u MB)\n", (unsigned)chip_size, (unsigned)(chip_size >> 20));
    printf("PSRAM in heap: total %u, free %u, largest block %u bytes\n", (unsigned)heap_total,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)largest);
    if (heap_total == 0) {
        printf("PSRAM: FAIL, none in the heap\n");
        return 1;
    }
    size_t test_size = largest < (1024 * 1024) ? largest : (1024 * 1024);
    uint32_t *buf = heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        printf("PSRAM: FAIL, could not allocate %u bytes\n", (unsigned)test_size);
        return 1;
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
    printf("PSRAM test: %u bytes, %u mismatches, write %.1f MB/s, read %.1f MB/s -> %s\n",
           (unsigned)test_size, (unsigned)bad, (double)test_size / (double)(t1 - t0),
           (double)test_size / (double)(t2 - t1), bad == 0 ? "PASS" : "FAIL");
    return bad == 0 ? 0 : 1;
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
        printf("I2C pair %s: driver setup failed\n", pair->name);
        return -1;
    }
    int found = 0;
    for (uint8_t addr = 0x01; addr < 0x7f; addr++) {
        if (i2c_probe(port, addr)) {
            printf("I2C pair %s SDA %d / SCL %d: device at 0x%02x%s\n", pair->name, pair->sda, pair->scl,
                   addr, addr == 0x10 ? " (ES8388 codec)" : addr == 0x1a ? " (AC101 codec)" : "");
            found++;
        }
    }
    if (found == 0) {
        printf("I2C pair %s SDA %d / SCL %d: no devices\n", pair->name, pair->sda, pair->scl);
    }
    i2c_driver_delete(port);
    gpio_reset_pin(pair->sda);
    gpio_reset_pin(pair->scl);
    return found;
}

static int cmd_i2cscan(int argc, char **argv)
{
    for (size_t i = 0; i < sizeof(I2C_PAIRS) / sizeof(I2C_PAIRS[0]); i++) {
        i2c_scan_pair(&I2C_PAIRS[i]);
    }
    // The scan leaves IO18 and IO23 with pull-ups on; put the test pins back to plain inputs.
    gpio_tools_restore_floating();
    return 0;
}

// Console stress test: hello <lines_per_sec> <seconds>. Consecutive numbers show a dropped line.
static int cmd_hello(int argc, char **argv)
{
    int rate = argc > 1 ? atoi(argv[1]) : 1;
    int secs = argc > 2 ? atoi(argv[2]) : 10;
    if (rate < 1 || rate > 100 || secs < 1) {
        printf("usage: hello <lines per second 1-100> <seconds>\n");
        return 1;
    }
    const TickType_t period = pdMS_TO_TICKS(1000 / rate);
    for (uint32_t n = 0; n < (uint32_t)(rate * secs); n++) {
        printf("hello %" PRIu32 "  uptime %" PRIu64 " ms\n", n, esp_timer_get_time() / 1000);
        vTaskDelay(period);
    }
    return 0;
}

void register_sys_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command = "info", .help = "Chip, flash, heap, reset reason, uptime", .func = cmd_info},
        {.command = "psram", .help = "PSRAM size and a 1 MB write/read-back test (stage 3)", .func = cmd_psram},
        {.command = "i2cscan", .help = "Scan the two candidate codec I2C pin pairs (stage 3)", .func = cmd_i2cscan},
        {.command = "hello", .help = "Console stress test: hello <lines per second> <seconds> (stage 2)", .func = cmd_hello},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}
