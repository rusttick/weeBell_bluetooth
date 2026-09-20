// Stage 5: talk to the ES8388 codec over I2C (SDA 33 / SCL 32, address 0x10), no audio yet.
//
// The repo's es8388.c cannot be used as is: it is tied to the gCore board's I2C pins (21/22) and an audio_hal
// layer. This file repeats its es8388_init() register sequence (I2S slave mode, ADC input LINE2, DAC output
// LINE2) over our own I2C bus.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_console.h"
#include "esp_err.h"
#include "cmds.h"

#define CODEC_PORT   I2C_NUM_1
#define CODEC_SDA    33
#define CODEC_SCL    32
#define CODEC_ADDR   0x10   // 7-bit; the repo's driver writes 0x20, the 8-bit form
#define I2C_TIMEOUT  pdMS_TO_TICKS(100)

#define REG_CHIPPOWER 0x02
#define REG_LDACVOL   0x1a
#define REG_RDACVOL   0x1b
#define LAST_REG      0x34

typedef struct {
    uint8_t reg;
    uint8_t val;
} reg_write_t;

// es8388_init() with adc_input = LINE2 (0x50) and dac_output = LINE2 (LOUT2 | ROUT2 = 0x28), slave mode.
static const reg_write_t INIT_SEQ[] = {
    {0x00, 0x12}, {0x01, 0x50}, {0x02, 0x00}, {0x05, 0x00}, {0x06, 0x00}, {0x07, 0x7c},
    {0x35, 0xa0}, {0x37, 0xd0}, {0x39, 0xd0},   // undocumented DLL settings for 8 kHz, from Espressif
    {0x08, 0x00},                                // I2S slave mode
    // DAC
    {0x04, 0xc0}, {0x17, 0x18}, {0x18, 0x02}, {0x19, 0x64}, {0x1c, 0x08}, {0x1d, 0x00},
    {0x26, 0x00}, {0x27, 0x90}, {0x2a, 0x90}, {0x2b, 0x80}, {0x2d, 0x00},
    {0x2e, 0x21}, {0x2f, 0x21}, {0x30, 0x21}, {0x31, 0x21},
    // Enable DAC and the LOUT2 / ROUT2 outputs (the headphone jack). The repo's DAC_OUTPUT_LOUT2 | ROUT2 is 0x28, but
    // the datasheet bit map for this register is bit5 LOUT1, bit4 ROUT1, bit3 LOUT2, bit2 ROUT2, so 0x28 turns on
    // the LEFT channel of both outputs and the right channel stays off. 0x0c is LOUT2 + ROUT2.
    {0x04, 0x0c},
    // ADC
    {0x03, 0xff}, {0x09, 0x33}, {0x0a, 0x50}, {0x0b, 0x02}, {0x0c, 0x0c}, {0x0d, 0x02}, {0x0e, 0x30},
    {0x0f, 0x60}, {0x12, 0x38}, {0x13, 0xb0}, {0x14, 0x32}, {0x15, 0x06}, {0x16, 0xdb},
    {0x10, 0x00}, {0x11, 0x00},                  // ADC volume 0 dB
    {0x03, 0x09},                                // power up the ADC, line-in enabled
};

static bool i2c_ready;

static esp_err_t codec_i2c_start(void)
{
    if (i2c_ready) {
        return ESP_OK;
    }
    // No internal pull-ups: the module has its own 10 kΩ pull-ups, and this checks they are enough.
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CODEC_SDA,
        .scl_io_num = CODEC_SCL,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t err = i2c_param_config(CODEC_PORT, &conf);
    if (err == ESP_OK) {
        err = i2c_driver_install(CODEC_PORT, conf.mode, 0, 0, 0);
    }
    if (err == ESP_OK) {
        i2c_ready = true;
    }
    return err;
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(CODEC_PORT, CODEC_ADDR, buf, sizeof(buf), I2C_TIMEOUT);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(CODEC_PORT, CODEC_ADDR, &reg, 1, val, 1, I2C_TIMEOUT);
}

