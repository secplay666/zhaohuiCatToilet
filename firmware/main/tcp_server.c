/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "freertos/message_buffer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "errno.h"
#include "string.h"
#include "sys/unistd.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "command.h"
#include "tcp_server.h"

#define PORT                        CONFIG_PORT
#define KEEPALIVE_IDLE              CONFIG_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_KEEPALIVE_COUNT
#define MAX_CONN                    CONFIG_MAXIMUM_CONNECTIONS


static const char *TAG = "server";
#define BUFFSIZE 128
#define STREAMBUFFSIZE 256

static const UBaseType_t conn_notify_index = 1;
static TaskHandle_t server_task_handle = NULL;

/*
static void do_retransmit(const int sock)
{
    int len;
    char rx_buffer[BUFFSIZE];
	size_t cmdlen = 0;
	char cmdbuf[BUFFSIZE];

	char c = 0;
	bool escape = false;
	bool overflow = false;

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            //ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);
            ESP_LOGI(TAG, "Received %d bytes", len);

			for (size_t i = 0; i < len; ++i){
				c = rx_buffer[i];
				if (escape) {
					if (cmdlen >= BUFFSIZE-1){
						if (!overflow){
							overflow = true;
							ESP_LOGW("stream", "Max command length %d exceeded", BUFFSIZE);
						}
					} else cmdbuf[cmdlen++] = c;
					escape = false;
				} else {
					switch (c) {
						case '\\':
							escape = true;
							break;
						case '\n':
							// send data to command processing task
                            if (messageBuffer)
                                xMessageBufferSend(messageBuffer, cmdbuf, cmdlen, portMAX_DELAY);
							overflow = false;
							cmdlen = 0;
							break;
						default:
							if (cmdlen >= BUFFSIZE){
								if (!overflow){
									overflow = true;
									ESP_LOGW("stream", "Max command length %d exceeded", BUFFSIZE);
								}
							} else cmdbuf[cmdlen++] = c;
							break;
					}
				}
			}


            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
			
            //int to_write = len;
            //while (to_write > 0) {
            //    int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
            //    if (written < 0) {
            //        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            //    }
            //    to_write -= written;
            //}
			
        }
    } while (len > 0);
}
*/

void cleanup_socket(command_parameter * para){
    //shutdown(sock, 0);
    if (para->stream_in == para->stream_out) {
        fclose(para->stream_in);
        fclose(para->stream_out);
    } else
        fclose(para->stream_in);
    free(para);
    //connection ends, increasing semaphore
    xTaskNotifyGiveIndexed(server_task_handle, conn_notify_index);
}

esp_err_t start_console(int sock){
    esp_err_t err = ESP_OK;

    //int sock_copy = dup(sock);
    //if (sock_copy < 0){
    //    ESP_LOGE(TAG, "dup: %s", strerror(errno));
    //    err = ESP_FAIL;
    //    close(sock);
    //    goto exit;
    //}

    //open stream for read and write
    FILE* stream_in = fdopen(sock, "r+");
    if (NULL == stream_in){
        ESP_LOGE(TAG, "fdopen: %s", strerror(errno));
        err = ESP_FAIL;
        goto exit_cleanup1;
    }

    FILE* stream_out = stream_in;
    //FILE* stream_out = fdopen(sock_copy, "w");
    //if (NULL == stream_out){
    //    ESP_LOGE(TAG, "fdopen: %s", strerror(errno));
    //    fclose(stream_in);
    //    err = ESP_FAIL;
    //    goto exit_cleanup1;
    //}

    command_parameter * para = malloc(sizeof(command_parameter));
    if (NULL == para){
        ESP_LOGE(TAG, "malloc: %s", strerror(errno));
        err = ESP_ERR_NO_MEM;
        goto exit_cleanup2;
    }
    para->id = "webconsole";
    para->stream_in = stream_in;
    para->stream_out = stream_out;
    para->at_exit = cleanup_socket;

    //create command task
    BaseType_t rtos_err = xTaskCreate(command_task, "webconsole", 4096, (void*)para, 5, NULL);
    if (pdPASS != rtos_err){
        ESP_LOGE(TAG, "xTaskCreate failed: Cannot allocate required memory");
        err = ESP_ERR_NO_MEM;
        goto exit_cleanup3;
    }
    goto exit;

exit_cleanup3:
    free(para);
exit_cleanup2:
    fclose(stream_in);
    //fclose(stream_out);
    goto exit;
exit_cleanup1:
    //close(sock_copy);
    close(sock);
exit:
    //creation of task failed, increasing semaphore
    xTaskNotifyGiveIndexed(server_task_handle, conn_notify_index);
    return err;
}

void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;
    server_task_handle = xTaskGetCurrentTaskHandle();

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }
#ifdef CONFIG_IPV6
    else if (addr_family == AF_INET6) {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_IPV4) && defined(CONFIG_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    //setup task notify value as counting semaphore
    //to limit connection number to MAX_CONN
    xTaskNotifyIndexed(server_task_handle, conn_notify_index, MAX_CONN, eSetValueWithOverwrite);

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);

        //If we have maximal connections, wait for some other connection to
        //close before accepting new connection.
        //Decrease available connection number by 1 .
        ulTaskNotifyTakeIndexed(conn_notify_index, pdFALSE, portMAX_DELAY);

        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#ifdef CONFIG_IPV6
        else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        //do_retransmit(sock);
        start_console(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

