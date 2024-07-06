#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "linenoise/linenoise.h"
#include "esp_log.h"
#include "esp_err.h"
#include "stdio.h"
#include "errno.h"
#include "string.h"

#include "TM1638_driver.h"
#include "drv8871_driver.h"
#include "motor.h"
#include "command.h"

#define STR(x) #x
#define COMPOSE_VERSION(major, minor, patch) STR(major.minor.patch)
#define VERSION COMPOSE_VERSION(CONFIG_PROJECT_VERSION_MAJOR, CONFIG_PROJECT_VERSION_MINOR, CONFIG_PROJECT_VERSION_PATCH)
#define PROJECT_NAME CONFIG_PROJECT_NAME
#define COMPILE_DATE __DATE__

//MessageBufferHandle_t messageBuffer;
static const char *TAG = "command";
static const int uart_buffer_size = 1024;
const char *greetings = "This is " PROJECT_NAME " commandline interface, version " VERSION ", build date " COMPILE_DATE ".\n"
"\n"
"Enter ? or h for help:\n";

const char *helpstr = "Commands:\n"
"w      write "STR(TM1638_MEMSIZE)" bytes of hex data to TM1638 segment display\n"
"s      write a string to TM1638 segment display\n"
"f      start motor forward\n"
"r      start motor reversed\n"
"b      brake motor\n"
"c      coast motor\n"
"+      speedup motor\n"
"-      speeddown motor\n"
"q      exit console\n"
"\n";

//hold old vprintf function for log
vprintf_like_t old_vprintf = NULL;

//vprintf that outputs to stderr
static int veprintf(const char* format, va_list vlist){
    return vfprintf(stderr, format, vlist);
}

esp_err_t command_init(void){
    //redirects all logs to stderr
    old_vprintf = esp_log_set_vprintf(veprintf);
    return ESP_OK;
}

uint8_t hex_to_nibble(char c){
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

void print_help(){
    fputs(helpstr, stdout);
}

void print_status(){
}

void command_task(void *pvParameters)
{
    command_parameter *para = (command_parameter *) pvParameters;
    size_t cmdlen = 0;
    char *cmdbuf = NULL, *p;
    const char *id = "uart";
    const char *prompt = LOG_COLOR(LOG_COLOR_YELLOW)"esp> "LOG_RESET_COLOR;
    const char *prompt_nocolor = "esp> ";
    bool use_console = true;
    bool quit = false;
    uint8_t b;

    if (para) {
        //redirect standard io and set id
        if (NULL == para->stream_in || NULL == para->stream_out) {
            ESP_LOGE(TAG, "Invalid input/output stream");
            goto exit;
        }
        id = para->id;
        stdin  = para->stream_in;
        stdout = para->stream_out;
        //setting dumb mode for linenoise
        linenoiseSetDumbMode(1);
        prompt = prompt_nocolor;
        use_console = false;
    }
    else {
        //install uart driver
        if (!uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM))
            ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, uart_buffer_size, uart_buffer_size, 0, NULL, 0));
        //use blocking driver mode for uart console
        uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

        // Disable buffering on stdin of the current task.
        setvbuf(stdin, NULL, _IONBF, 0);
        /* Figure out if the terminal supports escape sequences */
        int probe_status = linenoiseProbe();
        if (probe_status) {
            /* zero indicates success */
            linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
            /* Since the terminal doesn't support escape sequences,
             * don't use color codes in the s_prompt.
             */
            prompt = prompt_nocolor;
#endif //CONFIG_LOG_COLORS
        }
        if (linenoiseIsDumbMode()) {
            printf("\n"
                   "Your terminal application does not support escape sequences.\n\n"
                   "Line editing and history features are disabled.\n\n"
                   "On Windows, try using Putty instead.\n");
        }
    }

    //allocate line buffer for web console
    if (!use_console) {
        cmdbuf = calloc(1, COMMANDBUFFSIZE);
        if (NULL == cmdbuf){
            ESP_LOGE(TAG, "Cannot allocate command buffer. calloc: %s", strerror(errno));
            goto exit;
        }
    }

start:
    //print greetings message
    fputs(greetings, stdout);

    //command loop
    while (!quit) {
        if (use_console) {
            p = linenoise(prompt);
            cmdbuf = p;
        } else {
            // print pormpt string
            fputs(prompt, stdout);
            fflush(stdout);
            p = fgets(cmdbuf, COMMANDBUFFSIZE, stdin);
        }
        
        //handle input error
        if (NULL == p) {
            if (feof(stdin)){
                //input connection closed
                break;
            }
            else {
                ESP_LOGE(TAG, "fgets: %s", strerror(errno));
            }
        }
        //ESP_LOGI(TAG, "Command (%d bytes): %s", cmdlen, cmdbuf);
        switch (cmdbuf[0]) {
            case '\0':
            case '\n':
                break;
            case 'h':
            case '?':
                print_help();
                break;
            case 'w':
                p = cmdbuf + 1;
                for (int i = 0; i < TM1638_MEMSIZE; ++i){
                    while (' ' == *p) p++;
                    if (p >= cmdbuf + cmdlen){
                        ESP_LOGW(TAG, "Command syntax warning: w: insufficient data length");
                        break;
                    }
                    b = hex_to_nibble(*p++);

                    while (' ' == *p) p++;
                    if (p >= cmdbuf + cmdlen){
                        ESP_LOGW(TAG, "Command syntax warning: w: insufficient data length");
                        break;
                    }
                    b = (b << 4) | hex_to_nibble(*p++);

                    TM1638_write_buffer(i, b);
                }
                TM1638_flush();
                break;
            case 's':
                p = cmdbuf + 2;
                for (int i = 0; i < TM1638_MEMSIZE; i=i+2){
                    if ((p >= cmdbuf + cmdlen) || (*p >= 128)) {
                        TM1638_write_buffer(i, TM1638_char_table[(int)' ']);
                        continue;
                    }
                    TM1638_write_buffer(i, TM1638_char_table[(int)*p]);
                    p++;
                }
                TM1638_flush();
                break;
            case 'q':
                quit = true;
                break;
            case 'f':
                motor_forward();
                motor_auto_stop();
                break;
            case 'r':
                motor_reverse();
                motor_auto_stop();
                break;
            case 'b':
                motor_brake();
                motor_auto_stop();
                break;
            case 'c':
                motor_coast();
                motor_auto_stop();
                break;
            case '+':
                motor_speed_up();
                break;
            case '-':
                motor_speed_down();
                break;
            case 'p':
                print_status();
                break;
            default:
                ESP_LOGW(TAG, "Unrecognized command: %c", cmdbuf[0]);
                break;
        }
    }
    ESP_LOGI(TAG, "Console %s logged out.", id);

exit:
    if (!use_console)
        if (NULL != cmdbuf)
            free(cmdbuf);
    if (NULL == para){
        //sleep and restart if we are on uart console
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        goto start;
    }
    else if (NULL != para->at_exit) {
        //run callback function
        para->at_exit(para);
    }
    vTaskDelete(NULL);
}
