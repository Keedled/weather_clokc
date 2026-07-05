#ifndef __APP_TYPES_H
#define __APP_TYPES_H

#include <stdint.h>

typedef enum {
    UI_MSG_NONE = 0,
    UI_MSG_LEFT,
    UI_MSG_RIGHT,
    UI_MSG_OK,
    UI_MSG_BACK,
    UI_MSG_TIME_UPDATE,
    UI_MSG_WEATHER_UPDATE,
    UI_MSG_NETWORK_CHANGE
} UiMsg;

typedef enum {
    WEATHER_DAILY_HTTP_FAIL = 0,
    WEATHER_DAILY_PARSE_OK,
    WEATHER_DAILY_PARSE_FAIL
} WeatherDailyResult;

typedef struct {
    char city[24];
    char weather[24];
    char temperature[12];
    char humidity[12];
    char wind_dir[24];
    char wind_scale[12];
    char update_time[24];
    uint8_t valid;
} WeatherNow;

typedef struct {
    char date[16];
    char text_day[24];
    char text_night[24];
    char high[8];
    char low[8];
    char wind_direction[24];
    char wind_scale[12];
} ForecastDay;

typedef struct {
    char time[16];
    char date[20];
    WeatherNow weather_now;
    ForecastDay forecast[3];
    uint8_t wifi_ok;
    uint8_t api_now_ok;
    uint8_t api_daily_ok;
    uint8_t parse_now_ok;
    uint8_t parse_daily_ok;
    uint8_t rtc_ok;
    uint8_t ntp_ok;
} UiModel;

#endif /* __APP_TYPES_H */
