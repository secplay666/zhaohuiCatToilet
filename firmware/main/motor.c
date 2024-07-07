#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "motor.h"
#include "drv8871_driver.h"
#include "TM1638_driver.h"

static TaskHandle_t motor_task_handle = NULL;
static const UBaseType_t notify_index = 1;

static enum motor_state state = M_Idle;
static int counter = 0;
static bool motor_auto = true;
//static int motor_auto_count = 0;
static const char *TAG = "motor";

const int PERIOD = 100;
const int BRAKE_TIME = 1000;
const int START_TIME = 2000;

// speed range from 0 to 100
static int max_speed = 50;
const int SPEED_STEP = 5;

esp_err_t motor_auto_process(){
    motor_auto = true;
    //motor_auto_count = 0;
    for (int i=0; i < 10; i++) {
        if (!motor_auto) break;
        motor_forward();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        motor_coast();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (!motor_auto) break;
        motor_reverse();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        motor_coast();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    return ESP_OK;
}

esp_err_t motor_auto_stop(){
    motor_auto = false;
    return ESP_OK;
}

esp_err_t motor_forward(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_reverse(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_brake(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Brake, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_coast(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Coast, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_speed_up(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    int max_speed_temp = max_speed + SPEED_STEP;
    max_speed = (max_speed_temp <= 100) ? max_speed_temp : 100;
    if (state == M_Forward)
        xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward_Starting, eSetValueWithOverwrite);
    else if (state == M_Reverse)
        xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse_Starting, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_speed_down(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    int max_speed_temp = max_speed - SPEED_STEP;
    max_speed = (max_speed_temp >= 0) ? max_speed_temp : 0;
    if (state == M_Forward)
        xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward_Starting, eSetValueWithOverwrite);
    else if (state == M_Reverse)
        xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse_Starting, eSetValueWithOverwrite);
    return ESP_OK;
}

void motor_task(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = PERIOD / portTICK_PERIOD_MS;
    int speed;
    motor_task_handle = xTaskGetCurrentTaskHandle();
    enum motor_state next_state;

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        counter++;

        //get and don't clear next motor state
        next_state = ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, 0);
        //clear pending state
        xTaskNotifyStateClearIndexed(motor_task_handle, notify_index);

        switch(state) {
            case M_Idle:
                //wait for motor command
                if (next_state == M_Idle){
                    uint32_t temp_state;
                    xTaskNotifyWaitIndexed(notify_index, 0, 0, &temp_state, portMAX_DELAY);
                    //type conversion
                    next_state = temp_state;
                    //update wake timer
                    xLastWakeTime += (xTaskGetTickCount() - xLastWakeTime) / xPeriod * xPeriod;
                }
                //handle commands
                if (next_state == M_Forward){
                    state = M_Forward_Starting;
                    DRV8871_set_speed(0);
                    DRV8871_forward_brake();
                    counter = 0;
                }
                else if (next_state == M_Reverse){
                    state = M_Reverse_Starting;
                    DRV8871_set_speed(0);
                    DRV8871_reverse_brake();
                    counter = 0;
                }
                //clearing command
                ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                break;
            case M_Forward_Starting:
                if (next_state == M_Brake){
                    state = M_Brake;
                    DRV8871_brake();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Coast) {
                    state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Reverse) {
                    //coast and reverse
                    state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //keeping command
                } else {
                    speed = DRV8871_get_speed();
                    if (speed - 100 * PERIOD / START_TIME > max_speed){
                        //too fast, speed down
                        speed = speed - 100 * PERIOD / START_TIME;
                    } else if (speed + 100 * PERIOD / START_TIME < max_speed) {
                        //too slow, speed up
                        speed = speed + 100 * PERIOD / START_TIME;
                    } else {
                        speed = max_speed;
                        state = M_Forward;
                    }
                    DRV8871_set_speed(speed);
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                }
                break;
            case M_Reverse_Starting:
                if (next_state == M_Brake){
                    state = M_Brake;
                    DRV8871_brake();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Coast) {
                    state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Forward) {
                    //coast and forward
                    state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //keeping command
                } else {
                    speed = DRV8871_get_speed();
                    if (speed - 100 * PERIOD / START_TIME > max_speed){
                        //too fast, speed down
                        speed = speed - 100 * PERIOD / START_TIME;
                    } else if (speed + 100 * PERIOD / START_TIME < max_speed) {
                        //too slow, speed up
                        speed = speed + 100 * PERIOD / START_TIME;
                    } else {
                        speed = max_speed;
                        state = M_Reverse;
                    }
                    DRV8871_set_speed(speed);
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                }
                break;
            case M_Forward:
                if (next_state == M_Forward_Starting){
                    //start changing speed
                    state = M_Forward_Starting;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Brake) {
                    state = M_Brake;
                    DRV8871_brake();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Coast) {
                    state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Reverse){
                    //coast and reverse
                    state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //keeping command
                } else {
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                }
                break;
            case M_Reverse:
                if (next_state == M_Reverse_Starting){
                    //start changing speed
                    state = M_Reverse_Starting;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Brake) {
                    state = M_Brake;
                    DRV8871_brake();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Coast) {
                    state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Forward){
                    //coast and forward
                    state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //keeping command
                } else {
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                }
                break;
            case M_Brake:
                //don't clear comand until braking for enough time
                if (counter >= BRAKE_TIME / PERIOD) {
                    state = M_Idle;
                    DRV8871_coast();
                    counter = 0;
                }
                break;
            case M_Coast:
                //don't clear comand until coasting for enough time
                if (counter >= BRAKE_TIME / PERIOD) {
                    state = M_Idle;
                    DRV8871_coast();
                    counter = 0;
                }
                break;
        }

    }

}
