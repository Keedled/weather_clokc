#include "esp32_client.h"

#include <stdio.h>
#include <stdlib.h>
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
static int ESP_ParseMqttSubRecv(const char *buf,
                                char *topic,
                                uint16_t topic_size,
                                char *payload,
                                uint16_t payload_size);

/*
 * Initialize the ESP32 client module.
 * Creates the mutex used to serialize all USART1 AT command traffic.
 */
int ESP32_ClientInit(void)
{
  espMutexHandle = osMutexNew(&espMutex_attributes);
  return espMutexHandle != NULL;
}

/*
 * Take exclusive ownership of the ESP32 AT command channel.
 * Every public ESP32_* function locks before touching USART1/espRxBuf.
 */
static int ESP_Lock(uint32_t timeout_ms)
{
  if (espMutexHandle == NULL)
  {
    return 0;
  }

  return osMutexAcquire(espMutexHandle, timeout_ms) == osOK;
}

/*
 * Release the ESP32 AT command channel.
 */
static void ESP_Unlock(void)
{
  if (espMutexHandle != NULL)
  {
    osMutexRelease(espMutexHandle);
  }
}

/*
 * Clear the shared AT response buffer before sending a new command.
 */
static void ESP_ClearRxBuf(void)
{
  memset(espRxBuf, 0, sizeof(espRxBuf));
}

/*
 * Check whether the current response buffer ends with a target suffix.
 * Used to detect terminal strings like "OK\r\n" and "ERROR\r\n".
 */
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

/*
 * Receive bytes from USART1 until the expected suffix appears or timeout expires.
 * Returns 1 on expected response, 0 on timeout or ESP-AT error response.
 */
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

/*
 * Send a raw AT command and wait for its terminal response.
 * The received response remains in espRxBuf for the caller to inspect.
 */
static int ESP_SendCmdRaw(const char *cmd, const char *expect, uint32_t timeout_ms)
{
  ESP_ClearRxBuf();

  if (HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000) != HAL_OK)
  {
    return 0;
  }

  return ESP_ReadUntil(expect, timeout_ms);
}

/*
 * Query whether ESP32 is already associated with an AP.
 * Returns 1 only when AT+CWJAP? succeeds and reports a joined AP.
 */
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

/*
 * Configure station mode and connect ESP32 to the configured WiFi network.
 * If already connected and has a STA IP, it returns success without reconnecting.
 */
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

/*
 * Ask ESP32 SNTP for network time.
 * On success, fills date/time; caller decides whether to write them into RTC.
 */
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

/*
 * Fetch current weather through ESP32 HTTP client and parse it into WeatherNow.
 * Returns 1 only when HTTP response and weather parsing both succeed.
 */
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

/*
 * Fetch 3-day forecast through ESP32 HTTP client.
 * Return value distinguishes HTTP failure from JSON/field parsing failure.
 */
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

/*
 * Connect ESP32 MQTT client to the configured broker.
 * It first clears stale ESP32-side MQTT state, which matters after STM32-only reset.
 */
int ESP32_MqttConnect(void)
{
  char cmd[256];
  int result = 0;

  if (!ESP_Lock(10000))
  {
    return 0;
  }

  /* STM32 may reset while ESP32 keeps its old MQTT state; clear it first. */
  ESP_SendCmdRaw("AT+MQTTDISCONN=0\r\n", "OK", 1000);
  ESP_SendCmdRaw("AT+MQTTCLEAN=0\r\n", "OK", 1000);

  /* Link 0, MQTT over TCP, no TLS certificates. Empty user/password are OK. */
  snprintf(cmd,
           sizeof(cmd),
           "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
           MQTT_CLIENT_ID,
           MQTT_USERNAME,
           MQTT_PASSWORD);

  if (!ESP_SendCmdRaw(cmd, "OK", 3000))
  {
    goto done;
  }

  snprintf(cmd,
           sizeof(cmd),
           "AT+MQTTCONN=0,\"%s\",%d,1\r\n",
           MQTT_HOST,
           MQTT_PORT);

  if (!ESP_SendCmdRaw(cmd, "OK", 8000))
  {
    goto done;
  }

  result = 1;

done:
  ESP_Unlock();
  return result;
}

/*
 * Subscribe to MQTT_TOPIC_CMD, the command topic used by the remote controller.
 */
int ESP32_MqttSubscribeCmd(void)
{
  char cmd[192];
  int result;

  if (!ESP_Lock(5000))
  {
    return 0;
  }

  /* Subscribe to the command topic; payloads are parsed in StartEsp32Task. */
  snprintf(cmd,
           sizeof(cmd),
           "AT+MQTTSUB=0,\"%s\",1\r\n",
           MQTT_TOPIC_CMD);

  result = ESP_SendCmdRaw(cmd, "OK", 3000);

  ESP_Unlock();
  return result;
}

