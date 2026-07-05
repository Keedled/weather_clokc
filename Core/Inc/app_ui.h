#ifndef __APP_UI_H
#define __APP_UI_H

#include "app_types.h"
#include "cmsis_os.h"

int AppUi_Init(void);
void AppUi_SetMessageQueue(osMessageQueueId_t queue);
void AppUi_PostMessage(uint32_t msg);
void AppUi_DisplayInit(void);
void AppUi_HandleMessage(uint32_t msg);

uint8_t AppUi_IsWeatherForceUpdateRequested(void);
void AppUi_ClearWeatherForceUpdate(void);

void AppUi_ModelSetTimeDate(const char *time, const char *date);
void AppUi_ModelSetNetwork(uint8_t wifi_ok);
void AppUi_ModelSetApiNowOk(uint8_t api_ok);
void AppUi_ModelSetApiDailyOk(uint8_t api_ok);
void AppUi_ModelSetParseNowOk(uint8_t parse_ok);
void AppUi_ModelSetParseDailyOk(uint8_t parse_ok);
void AppUi_ModelSetRtcOk(uint8_t rtc_ok);
void AppUi_ModelSetNtpOk(uint8_t ntp_ok);
void AppUi_ModelSetMqttOk(uint8_t mqtt_ok);
void AppUi_ModelUpdateWeather(const WeatherNow *weather_now);
void AppUi_ModelUpdateForecast(const ForecastDay forecast[3]);

#endif /* __APP_UI_H */
