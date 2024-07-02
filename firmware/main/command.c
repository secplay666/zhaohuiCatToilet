#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "stdio.h"
#include "errno.h"
#include "string.h"

#include "TM1638_driver.h"
#include "drv8871_driver.h"
#include "motor.h"
#include "command.h"

#define STR(x) #x
#define VERSION STR(CONFIG_PROJECT_VERSION_MAJOR.CONFIG_PROJECT_VERSION_MINOR.CONFIG_PROJECT_VERSION_PATCH)
#define PROJECT_NAME CONFIG_PROJECT_NAME
#define COMPILE_DATE __DATE__

//MessageBufferHandle_t messageBuffer;
static const char *TAG = "command";
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

uint8_t hex_to_nibble(char c){
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return 0;
}

void print_help(){
    puts(helpstr);
}

void print_status(){
}

void command_task(void *pvParameters)
{
    command_parameter *para = (command_parameter *) pvParameters;
	size_t cmdlen = 0;
	char cmdbuf[COMMANDBUFFSIZE];
    const char *id = "uart";
    const char *prompt = "esp> ";
	char *p = cmdbuf;
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
        stderr = para->stream_out;
    }
    else {
        //use blocking driver mode for uart console
        uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    }

start:
    //print greetings message
    puts(greetings);

    //command loop
	while (!quit) {
        // print pormpt string
        puts(prompt);
        
		//cmdlen = xMessageBufferReceive(messageBuffer, cmdbuf, COMMANDBUFFSIZE-1, portMAX_DELAY);
        p = fgets(cmdbuf, COMMANDBUFFSIZE, stdin);
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
                motor_next_state = M_Forward_Starting;
                motor_auto_stop();
                //DRV8871_forward_brake();
				break;
			case 'r':
                motor_next_state = M_Reverse_Starting;
                motor_auto_stop();
                //DRV8871_reverse_brake();
				break;
			case 'b':
                motor_next_state = M_Brake;
                motor_auto_stop();
                //DRV8871_brake();
				break;
			case 'c':
                motor_next_state = M_Coast;
                motor_auto_stop();
                //DRV8871_coast();
				break;
			case '+':
                DRV8871_speed_up();
				break;
			case '-':
                DRV8871_speed_down();
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