/*
 * Publish a short MQTT payload to the given topic through AT+MQTTPUB.
 * Topic is the address; payload is the message content.
 */
int ESP32_MqttPublish(const char *topic, const char *payload)
{
  char cmd[384];
  int result;

  if (topic == NULL || payload == NULL)
  {
    return 0;
  }

  if (!ESP_Lock(5000))
  {
    return 0;
  }

  /* Keep payloads short/plain here; JSON quotes should use MQTTPUBRAW later. */
  snprintf(cmd,
           sizeof(cmd),
           "AT+MQTTPUB=0,\"%s\",\"%s\",1,0\r\n",
           topic,
           payload);

  result = ESP_SendCmdRaw(cmd, "OK", 3000);

  ESP_Unlock();
  return result;
}

/*
 * Poll USART1 briefly for asynchronous MQTT input from ESP-AT.
 * Returns 1 when topic/payload is parsed, 0 when no message arrives, -1 on error/disconnect.
 */
int ESP32_MqttPoll(char *topic,
                   uint16_t topic_size,
                   char *payload,
                   uint16_t payload_size,
                   uint32_t timeout_ms)
{
  uint8_t ch;
  uint16_t len = 0;
  uint32_t start_tick = HAL_GetTick();

  if (topic == NULL || topic_size == 0 ||
      payload == NULL || payload_size == 0)
  {
    return -1;
  }

  topic[0] = '\0';
  payload[0] = '\0';

  if (!ESP_Lock(timeout_ms + 100U))
  {
    return -1;
  }

  /* Short polling keeps MQTT responsive during the 10-minute weather interval. */
  ESP_ClearRxBuf();

  while ((HAL_GetTick() - start_tick) < timeout_ms)
  {
    if (HAL_UART_Receive(&huart1, &ch, 1, 20) == HAL_OK)
    {
      if (len < sizeof(espRxBuf) - 1)
      {
        espRxBuf[len++] = ch;
        espRxBuf[len] = '\0';
      }

      if (strstr((char *)espRxBuf, "+MQTTDISCONNECTED") != NULL)
      {
        ESP_Unlock();
        return -1;
      }

      if (ESP_ParseMqttSubRecv((char *)espRxBuf,
                               topic,
                               topic_size,
                               payload,
                               payload_size))
      {
        ESP_Unlock();
        return 1;
      }
    }
  }

  ESP_Unlock();
  return 0;
}

/*
 * Parse ESP-AT subscription notifications:
 * +MQTTSUBRECV:0,"topic",len,payload
 */
static int ESP_ParseMqttSubRecv(const char *buf,
                                char *topic,
                                uint16_t topic_size,
                                char *payload,
                                uint16_t payload_size)
{
  const char *p;
  const char *t1;
  const char *t2;
  const char *comma;
  const char *payload_start;
  size_t topic_len;
  size_t available_len;
  int data_len;
  int copy_len;

  if (buf == NULL || topic == NULL || topic_size == 0 ||
      payload == NULL || payload_size == 0)
  {
    return 0;
  }

  p = strstr(buf, "+MQTTSUBRECV:");
  if (p == NULL)
  {
    return 0;
  }

  /* Format: +MQTTSUBRECV:0,"topic",len,payload */
  t1 = strchr(p, '"');
  if (t1 == NULL)
  {
    return 0;
  }

  t2 = strchr(t1 + 1, '"');
  if (t2 == NULL)
  {
    return 0;
  }

  topic_len = (size_t)(t2 - t1 - 1);
  if (topic_len >= topic_size)
  {
    topic_len = topic_size - 1;
  }

  memcpy(topic, t1 + 1, topic_len);
  topic[topic_len] = '\0';

  comma = strchr(t2 + 1, ',');
  if (comma == NULL)
  {
    return 0;
  }

  data_len = atoi(comma + 1);

  comma = strchr(comma + 1, ',');
  if (comma == NULL || data_len <= 0)
  {
    return 0;
  }

  payload_start = comma + 1;
  available_len = strlen(payload_start);

  /*
   * ESP32_MqttPoll receives one byte at a time. Do not report success until
   * the whole payload promised by ESP-AT has arrived, otherwise commands such
   * as {"cmd":"page_next"} can be parsed as an empty partial payload.
   */
  if (available_len < (size_t)data_len)
  {
    return 0;
  }

  copy_len = data_len;
  if (copy_len >= payload_size)
  {
    copy_len = payload_size - 1;
  }

  memcpy(payload, payload_start, (size_t)copy_len);
  payload[copy_len] = '\0';

  return 1;
}

/*
 * Parse the text returned by AT+CIPSNTPTIME? into STM32 RTC date/time structs.
 */
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
