#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "driver/gpio.h"
#include "linenoise/linenoise.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "stdio.h"
#include "errno.h"
#include "string.h"

//#include "TM1638_driver.h"
#include "drv8871_driver.h"
#include "hx711_driver.h"
#include "motor.h"
#include "command.h"
#include "config.h"

#define STR(x) #x
#define M_STR(x) STR(x)
#define COMPOSE_VERSION(major, minor, patch) STR(major.minor.patch)
#define VERSION COMPOSE_VERSION(CONFIG_PROJECT_VERSION_MAJOR, CONFIG_PROJECT_VERSION_MINOR, CONFIG_PROJECT_VERSION_PATCH)
#define PROJECT_NAME CONFIG_PROJECT_NAME
#define COMPILE_DATE __DATE__

#define CMD_LIST_GEN(FUNC) \
    FUNC('h', "h", print_help, , "print this help") \
    FUNC('?', "?", print_help, , "print this help") \
    FUNC('d', "d", HX711_toggle_output, stdout, "start/stop data output from weight sensor") \
    FUNC('m', "m", cmd_motor_ctrl, cmdbuf, "motor control...") \
    FUNC('c', "c", motor_start_cleaning, , "start cleaning") \
    FUNC('r', "r", motor_start_homing, , "start homing") \
    FUNC('t', "t", motor_stop_action, , "stop motor action") \
    FUNC('p', "p", print_status, , "print status") \
    FUNC('+', "+", cmd_motor_speed_up, , "speedup motor") \
    FUNC('-', "-", cmd_motor_speed_down, , "speeddown motor") \
    FUNC('i', "i", cmd_info, cmdbuf, "get info...") \
    FUNC('g', "g", cmd_get_config, cmdbuf, "get config...") \
    FUNC('s', "s", cmd_set_config, cmdbuf, "set config...") \
    FUNC('q', "q", cmd_quit, &quit, "exit console") \

#define GEN_HELP_STR(c, s, cmd, arg, help) s "\t\t" help "\n"
#define GEN_SWITCH_CASE(c, s, cmd, arg, help) case c: cmd(arg); break;

//MessageBufferHandle_t messageBuffer;
static const char *TAG = "command";
static const int uart_buffer_size = 1024;
static const char *greetings = "This is " PROJECT_NAME " commandline interface, version " VERSION ", build date " COMPILE_DATE ".\n"
"\n"
"Enter ? or h for help:\n";

static const char *helpstr = "Commands:\n"
CMD_LIST_GEN(GEN_HELP_STR)
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

static void print_help(){
    fputs(helpstr, stdout);
}

static void print_motor_speed(){
    printf("motor speed: %d\n", motor_get_speed());
}

static void print_motor_state(){
    printf("action: %s\nmotor : %s\n", motor_get_action_str(), motor_get_state_str());
}

static void print_status(){
    print_motor_state();
    print_motor_speed();
    ////including ssid, connection, ip address, port, etc.
    //print_wifi_state(); 
}

static void cmd_motor_speed_up(){
    motor_speed_up();
    print_motor_speed();
}

static void cmd_motor_speed_down(){
    motor_speed_down();
    print_motor_speed();
}

static void cmd_info(const char* cmd){
    assert (cmd[0] == 'i');
    const char* info_helpstr = "usage: i[?gm] print infomation\n"
        "ig\t\tprint gpio infomation\n"
        "im\t\tprint heap memory usage infomation\n"
        ;
    const char* p = cmd+1;
    switch (*p){
        case 'g':
            gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);
            break;
        case 'm':
            heap_caps_print_heap_info(MALLOC_CAP_8BIT);
            break;
        case '\0':
        case '\n':
        case '?':
        default:
            printf(info_helpstr);
            break;
    }
}

static void cmd_motor_ctrl(const char* cmd){
    assert (cmd[0] == 'm');
    const char* motor_helpstr = "usage: m[?frbc+-] motor control\n"
        "mf\t\tforward motor\n"
        "mr\t\treverse motor\n"
        "mb\t\tbrake motor\n"
        "mc\t\tcoast motor\n"
        "m+\t\tspeedup motor\n"
        "m-\t\tspeeddown motor\n"
        ;
    const char* p = cmd+1;
    switch (*p){
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
            cmd_motor_speed_up();
            break;
        case '-':
            cmd_motor_speed_down();
            break;
        case '\0':
        case '\n':
        case '?':
        default:
            printf(motor_helpstr);
            break;
    }
}

