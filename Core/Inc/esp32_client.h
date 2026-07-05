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
int ESP32_MqttConnect(void);
int ESP32_MqttSubscribeCmd(void);
int ESP32_MqttPublish(const char *topic, const char *payload);
/* Returns 1 when a subscribed message is parsed, 0 on no message, -1 on disconnect/error. */
int ESP32_MqttPoll(char *topic,
                   uint16_t topic_size,
                   char *payload,
                   uint16_t payload_size,
                   uint32_t timeout_ms);

#endif /* __ESP32_CLIENT_H */
