/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for FreeRTOS applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

#include "app_ui.h"
#include "app_config_private.h"
#include "esp32_client.h"
#include "rtc.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define WEATHER_UPDATE_INTERVAL_MS  600000U
/* ESP-AT MQTT receive is asynchronous; stay in UART receive most of the time. */
#define MQTT_POLL_INTERVAL_MS       1U
#define MQTT_POLL_TIMEOUT_MS        500U
#define WIFI_CHECK_INTERVAL_MS      10000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for KeyTask */
osThreadId_t KeyTaskHandle;
const osThreadAttr_t KeyTask_attributes = {
  .name = "KeyTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for UiTask */
osThreadId_t UiTaskHandle;
const osThreadAttr_t UiTask_attributes = {
  .name = "UiTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TimeTask */
osThreadId_t TimeTaskHandle;
const osThreadAttr_t TimeTask_attributes = {
  .name = "TimeTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for Esp32Task */
osThreadId_t Esp32TaskHandle;
const osThreadAttr_t Esp32Task_attributes = {
  .name = "Esp32Task",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for uiQueue */
osMessageQueueId_t uiQueueHandle;
const osMessageQueueAttr_t uiQueue_attributes = {
  .name = "uiQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static int RTC_SetDateTime(const RTC_DateTypeDef *date, const RTC_TimeTypeDef *time);
static int ParseISOTime(const char *str, RTC_DateTypeDef *date, RTC_TimeTypeDef *time);
static int RTC_SetFromWeatherTime(const char *time_str);
static void Mqtt_HandleCmd(const char *payload);
/* USER CODE END FunctionPrototypes */

void StartKeyTask(void *argument);
void StartUiTask(void *argument);
void StartTimeTask(void *argument);
void StartEsp32Task(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  AppUi_Init();
  ESP32_ClientInit();
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of uiQueue */
  uiQueueHandle = osMessageQueueNew (8, sizeof(uint32_t), &uiQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  AppUi_SetMessageQueue(uiQueueHandle);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of KeyTask */
  KeyTaskHandle = osThreadNew(StartKeyTask, NULL, &KeyTask_attributes);

  /* creation of UiTask */
  UiTaskHandle = osThreadNew(StartUiTask, NULL, &UiTask_attributes);

  /* creation of TimeTask */
  TimeTaskHandle = osThreadNew(StartTimeTask, NULL, &TimeTask_attributes);

  /* creation of Esp32Task */
  Esp32TaskHandle = osThreadNew(StartEsp32Task, NULL, &Esp32Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartKeyTask */
/**
  * @brief  Function implementing the KeyTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartKeyTask */
void StartKeyTask(void *argument)
{
  /* USER CODE BEGIN StartKeyTask */
  uint32_t msg;
  uint8_t k1_last = GPIO_PIN_SET;
  uint8_t k2_last = GPIO_PIN_SET;
  uint8_t k3_last = GPIO_PIN_SET;
  uint8_t k4_last = GPIO_PIN_SET;

  for (;;)
  {
    uint8_t k1 = HAL_GPIO_ReadPin(KEY_RIGHT_GPIO_Port, KEY_RIGHT_Pin);
    uint8_t k2 = HAL_GPIO_ReadPin(KEY_LEFT_GPIO_Port, KEY_LEFT_Pin);
    uint8_t k3 = HAL_GPIO_ReadPin(KEY_OK_GPIO_Port, KEY_OK_Pin);
    uint8_t k4 = HAL_GPIO_ReadPin(KEY_BACK_GPIO_Port, KEY_BACK_Pin);

    if (k1 == GPIO_PIN_RESET && k1_last == GPIO_PIN_SET)
    {
      osDelay(20);
      if (HAL_GPIO_ReadPin(KEY_RIGHT_GPIO_Port, KEY_RIGHT_Pin) == GPIO_PIN_RESET)
      {
        msg = UI_MSG_RIGHT;
        osMessageQueuePut(uiQueueHandle, &msg, 0, 0);
      }
    }

    if (k2 == GPIO_PIN_RESET && k2_last == GPIO_PIN_SET)
    {
      osDelay(20);
      if (HAL_GPIO_ReadPin(KEY_LEFT_GPIO_Port, KEY_LEFT_Pin) == GPIO_PIN_RESET)
      {
        msg = UI_MSG_LEFT;
        osMessageQueuePut(uiQueueHandle, &msg, 0, 0);
      }
    }

    if (k3 == GPIO_PIN_RESET && k3_last == GPIO_PIN_SET)
    {
      osDelay(20);
      if (HAL_GPIO_ReadPin(KEY_OK_GPIO_Port, KEY_OK_Pin) == GPIO_PIN_RESET)
      {
        msg = UI_MSG_OK;
        osMessageQueuePut(uiQueueHandle, &msg, 0, 0);
      }
    }

    if (k4 == GPIO_PIN_RESET && k4_last == GPIO_PIN_SET)
    {
      osDelay(20);
      if (HAL_GPIO_ReadPin(KEY_BACK_GPIO_Port, KEY_BACK_Pin) == GPIO_PIN_RESET)
      {
        msg = UI_MSG_BACK;
        osMessageQueuePut(uiQueueHandle, &msg, 0, 0);
      }
    }

    k1_last = k1;
    k2_last = k2;
    k3_last = k3;
    k4_last = k4;

    osDelay(20);
  }
  /* USER CODE END StartKeyTask */
}

/* USER CODE BEGIN Header_StartUiTask */
/**
* @brief Function implementing the UiTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUiTask */
void StartUiTask(void *argument)
{
  /* USER CODE BEGIN StartUiTask */
  uint32_t msg;

  AppUi_DisplayInit();

  for (;;)
  {
    if (osMessageQueueGet(uiQueueHandle, &msg, NULL, osWaitForever) == osOK)
    {
      AppUi_HandleMessage(msg);
    }
  }
  /* USER CODE END StartUiTask */
}

/* USER CODE BEGIN Header_StartTimeTask */
/**
* @brief Function implementing the TimeTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTimeTask */
void StartTimeTask(void *argument)
{
  /* USER CODE BEGIN StartTimeTask */
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;
  char time[16];
  char date[20];

  for(;;)
  {
    if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK &&
        HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) == HAL_OK)
    {
      snprintf(time,
               sizeof(time),
               "%02d:%02d:%02d",
               sTime.Hours,
               sTime.Minutes,
               sTime.Seconds);

      snprintf(date,
               sizeof(date),
               "20%02d-%02d-%02d",
               sDate.Year,
               sDate.Month,
               sDate.Date);

      AppUi_ModelSetTimeDate(time, date);
      AppUi_ModelSetRtcOk(1);
      AppUi_PostMessage(UI_MSG_TIME_UPDATE);
    }

    osDelay(1000);
  }
  /* USER CODE END StartTimeTask */
}

/* USER CODE BEGIN Header_StartEsp32Task */
/**
* @brief Function implementing the Esp32Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartEsp32Task */
void StartEsp32Task(void *argument)
{
  /* USER CODE BEGIN StartEsp32Task */
  WeatherNow weather_now;
  ForecastDay forecast[3];
  WeatherDailyResult daily_result;
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;
  uint8_t wifi_ok = 0;
  uint8_t mqtt_ok = 0;
  uint8_t sntp_tried = 0;
  uint32_t last_weather_tick = 0;
  uint32_t last_wifi_check_tick = 0;
  uint32_t now_tick;
  int mqtt_poll_result;
  char mqtt_topic[128];
  char mqtt_payload[256];

  osDelay(3000);

  for(;;)
  {
    now_tick = HAL_GetTick();

    /* Do not query WiFi every 500 ms; AT+CWJAP? is much slower than MQTT poll. */
    if (!wifi_ok || (now_tick - last_wifi_check_tick >= WIFI_CHECK_INTERVAL_MS))
    {
      last_wifi_check_tick = now_tick;
      if (ESP32_CheckWiFi())
      {
        wifi_ok = 1;
      }
      else
      {
        wifi_ok = ESP32_ConnectWiFi() ? 1U : 0U;
        mqtt_ok = 0;
        sntp_tried = 0;
      }
    }

    AppUi_ModelSetNetwork(wifi_ok);
    AppUi_PostMessage(UI_MSG_NETWORK_CHANGE);
    if (!wifi_ok)
    {
      AppUi_ModelSetNtpOk(0);
      AppUi_ModelSetMqttOk(0);
      osDelay(3000);
      continue;
    }

    if (!mqtt_ok)
    {
      mqtt_ok = ESP32_MqttConnect() ? 1U : 0U;
      if (mqtt_ok)
      {
        if (!ESP32_MqttSubscribeCmd())
        {
          mqtt_ok = 0;
        }
        else
        {
          /* Publish a small state payload; topic and payload are different things. */
          ESP32_MqttPublish(MQTT_TOPIC_STATE, "wifi=1,mqtt=1");
        }
      }
      AppUi_ModelSetMqttOk(mqtt_ok);
      AppUi_PostMessage(UI_MSG_NETWORK_CHANGE);
    }

    if (!sntp_tried)
    {
      sntp_tried = 1;
      if (ESP32_SyncTimeBySNTP(&sDate, &sTime))
      {
        AppUi_ModelSetNtpOk(RTC_SetDateTime(&sDate, &sTime) ? 1U : 0U);
      }
      else
      {
        AppUi_ModelSetNtpOk(0);
      }
    }

    /* First boot updates immediately; after that, update every 10 minutes. */
    if (last_weather_tick == 0U ||
        (now_tick - last_weather_tick >= WEATHER_UPDATE_INTERVAL_MS) ||
        AppUi_IsWeatherForceUpdateRequested())
    {
      if (ESP32_GetWeather(&weather_now))
      {
        AppUi_ModelSetApiNowOk(1);
        if (weather_now.update_time[0] != '\0' &&
            strcmp(weather_now.update_time, "--") != 0)
        {
          RTC_SetFromWeatherTime(weather_now.update_time);
        }
        AppUi_ModelUpdateWeather(&weather_now);

        if (mqtt_ok)
        {
          /* This publishes payload "weather_now_ok" to MQTT_TOPIC_WEATHER. */
          ESP32_MqttPublish(MQTT_TOPIC_WEATHER, "weather_now_ok");
        }
      }
      else
      {
        AppUi_ModelSetApiNowOk(0);
        AppUi_ModelSetParseNowOk(0);
        AppUi_PostMessage(UI_MSG_WEATHER_UPDATE);
      }

      osDelay(1000);

      daily_result = ESP32_GetDailyForecast(forecast);
      if (daily_result == WEATHER_DAILY_PARSE_OK)
      {
        AppUi_ModelSetApiDailyOk(1);
        AppUi_ModelUpdateForecast(forecast);
      }
      else if (daily_result == WEATHER_DAILY_PARSE_FAIL)
      {
        AppUi_ModelSetApiDailyOk(1);
        AppUi_ModelSetParseDailyOk(0);
        AppUi_PostMessage(UI_MSG_WEATHER_UPDATE);
      }
      else
      {
        AppUi_ModelSetApiDailyOk(0);
        AppUi_ModelSetParseDailyOk(0);
        AppUi_PostMessage(UI_MSG_WEATHER_UPDATE);
      }

      AppUi_ClearWeatherForceUpdate();
      last_weather_tick = HAL_GetTick();
    }

    if (mqtt_ok)
    {
      /* Return value: 1 means command received, 0 means no data, -1 means disconnected. */
      mqtt_poll_result = ESP32_MqttPoll(mqtt_topic,
                                        sizeof(mqtt_topic),
                                        mqtt_payload,
                                        sizeof(mqtt_payload),
                                        MQTT_POLL_TIMEOUT_MS);
      if (mqtt_poll_result > 0)
      {
        Mqtt_HandleCmd(mqtt_payload);
      }
      else if (mqtt_poll_result < 0)
      {
        mqtt_ok = 0;
        AppUi_ModelSetMqttOk(0);
        AppUi_PostMessage(UI_MSG_NETWORK_CHANGE);
      }
    }

    osDelay(MQTT_POLL_INTERVAL_MS);
  }
  /* USER CODE END StartEsp32Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static int RTC_SetDateTime(const RTC_DateTypeDef *date, const RTC_TimeTypeDef *time)
{
  RTC_DateTypeDef date_copy;
  RTC_TimeTypeDef time_copy;

  if (date == NULL || time == NULL)
  {
    return 0;
  }

  date_copy = *date;
  time_copy = *time;

  if (HAL_RTC_SetTime(&hrtc, &time_copy, RTC_FORMAT_BIN) != HAL_OK)
  {
    return 0;
  }

  if (HAL_RTC_SetDate(&hrtc, &date_copy, RTC_FORMAT_BIN) != HAL_OK)
  {
    return 0;
  }

  AppUi_ModelSetRtcOk(1);
  return 1;
}

static int ParseISOTime(const char *str, RTC_DateTypeDef *date, RTC_TimeTypeDef *time)
{
  int year;
  int month;
  int day;
  int hour;
  int minute;

  if (str == NULL || date == NULL || time == NULL)
  {
    return 0;
  }

  if (sscanf(str, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute) != 5)
  {
    return 0;
  }

  if (year < 2000 || year > 2099 ||
      month < 1 || month > 12 ||
      day < 1 || day > 31 ||
      hour < 0 || hour > 23 ||
      minute < 0 || minute > 59)
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
  time->Seconds = 0;
  time->DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time->StoreOperation = RTC_STOREOPERATION_RESET;

  return 1;
}

static int RTC_SetFromWeatherTime(const char *time_str)
{
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;

  if (!ParseISOTime(time_str, &sDate, &sTime))
  {
    return 0;
  }

  return RTC_SetDateTime(&sDate, &sTime);
}

static void Mqtt_HandleCmd(const char *payload)
{
  if (payload == NULL)
  {
    return;
  }

  /* MQTT commands reuse the same UI queue as physical keys. */
  if (strstr(payload, "page_next") != NULL)
  {
    AppUi_PostMessage(UI_MSG_RIGHT);
    ESP32_MqttPublish(MQTT_TOPIC_ACK, "page_next_ok");
  }
  else if (strstr(payload, "page_prev") != NULL)
  {
    AppUi_PostMessage(UI_MSG_LEFT);
    ESP32_MqttPublish(MQTT_TOPIC_ACK, "page_prev_ok");
  }
  else if (strstr(payload, "page_home") != NULL)
  {
    AppUi_PostMessage(UI_MSG_BACK);
    ESP32_MqttPublish(MQTT_TOPIC_ACK, "page_home_ok");
  }
  else if (strstr(payload, "weather_refresh") != NULL)
  {
    AppUi_PostMessage(UI_MSG_OK);
    ESP32_MqttPublish(MQTT_TOPIC_ACK, "weather_refresh_ok");
  }
}
/* USER CODE END Application */
