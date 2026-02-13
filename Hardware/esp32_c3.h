#ifndef __ESP32_C3_H  // 修改：更正头文件保护宏名称
#define __ESP32_C3_H

#include "MyUSART.h"
#include <string.h>
#include "Delay.h"
#define RX_BUF_SIZE 400

static char year[4] = "2026";

typedef struct Time
{
    char year[4];
    char month[3];
    char day[2];
    char week[3];
    char hour[2];
    char min[2]; 
}Time;

typedef struct Weather
{
    char weather[10];
    char temperature[5];
    char location[10];
}Weather;

typedef struct Network
{
    char state;
    char ssid[18];
}Network;

static char rx_buf[RX_BUF_SIZE] = {0};

void esp32_c3_init(void);

void esp32_c3_get_time(Time *time);
void esp32_c3_get_weather(Weather *weather);

void esp32_c3_get_network(Network *network);

#endif // 修改：更正头文件保护宏名称