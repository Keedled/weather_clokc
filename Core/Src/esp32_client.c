#include "esp32_client.h"

#include <stdio.h>
#include <string.h>

#include "app_config_private.h"
#include "cmsis_os.h"
#include "main.h"
#include "usart.h"
#include "weather_parser.h"

#define WEATHER_NOW_HTTP_CMD "AT+HTTPCGET=\"http://api.seniverse.com/v3/weather/now.json?key=" WEATHER_API_KEY "&location=" WEATHER_CITY "&language=en&unit=c\",2048,2048,30000\r\n"
#define WEATHER_DAILY_HTTP_CMD "AT+HTTPCGET=\"http://api.seniverse.com/v3/weather/daily.json?key=" WEATHER_API_KEY "&location=" WEATHER_CITY "&language=en&unit=c&start=0&days=3\",4096,4096,30000\r\n"

static uint8_t espRxBuf[8192];
static osMutexId_t espMutexHandle;

static const osMutexAttr_t espMutex_attributes = {
  .name = "espMutex"
};

static int ESP_Lock(uint32_t timeout_ms);
static void ESP_Unlock(void);
static void ESP_ClearRxBuf(void);
static int BufferEndsWith(const char *buf, uint16_t len, const char *suffix);
static int ESP_ReadUntil(const char *expect, uint32_t timeout_ms);
static int ESP_SendCmdRaw(const char *cmd, const char *expect, uint32_t timeout_ms);
static int ESP_ParseSntpTime(const char *src, RTC_DateTypeDef *date, RTC_TimeTypeDef *time);

int ESP32_ClientInit(void)
{
  espMutexHandle = osMutexNew(&espMutex_attributes);
  return espMutexHandle != NULL;
}

static int ESP_Lock(uint32_t timeout_ms)
{
  if (espMutexHandle == NULL)
  {
    return 0;
  }

  return osMutexAcquire(espMutexHandle, timeout_ms) == osOK;
}

static void ESP_Unlock(void)
{
  if (espMutexHandle != NULL)
  {
    osMutexRelease(espMutexHandle);
  }
}

static void ESP_ClearRxBuf(void)
{
  memset(espRxBuf, 0, sizeof(espRxBuf));
}

static int BufferEndsWith(const char *buf, uint16_t len, const char *suffix)
{
  size_t suffix_len;

  if (buf == NULL || suffix == NULL)
  {
    return 0;
  }

  suffix_len = strlen(suffix);
  if (suffix_len == 0 || len < suffix_len)
  {
    return 0;
  }

  return memcmp(buf + len - suffix_len, suffix, suffix_len) == 0;
}

static int ESP_ReadUntil(const char *expect, uint32_t timeout_ms)
{
  uint8_t ch;
  uint16_t len = (uint16_t)strlen((char *)espRxBuf);
  uint32_t start_tick = HAL_GetTick();

  while ((HAL_GetTick() - start_tick) < timeout_ms)
  {
    if (HAL_UART_Receive(&huart1, &ch, 1, 20) == HAL_OK)
    {
      if (len < sizeof(espRxBuf) - 1)
      {
        espRxBuf[len++] = ch;
        espRxBuf[len] = '\0';
      }

      if (expect != NULL && BufferEndsWith((char *)espRxBuf, len, expect))
      {
        return 1;
      }

      if (BufferEndsWith((char *)espRxBuf, len, "ERROR\r\n") ||
          BufferEndsWith((char *)espRxBuf, len, "FAIL\r\n") ||
          BufferEndsWith((char *)espRxBuf, len, "busy p...\r\n"))
      {
        return 0;
      }
    }
  }

  return 0;
}

static int ESP_SendCmdRaw(const char *cmd, const char *expect, uint32_t timeout_ms)
{
  ESP_ClearRxBuf();

  if (HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000) != HAL_OK)
  {
    return 0;
  }

  return ESP_ReadUntil(expect, timeout_ms);
}

int ESP32_CheckWiFi(void)
{
  int result;

  if (!ESP_Lock(4000))
  {
    return 0;
  }

  result = ESP_SendCmdRaw("AT+CWJAP?\r\n", "OK", 3000) &&
           strstr((char *)espRxBuf, "+CWJAP:") != NULL;

  ESP_Unlock();
  return result;
}

int ESP32_ConnectWiFi(void)
{
  char cmd[160];
  int result = 0;

  if (!ESP_Lock(25000))
  {
    return 0;
  }

  if (!ESP_SendCmdRaw("AT\r\n", "OK", 1000))
  {
    goto done;
  }

  ESP_SendCmdRaw("ATE0\r\n", "OK", 1000);

  if (!ESP_SendCmdRaw("AT+CWMODE=1\r\n", "OK", 1000))
  {
    goto done;
  }

  ESP_SendCmdRaw("AT+CIPMUX=0\r\n", "OK", 1000);

  if (ESP_SendCmdRaw("AT+CWJAP?\r\n", "OK", 3000) &&
      strstr((char *)espRxBuf, "+CWJAP:") != NULL &&
      ESP_SendCmdRaw("AT+CIFSR\r\n", "OK", 3000) &&
      strstr((char *)espRxBuf, "STAIP") != NULL)
  {
    result = 1;
    goto done;
  }

  snprintf(cmd,
           sizeof(cmd),
           "AT+CWJAP=\"%s\",\"%s\"\r\n",
           WIFI_SSID,
           WIFI_PASSWORD);

  if (!ESP_SendCmdRaw(cmd, "OK", 20000))
  {
    goto done;
  }

  result = ESP_SendCmdRaw("AT+CIFSR\r\n", "OK", 3000) &&
           strstr((char *)espRxBuf, "STAIP") != NULL;

done:
  ESP_Unlock();
  return result;
}

