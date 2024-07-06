#ifndef MOTOR_H
#define MOTOR_H

#include "esp_err.h"
enum motor_state {M_Idle=0, M_Forward, M_Reverse, M_Forward_Starting, M_Reverse_Starting, M_Brake, M_Coast};
extern enum motor_state motor_next_state;
esp_err_t motor_auto_process();
esp_err_t motor_auto_stop();
void motor_task(void *pvParameters);

esp_err_t motor_forward();
esp_err_t motor_reverse();
esp_err_t motor_brake();
esp_err_t motor_coast();
esp_err_t motor_speed_up();
esp_err_t motor_speed_down();

#endif
