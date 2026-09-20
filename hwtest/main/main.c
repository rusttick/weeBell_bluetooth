// Hardware validation firmware for the ESP32 Audio Kit V2.2: a serial console with one command per
// test (see README.md and doc/validate_board_plan.md).
#include <stdio.h>
#include "esp_console.h"
#include "esp_system.h"
#include "cmds.h"

extern const char *reset_reason_str(esp_reset_reason_t r);

void app_main(void)
{
    gpio_tools_init();  // first: it records the pin levels at the start of app_main

    printf("\nhwtest console. Reset reason: %s. Type 'help'.\n", reset_reason_str(esp_reset_reason()));

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "hwtest> ";
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    esp_console_register_help_command();
    register_sys_commands();
    register_gpio_commands();
    register_codec_commands();
    register_audio_commands();
    register_mic_commands();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