int ESP32_SyncTimeBySNTP(RTC_DateTypeDef *date, RTC_TimeTypeDef *time)
{
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;
  int result = 0;
  int retry;

  if (date == NULL || time == NULL)
  {
    return 0;
  }

  if (!ESP_Lock(20000))
  {
    return 0;
  }

  if (!ESP_SendCmdRaw("AT+CIPSNTPCFG=1,8,\"cn.ntp.org.cn\",\"ntp.sjtu.edu.cn\",\"pool.ntp.org\"\r\n",
                      "OK",
                      3000))
  {
    goto done;
  }

  for (retry = 0; retry < 5; retry++)
  {
    osDelay(2000);

    if (ESP_SendCmdRaw("AT+CIPSNTPTIME?\r\n", "OK", 5000) &&
        ESP_ParseSntpTime((char *)espRxBuf, &sDate, &sTime))
    {
      *date = sDate;
      *time = sTime;
      result = 1;
      break;
    }
  }

done:
  ESP_Unlock();
  return result;
}

int ESP32_GetWeather(WeatherNow *out)
{
  const char http_cmd[] = WEATHER_NOW_HTTP_CMD;
  int result = 0;

  if (out == NULL)
  {
    return 0;
  }

  if (!ESP_Lock(32000))
  {
    return 0;
  }

  if (!ESP_SendCmdRaw(http_cmd, "\r\nOK\r\n", 30000))
  {
    goto done;
  }

  if (strstr((char *)espRxBuf, "\"results\"") == NULL ||
      strstr((char *)espRxBuf, "\"now\"") == NULL)
  {
    goto done;
  }

  result = Weather_ParseNowResponse((char *)espRxBuf, out);

done:
  ESP_Unlock();
  return result;
}

WeatherDailyResult ESP32_GetDailyForecast(ForecastDay out[3])
{
  const char http_cmd[] = WEATHER_DAILY_HTTP_CMD;
  WeatherDailyResult result = WEATHER_DAILY_HTTP_FAIL;
  int parse_ok;

  if (out == NULL)
  {
    return WEATHER_DAILY_HTTP_FAIL;
  }

  if (!ESP_Lock(32000))
  {
    return WEATHER_DAILY_HTTP_FAIL;
  }

  if (!ESP_SendCmdRaw(http_cmd, "\r\nOK\r\n", 30000))
  {
    goto done;
  }

  if (strstr((char *)espRxBuf, "\"results\"") == NULL ||
      strstr((char *)espRxBuf, "\"daily\"") == NULL)
  {
    goto done;
  }

  parse_ok = Weather_ParseDailyResponse((char *)espRxBuf, out);
  result = parse_ok ? WEATHER_DAILY_PARSE_OK : WEATHER_DAILY_PARSE_FAIL;

done:
  ESP_Unlock();
  return result;
}

static int ESP_ParseSntpTime(const char *src, RTC_DateTypeDef *date, RTC_TimeTypeDef *time)
{
  static const char *months[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  char month_str[4];
  int month = 0;
  int day;
  int hour;
  int minute;
  int second;
  int year;
  int i;
  const char *p;

  if (src == NULL || date == NULL || time == NULL)
  {
    return 0;
  }

  p = strchr(src, ':');
  if (p == NULL)
  {
    return 0;
  }

  p++;
  while (*p == ' ')
  {
    p++;
  }

  if (sscanf(p, "%*3s %3s %d %d:%d:%d %d",
             month_str,
             &day,
             &hour,
             &minute,
             &second,
             &year) != 6)
  {
    return 0;
  }

  month_str[3] = '\0';
  for (i = 0; i < 12; i++)
  {
    if (strcmp(month_str, months[i]) == 0)
    {
      month = i + 1;
      break;
    }
  }

  if (month == 0 ||
      year < 2000 || year > 2099 ||
      day < 1 || day > 31 ||
      hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 ||
      second < 0 || second > 59)
  {
    return 0;
  }

  memset(date, 0, sizeof(*date));
  memset(time, 0, sizeof(*time));
  date->Year = (uint8_t)(year - 2000);
  date->Month = (uint8_t)month;
  date->Date = (uint8_t)day;
  date->WeekDay = RTC_WEEKDAY_MONDAY;

  time->Hours = (uint8_t)hour;
  time->Minutes = (uint8_t)minute;
  time->Seconds = (uint8_t)second;
  time->DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time->StoreOperation = RTC_STOREOPERATION_RESET;

  return 1;
}
