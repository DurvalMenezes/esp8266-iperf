/* Iperf Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <errno.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"

#include "cmd_autorun.h"
#include "rom/uart.h"

#define WIFI_CONNECTED_BIT BIT0

static void initialize_console()
{
    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_CONSOLE_UART_NUM,
                                         256, 0, 0, NULL, 0) );

    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(CONFIG_CONSOLE_UART_NUM);

    /* Initialize the console */
    esp_console_config_t console_config = {
        .max_cmdline_args = 32,
        .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
        .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback *) &esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(100);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) { // 20201227 DM There's no ESP_ERR_NVS_NEW_VERSION_FOUND on ESP8266_RTOS_SDK
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    initialise_wifi();

    // Check whether autorun is set and gets it ready to execute, then gives the user an opportunity to interrupt
    // This *must* happen before initialize_console() right below, or else uart_rx_one_char() stops working.
    char *autorun_cmdlist;
    int err;
    if ((autorun_cmdlist = fn_autorun_get()) != NULL) {
        printf("ATTENTION: Autorun command-list is [%s]\n", autorun_cmdlist);
        for (int ix=5; ix>0; --ix) {
            printf("\rPress ^C to abort, or <Enter> to execute immediatelly before count reaches zero: %d", ix);
            static uint8_t ch;
            if ((err = uart_rx_one_char(&ch)) == OK) {
                if (ch == '\n' || ch == '\r') {
                    printf("\n\nSkipping count and going ahead with autorun");
                    break;
                }
                else if (ch == '\x03') { //ETX aka ^C
                    printf("\n\nAutorun interrupted");
                    autorun_cmdlist = NULL;
                    break;
                }
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS); //delay for 1 second
        }
        printf("\n\n");
    }

    initialize_console();

    /* Register commands */
    esp_console_register_help_command();
    register_system();
    register_wifi();
    register_autorun();

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    const char *prompt = LOG_COLOR_I "esp8266> " LOG_RESET_COLOR;

    printf("\n ==================================================\n");
    printf(" |       Steps to test WiFi throughput            |\n");
    printf(" |                                                |\n");
    printf(" |  1. Print 'help' to gain overview of commands  |\n");
    printf(" |  2. Configure device to station or soft-AP     |\n");
    printf(" |  3. Setup WiFi connection                      |\n");
    printf(" |  4. Run iperf to test UDP/TCP RX/TX throughput |\n");
    printf(" =================================================|\n");
    printf("\n");
    printf(" See also the `autorun_*` commands for headless/automated operation, eg:\n");
    printf("    autorun_set \"sta SSID PASSWORD; autorun_delay 2000; iperf -s; autorun_wait iperf_traffic; restart\"\n");
    printf("\n\n");

    /* Figure out if the terminal supports escape sequences */
    int probe_status = linenoiseProbe();
    if (probe_status) { /* zero indicates success */
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n");
        linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
        /* Since the terminal doesn't support escape sequences,
         * don't use color codes in the prompt.
         */
        prompt = "esp8266> ";
#endif //CONFIG_LOG_COLORS
    }

    char *autorun_cmd = autorun_cmdlist;
    /* Main loop */
    while (true) {
        char *line;
        if (autorun_cmd != NULL) {
            //get line as the next command from autorun_cmdlist
            line = autorun_cmd;
            autorun_cmd=index(autorun_cmd,';'); //advance to the end of the current command (next `;` character)
            if (autorun_cmd)
                *(autorun_cmd++) = '\0';    //replace ';' with NUL so as to properly finish the line, and advance autorun to the beginning of the next command
            printf("%s [Autorun] now executing `%s`\n", prompt, line);
        } else {
            /* Get a line using linenoise.
             * The line is returned when ENTER is pressed.
             */
            line = linenoise(prompt);
            if (line == NULL) { /* Ignore empty lines */
                continue;
            }
            /* Add the command to the history */
            linenoiseHistoryAdd(line);
        }

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x\n", ret);
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }
}