// Shared with cmds_audio.c
bool codec_ready(void)
{
    esp_err_t err = codec_i2c_start();
    if (err != ESP_OK) {
        printf("I2C setup failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

esp_err_t codec_write(uint8_t reg, uint8_t val)
{
    return codec_ready() ? reg_write(reg, val) : ESP_FAIL;
}

static bool require_i2c(void)
{
    esp_err_t err = codec_i2c_start();
    if (err != ESP_OK) {
        printf("I2C setup failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

static int codec_regs(void)
{
    printf("ES8388 registers (I2C 0x%02x on SDA %d / SCL %d):\n", CODEC_ADDR, CODEC_SDA, CODEC_SCL);
    int errors = 0;
    for (int r = 0; r <= LAST_REG; r++) {
        uint8_t v = 0;
        esp_err_t err = reg_read((uint8_t)r, &v);
        if (err != ESP_OK) {
            printf("0x%02x: %-4s ", r, "ERR");
            errors++;
        } else {
            printf("0x%02x: 0x%02x ", r, v);
        }
        if ((r & 7) == 7 || r == LAST_REG) {
            printf("\n");
        }
    }
    printf("%d read errors\n", errors);
    return errors ? 1 : 0;
}

// Write patterns to the two DAC volume registers (harmless with the DAC idle), read each back, restore.
static int codec_rw(void)
{
    static const uint8_t patterns[] = {0x00, 0x55, 0xaa, 0x33, 0xcc, 0xff};
    static const uint8_t regs[] = {REG_LDACVOL, REG_RDACVOL};
    int bad = 0, total = 0;

    for (size_t i = 0; i < sizeof(regs); i++) {
        uint8_t orig = 0;
        if (reg_read(regs[i], &orig) != ESP_OK) {
            printf("read of reg 0x%02x failed\n", regs[i]);
            return 1;
        }
        for (size_t k = 0; k < sizeof(patterns); k++) {
            uint8_t got = 0;
            esp_err_t err = reg_write(regs[i], patterns[k]);
            if (err == ESP_OK) {
                err = reg_read(regs[i], &got);
            }
            total++;
            if (err != ESP_OK || got != patterns[k]) {
                bad++;
                printf("reg 0x%02x: wrote 0x%02x, read 0x%02x (%s)\n", regs[i], patterns[k], got,
                       err == ESP_OK ? "MISMATCH" : esp_err_to_name(err));
            }
        }
        reg_write(regs[i], orig);
        uint8_t after = 0;
        reg_read(regs[i], &after);
        printf("reg 0x%02x: original 0x%02x, restored to 0x%02x\n", regs[i], orig, after);
    }
    printf("read/write test: %d of %d patterns matched -> %s\n", total - bad, total, bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}

// Reset the codec, run the init sequence, and read every register back.
int codec_init(void)
{
    esp_err_t err = reg_write(REG_CHIPPOWER, 0xff);  // what es8388_deinit() does: reset and stop
    if (err != ESP_OK) {
        printf("reset write failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    int write_errors = 0, diffs = 0;
    for (size_t i = 0; i < sizeof(INIT_SEQ) / sizeof(INIT_SEQ[0]); i++) {
        if (reg_write(INIT_SEQ[i].reg, INIT_SEQ[i].val) != ESP_OK) {
            printf("write of reg 0x%02x failed\n", INIT_SEQ[i].reg);
            write_errors++;
        }
    }

    // Read back the final value of each register we wrote. Some registers hold different bits than were
    // written (reserved or read-only bits), so a difference is reported but is not by itself a failure.
    for (size_t i = 0; i < sizeof(INIT_SEQ) / sizeof(INIT_SEQ[0]); i++) {
        uint8_t reg = INIT_SEQ[i].reg;
        bool last = true;
        for (size_t j = i + 1; j < sizeof(INIT_SEQ) / sizeof(INIT_SEQ[0]); j++) {
            if (INIT_SEQ[j].reg == reg) {
                last = false;
            }
        }
        if (!last) {
            continue;  // only check the value that ends up in the register
        }
        uint8_t got = 0;
        if (reg_read(reg, &got) != ESP_OK) {
            printf("read of reg 0x%02x failed\n", reg);
            write_errors++;
        } else if (got != INIT_SEQ[i].val) {
            printf("reg 0x%02x: wrote 0x%02x, reads 0x%02x\n", reg, INIT_SEQ[i].val, got);
            diffs++;
        }
    }
    printf("init: %d writes, %d I2C errors, %d registers read back different\n",
           (int)(sizeof(INIT_SEQ) / sizeof(INIT_SEQ[0])), write_errors, diffs);
    printf("init %s (ADC input LINE2, DAC output LINE2, I2S slave; no audio until I2S and MCLK run)\n",
           write_errors ? "FAILED" : "completed");
    return write_errors ? 1 : 0;
}

static int cmd_codec(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: codec regs | rw | init | r <reg> | w <reg> <val>\n");
        return 1;
    }
    if (!require_i2c()) {
        return 1;
    }
    if (!strcmp(argv[1], "r") && argc >= 3) {
        uint8_t v = 0;
        int reg = (int)strtol(argv[2], NULL, 0);
        esp_err_t err = reg_read((uint8_t)reg, &v);
        printf("reg 0x%02x = 0x%02x%s\n", reg, v, err == ESP_OK ? "" : " (read failed)");
        return err == ESP_OK ? 0 : 1;
    } else if (!strcmp(argv[1], "w") && argc >= 4) {
        int reg = (int)strtol(argv[2], NULL, 0);
        int val = (int)strtol(argv[3], NULL, 0);
        uint8_t back = 0;
        esp_err_t err = reg_write((uint8_t)reg, (uint8_t)val);
        if (err == ESP_OK) {
            err = reg_read((uint8_t)reg, &back);
        }
        printf("reg 0x%02x <- 0x%02x, reads 0x%02x%s\n", reg, val, back, err == ESP_OK ? "" : " (I2C error)");
        return err == ESP_OK ? 0 : 1;
    }
    if (!strcmp(argv[1], "regs")) {
        return codec_regs();
    } else if (!strcmp(argv[1], "rw")) {
        return codec_rw();
    } else if (!strcmp(argv[1], "init")) {
        return codec_init();
    }
    printf("usage: codec regs | rw | init | r <reg> | w <reg> <val>\n");
    return 1;
}

void register_codec_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "codec",
        .help = "ES8388 over I2C (SDA 33 / SCL 32, 0x10): regs | rw | init | r <reg> | w <reg> <val> (numbers as 0x..)",
        .func = cmd_codec,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