static esp_err_t parse_key(const char* cmd, char* key, size_t len){
    const char *p = cmd;
    char *q = key;
    while(*p == ' ' || *p == '\t') p++;
    while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\0'){
        if (q <= key+len-1) *q++ = *p++;
        else {
            *q = '\0';
            return ESP_ERR_NVS_INVALID_NAME;
        }
    }
    *q = '\0';
    return ESP_OK;
}

static void cmd_get_config(const char* cmd){
    assert (cmd[0] == 'g');
    const char* get_config_helpstr = "usage: g[?lsiuxb] get config\n"
        "gl      \t\tlist all config keys\n"
        "gi <key>\t\tget integer value with key <key>\n"
        "gu <key>\t\tget unsigned integer value with key <key>\n"
        "gx <key>\t\tget hexadecimal value with key <key>\n"
        "gs <key>\t\tget string value with key <key>\n"
        ;
    const char* p = cmd+1;
    char key[NVS_KEY_NAME_MAX_SIZE];
    esp_err_t ret = ESP_OK;
    union {int32_t i32;uint32_t u32;size_t sz;} value;
    switch (*p){
        case 'l':
            //TODO
            break;
        case 'i':
            ret = parse_key(p+1, key, sizeof(key));
            if (ESP_OK != ret) break;
            ret = config_get_i32(key, &value.i32);
            if (ESP_OK != ret) break;
            printf("key: %s, value:%"PRIi32"\n", key, value.i32);
            break;
        case 'u':
            ret = parse_key(p+1, key, sizeof(key));
            if (ESP_OK != ret) break;
            ret = config_get_u32(key, &value.u32);
            if (ESP_OK != ret) break;
            printf("key: %s, value:%"PRIu32"\n", key, value.u32);
            break;
        case 'x':
            ret = parse_key(p+1, key, sizeof(key));
            if (ESP_OK != ret) break;
            ret = config_get_u32(key, &value.u32);
            if (ESP_OK != ret) break;
            printf("key: %s, value:%"PRIx32"\n", key, value.u32);
            break;
        case 's':
            //TODO
            break;
        case '\0':
        case '\n':
        case '?':
        default:
            printf(get_config_helpstr);
            break;
    }
    if (ESP_OK != ret){
        printf("Error executing command %s:%s\n", cmd, esp_err_to_name(ret));
    }
}

void cmd_set_config(const char* cmd){
    assert (cmd[0] == 's');
    const char* p = cmd+1;
    //TODO
}

inline void cmd_quit(bool* quit){
    *quit = true;
}

void command_task(void *pvParameters)
{
    command_parameter *para = (command_parameter *) pvParameters;
    char *cmdbuf = NULL, *p;
    const char *id = "uart";
    const char *prompt = LOG_COLOR(LOG_COLOR_YELLOW)"esp> "LOG_RESET_COLOR;
    const char *prompt_nocolor = "esp> ";
    bool use_console = true;
    bool quit = false;

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
                goto exit;
            }
        }
        //ESP_LOGI(TAG, "Command (%d bytes): %s", cmdlen, cmdbuf);
        switch (cmdbuf[0]) {
            case '\0':
            case '\n':
                break;
            //generate case list with macro
            CMD_LIST_GEN(GEN_SWITCH_CASE)
            default:
                ESP_LOGW(TAG, "Unrecognized command: %c", cmdbuf[0]);
                break;
        }
    }
    fputs("Logging out..", stdout);
    ESP_LOGI(TAG, "Console %s logged out.", id);

exit:
    if (use_console) {
        //sleep and restart if we are on uart console
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        quit = false;
        goto start;
    }
    else {
        if (NULL != cmdbuf)
            free(cmdbuf);
    }

    if (NULL != para && NULL != para->at_exit) {
        //run callback function
        para->at_exit(para);
    }
    vTaskDelete(NULL);
}
