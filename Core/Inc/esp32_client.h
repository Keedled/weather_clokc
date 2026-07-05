#ifndef __ESP32_CLIENT_H
#define __ESP32_CLIENT_H

#include "app_types.h"
#include "rtc.h"

int ESP32_ClientInit(void);
int ESP32_CheckWiFi(void);
int ESP32_ConnectWiFi(void);
int ESP32_SyncTimeBySNTP(RTC_DateTypeDef *date, RTC_TimeTypeDef *time);
int ESP32_GetWeather(WeatherNow *out);
WeatherDailyResult ESP32_GetDailyForecast(ForecastDay out[3]);

#endif /* __ESP32_CLIENT_H */
