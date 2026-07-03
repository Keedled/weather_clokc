/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "ST7735.h"
#include "rtc.h"
#include "usart.h"
#include "app_config_private.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum {
    PAGE_HOME = 0,
    PAGE_WEATHER,
    PAGE_FORECAST,
    PAGE_DEBUG,
    PAGE_MAX
} PageId;

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

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define WEATHER_NOW_HTTP_CMD "AT+HTTPCGET=\"http://api.seniverse.com/v3/weather/now.json?key=" WEATHER_API_KEY "&location=" WEATHER_CITY "&language=en&unit=c\",2048,2048,30000\r\n"
#define WEATHER_DAILY_PATH "/v3/weather/daily.json?key=" WEATHER_API_KEY "&location=" WEATHER_CITY "&language=en&unit=c&start=0&days=3"
#define WEATHER_DAILY_HOST "api.seniverse.com"
#define WEATHER_UPDATE_INTERVAL_MS  (10U * 60U * 1000U)
#define WEATHER_RETRY_INTERVAL_MS   30000U
#define WIFI_WAIT_INTERVAL_MS       5000U
#define WIFI_CHECK_INTERVAL_MS      10000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static PageId g_currentPage = PAGE_HOME;

static UiModel g_uiModel = {
    .time = "00:00:00",
    .date = "2026-07-03",
    .weather_now = {
        .city = "Beijing",
        .weather = "--",
        .temperature = "--C",
        .humidity = "--%",
        .wind_dir = "--",
        .wind_scale = "--",
        .update_time = "--",
        .valid = 0
    },
    .forecast = {
        {"--", "--", "--", "--", "--", "--", "--"},
        {"--", "--", "--", "--", "--", "--", "--"},
        {"--", "--", "--", "--", "--", "--", "--"}
    },
    .wifi_ok = 0,
    .api_now_ok = 0,
    .api_daily_ok = 0,
    .parse_now_ok = 0,
    .parse_daily_ok = 0,
    .rtc_ok = 0,
    .ntp_ok = 0
};

static volatile uint8_t g_weatherForceUpdate = 0;

static osMutexId_t espMutexHandle;
const osMutexAttr_t espMutex_attributes = {
  .name = "espMutex"
};

static osMutexId_t uiModelMutexHandle;
const osMutexAttr_t uiModelMutex_attributes = {
  .name = "uiModelMutex"
};
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
/* Definitions for NetworkTask */
osThreadId_t NetworkTaskHandle;
const osThreadAttr_t NetworkTask_attributes = {
  .name = "NetworkTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for WeatherTask */
osThreadId_t WeatherTaskHandle;
const osThreadAttr_t WeatherTask_attributes = {
  .name = "WeatherTask",
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
static int ESP_Lock(uint32_t timeout_ms);
static void ESP_Unlock(void);
static void ESP_ClearRxBuf(void);
static int ESP_ReadUntil(const char *expect, uint32_t timeout_ms);
static int ESP_SendCmdRaw(const char *cmd, const char *expect, uint32_t timeout_ms);
static int ESP_SendRawAndReadUntil(const char *data, const char *expect, uint32_t timeout_ms);
static int ESP_CheckWiFi(void);
static int ESP_ConnectWiFi(void);
static int ESP_SyncTimeBySNTP(void);
static int ESP_ParseSntpTime(const char *src, RTC_DateTypeDef *date, RTC_TimeTypeDef *time);
static int ESP_GetWeather(WeatherNow *out);
static WeatherDailyResult ESP_GetDailyForecast(ForecastDay out[3]);
static int ExtractString(const char *src, const char *begin, const char *end, char *out, size_t out_size);
static int ExtractStringRange(const char *start, const char *end_limit, const char *begin, const char *end, char *out, size_t out_size);
static int Weather_ParseResponse(WeatherNow *out);
static int Weather_ParseDailyResponse(ForecastDay out[3]);
static int ParseISOTime(const char *str, RTC_DateTypeDef *date, RTC_TimeTypeDef *time);
static int RTC_SetDateTime(const RTC_DateTypeDef *date, const RTC_TimeTypeDef *time);
static int RTC_SetFromWeatherTime(const char *time_str);
static void UI_PostMessage(uint32_t msg);
static int UI_ModelLock(uint32_t timeout_ms);
static void UI_ModelUnlock(void);
static void UI_CopyString(char *dst, size_t dst_size, const char *src);
static void UI_ModelSnapshot(UiModel *model);
static uint8_t UI_ModelGetWifiOk(void);
static void UI_ModelSetTimeDate(const char *time, const char *date);
static void UI_ModelSetNetwork(uint8_t wifi_ok);
static void UI_ModelSetApiNowOk(uint8_t api_ok);
static void UI_ModelSetApiDailyOk(uint8_t api_ok);
static void UI_ModelSetParseNowOk(uint8_t parse_ok);
static void UI_ModelSetParseDailyOk(uint8_t parse_ok);
static void UI_ModelSetRtcOk(uint8_t rtc_ok);
static void UI_ModelSetNtpOk(uint8_t ntp_ok);
static void UI_ModelUpdateWeather(const WeatherNow *weather_now);
static void UI_ModelUpdateForecast(const ForecastDay forecast[3]);
static const char *Forecast_DateShort(const char *date);
/* USER CODE END FunctionPrototypes */

void StartKeyTask(void *argument);
void StartUiTask(void *argument);
void StartTimeTask(void *argument);
void StartNetworkTask(void *argument);
void StartWeatherTask(void *argument);

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
  espMutexHandle = osMutexNew(&espMutex_attributes);
  uiModelMutexHandle = osMutexNew(&uiModelMutex_attributes);
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

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of KeyTask */
  KeyTaskHandle = osThreadNew(StartKeyTask, NULL, &KeyTask_attributes);

  /* creation of UiTask */
  UiTaskHandle = osThreadNew(StartUiTask, NULL, &UiTask_attributes);

  /* creation of TimeTask */
  TimeTaskHandle = osThreadNew(StartTimeTask, NULL, &TimeTask_attributes);

  /* creation of NetworkTask */
  NetworkTaskHandle = osThreadNew(StartNetworkTask, NULL, &NetworkTask_attributes);

  /* creation of WeatherTask */
  WeatherTaskHandle = osThreadNew(StartWeatherTask, NULL, &WeatherTask_attributes);

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

static void UI_DrawText(int x, int y, const char *text, uint16_t color)
{
  Z_ST7735_ShowString(x, y, text, (int)strlen(text), color);
}

static void UI_DrawHomePage(const UiModel *model)
{
  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "HOME", ST7735_WHITE);
  UI_DrawText(8, 28, model->time, ST7735_GREEN);
  UI_DrawText(8, 50, model->date, ST7735_WHITE);
  UI_DrawText(8, 75, model->weather_now.city, ST7735_YELLOW);

  if (model->weather_now.valid)
  {
    UI_DrawText(8, 98, model->weather_now.weather, ST7735_CYAN);
    UI_DrawText(72, 98, model->weather_now.temperature, ST7735_CYAN);
    UI_DrawText(8, 122, "Humi:", ST7735_WHITE);
    UI_DrawText(58, 122, model->weather_now.humidity, ST7735_WHITE);
  }
  else
  {
    UI_DrawText(8, 100, "Weather loading", ST7735_YELLOW);
  }
}

static void UI_DrawWeatherPage(const UiModel *model)
{
  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "WEATHER", ST7735_WHITE);

  if (!model->weather_now.valid)
  {
    UI_DrawText(5, 40, "No weather data", ST7735_RED);
    return;
  }

  UI_DrawText(5, 28, "City:", ST7735_WHITE);
  UI_DrawText(58, 28, model->weather_now.city, ST7735_GREEN);
  UI_DrawText(5, 50, "Text:", ST7735_WHITE);
  UI_DrawText(58, 50, model->weather_now.weather, ST7735_YELLOW);
  UI_DrawText(5, 72, "Temp:", ST7735_WHITE);
  UI_DrawText(58, 72, model->weather_now.temperature, ST7735_CYAN);
  UI_DrawText(5, 94, "Humi:", ST7735_WHITE);
  UI_DrawText(58, 94, model->weather_now.humidity, ST7735_CYAN);
  UI_DrawText(5, 116, "Wind:", ST7735_WHITE);
  UI_DrawText(58, 116, model->weather_now.wind_scale, ST7735_WHITE);
  UI_DrawText(5, 140, "OK Refresh", ST7735_GREEN);
}

static const char *Forecast_DateShort(const char *date)
{
  if (date != NULL && strlen(date) >= 10)
  {
    return date + 5;
  }

  return "--";
}

static void UI_DrawForecastPage(const UiModel *model)
{
  char line[32];
  int i;
  int y;

  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "FORECAST", ST7735_WHITE);

  if (!model->parse_daily_ok)
  {
    UI_DrawText(5, 45, "No forecast data", ST7735_RED);
    return;
  }

  for (i = 0; i < 3; i++)
  {
    y = 28 + i * 42;

    snprintf(line,
             sizeof(line),
             "%s %s",
             Forecast_DateShort(model->forecast[i].date),
             model->forecast[i].text_day);
    UI_DrawText(5, y, line, ST7735_YELLOW);

    snprintf(line,
             sizeof(line),
             "%s/%sC",
             model->forecast[i].low,
             model->forecast[i].high);
    UI_DrawText(5, y + 18, line, ST7735_CYAN);
  }
}

static void UI_DrawDebugPage(const UiModel *model)
{
  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "DEBUG", ST7735_WHITE);
  UI_DrawText(5, 25, "WiFi:", ST7735_WHITE);
  UI_DrawText(65, 25, model->wifi_ok ? "OK" : "NO", ST7735_GREEN);
  UI_DrawText(5, 45, "Now:", ST7735_WHITE);
  UI_DrawText(65, 45, model->api_now_ok ? "OK" : "NO", ST7735_GREEN);
  UI_DrawText(5, 65, "Daily:", ST7735_WHITE);
  UI_DrawText(65, 65, model->api_daily_ok ? "OK" : "NO", ST7735_GREEN);
  UI_DrawText(5, 85, "JsonN:", ST7735_WHITE);
  UI_DrawText(65, 85, model->parse_now_ok ? "OK" : "NO", ST7735_GREEN);
  UI_DrawText(5, 105, "JsonD:", ST7735_WHITE);
  UI_DrawText(65, 105, model->parse_daily_ok ? "OK" : "NO", ST7735_GREEN);
  UI_DrawText(5, 125, "RTC:", ST7735_WHITE);
  UI_DrawText(65, 125, model->rtc_ok ? "OK" : "NO", ST7735_GREEN);
  UI_DrawText(5, 145, "NTP:", ST7735_WHITE);
  UI_DrawText(65, 145, model->ntp_ok ? "OK" : "NO", ST7735_GREEN);
}

static void UI_DrawCurrentPage(void)
{
  UiModel model;

  UI_ModelSnapshot(&model);

  switch (g_currentPage)
  {
    case PAGE_HOME:
      UI_DrawHomePage(&model);
      break;

    case PAGE_WEATHER:
      UI_DrawWeatherPage(&model);
      break;

    case PAGE_FORECAST:
      UI_DrawForecastPage(&model);
      break;

    case PAGE_DEBUG:
      UI_DrawDebugPage(&model);
      break;

    default:
      break;
  }
}

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

  Z_ST7735S_Init();

  g_currentPage = PAGE_HOME;
  UI_DrawCurrentPage();

  for (;;)
  {
    if (osMessageQueueGet(uiQueueHandle, &msg, NULL, osWaitForever) == osOK)
    {
      switch (msg)
      {
        case UI_MSG_RIGHT:
          g_currentPage = (PageId)((g_currentPage + 1) % PAGE_MAX);
          UI_DrawCurrentPage();
          break;

        case UI_MSG_LEFT:
          if (g_currentPage == 0)
          {
            g_currentPage = (PageId)(PAGE_MAX - 1);
          }
          else
          {
            g_currentPage = (PageId)(g_currentPage - 1);
          }
          UI_DrawCurrentPage();
          break;

        case UI_MSG_BACK:
          g_currentPage = PAGE_HOME;
          UI_DrawCurrentPage();
          break;

        case UI_MSG_OK:
          if (g_currentPage == PAGE_HOME || g_currentPage == PAGE_WEATHER || g_currentPage == PAGE_FORECAST)
          {
            g_weatherForceUpdate = 1;
            UI_PostMessage(UI_MSG_WEATHER_UPDATE);
          }
          else
          {
            UI_DrawCurrentPage();
          }
          break;

        case UI_MSG_TIME_UPDATE:
          if (g_currentPage == PAGE_HOME || g_currentPage == PAGE_DEBUG)
          {
            UI_DrawCurrentPage();
          }
          break;

        case UI_MSG_WEATHER_UPDATE:
          if (g_currentPage == PAGE_HOME ||
              g_currentPage == PAGE_WEATHER ||
              g_currentPage == PAGE_FORECAST ||
              g_currentPage == PAGE_DEBUG)
          {
            UI_DrawCurrentPage();
          }
          break;

        case UI_MSG_NETWORK_CHANGE:
          if (g_currentPage == PAGE_HOME || g_currentPage == PAGE_DEBUG)
          {
            UI_DrawCurrentPage();
          }
          break;

        default:
          break;
      }
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

      UI_ModelSetTimeDate(time, date);
      UI_ModelSetRtcOk(1);
      UI_PostMessage(UI_MSG_TIME_UPDATE);
    }

    osDelay(1000);
  }
  /* USER CODE END StartTimeTask */
}

/* USER CODE BEGIN Header_StartNetworkTask */
/**
* @brief Function implementing the NetworkTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartNetworkTask */
void StartNetworkTask(void *argument)
{
  /* USER CODE BEGIN StartNetworkTask */
  uint8_t wifi_ok;
  uint8_t sntp_tried = 0;

  osDelay(1500);

  for(;;)
  {
    if (ESP_CheckWiFi())
    {
      wifi_ok = 1;
    }
    else
    {
      wifi_ok = ESP_ConnectWiFi() ? 1 : 0;
    }

    UI_ModelSetNetwork(wifi_ok);
    if (wifi_ok && !sntp_tried)
    {
      sntp_tried = 1;
      ESP_SyncTimeBySNTP();
    }
    else
    {
      if (!wifi_ok)
      {
        UI_ModelSetNtpOk(0);
      }
    }

    UI_PostMessage(UI_MSG_NETWORK_CHANGE);
    osDelay(WIFI_CHECK_INTERVAL_MS);
  }
  /* USER CODE END StartNetworkTask */
}

/* USER CODE BEGIN Header_StartWeatherTask */
/**
* @brief Function implementing the WeatherTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartWeatherTask */
void StartWeatherTask(void *argument)
{
  /* USER CODE BEGIN StartWeatherTask */
  uint32_t last_update = 0;
  uint32_t now;
  WeatherNow weather_now;
  ForecastDay forecast[3];
  WeatherDailyResult daily_result;

  osDelay(8000);
  g_weatherForceUpdate = 1;

  for(;;)
  {
    if (!UI_ModelGetWifiOk())
    {
      osDelay(1000);
      continue;
    }

    now = osKernelGetTickCount();
    if (!g_weatherForceUpdate &&
        (last_update != 0) &&
        ((now - last_update) < WEATHER_UPDATE_INTERVAL_MS))
    {
      osDelay(1000);
      continue;
    }

    g_weatherForceUpdate = 0;
    last_update = now;

    if (ESP_GetWeather(&weather_now))
    {
      UI_ModelSetApiNowOk(1);
      if (weather_now.update_time[0] != '\0' &&
          strcmp(weather_now.update_time, "--") != 0)
      {
        RTC_SetFromWeatherTime(weather_now.update_time);
      }
      UI_ModelUpdateWeather(&weather_now);
    }
    else
    {
      UI_ModelSetApiNowOk(0);
      UI_ModelSetParseNowOk(0);
      UI_PostMessage(UI_MSG_WEATHER_UPDATE);
    }

    osDelay(1500);

    daily_result = ESP_GetDailyForecast(forecast);
    if (daily_result == WEATHER_DAILY_PARSE_OK)
    {
      UI_ModelUpdateForecast(forecast);
    }
    else if (daily_result == WEATHER_DAILY_PARSE_FAIL)
    {
      UI_ModelSetApiDailyOk(1);
      UI_ModelSetParseDailyOk(0);
      UI_PostMessage(UI_MSG_WEATHER_UPDATE);
      osDelay(WEATHER_RETRY_INTERVAL_MS);
      continue;
    }
    else
    {
      UI_ModelSetApiDailyOk(0);
      UI_ModelSetParseDailyOk(0);
      UI_PostMessage(UI_MSG_WEATHER_UPDATE);
      osDelay(WEATHER_RETRY_INTERVAL_MS);
      continue;
    }

    osDelay(1000);
  }
  /* USER CODE END StartWeatherTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static uint8_t espRxBuf[8192];

static void UI_PostMessage(uint32_t msg)
{
  osMessageQueuePut(uiQueueHandle, &msg, 0, 0);
}

static int UI_ModelLock(uint32_t timeout_ms)
{
  if (uiModelMutexHandle == NULL)
  {
    return 0;
  }

  return osMutexAcquire(uiModelMutexHandle, timeout_ms) == osOK;
}

static void UI_ModelUnlock(void)
{
  if (uiModelMutexHandle != NULL)
  {
    osMutexRelease(uiModelMutexHandle);
  }
}

static void UI_CopyString(char *dst, size_t dst_size, const char *src)
{
  if (dst == NULL || dst_size == 0)
  {
    return;
  }

  if (src == NULL)
  {
    src = "";
  }

  snprintf(dst, dst_size, "%s", src);
}

static void UI_ModelSnapshot(UiModel *model)
{
  if (model == NULL)
  {
    return;
  }

  memset(model, 0, sizeof(*model));
  UI_CopyString(model->time, sizeof(model->time), "00:00:00");
  UI_CopyString(model->date, sizeof(model->date), "2000-01-01");
  UI_CopyString(model->weather_now.city, sizeof(model->weather_now.city), "--");
  UI_CopyString(model->weather_now.weather, sizeof(model->weather_now.weather), "--");
  UI_CopyString(model->weather_now.temperature, sizeof(model->weather_now.temperature), "--C");
  UI_CopyString(model->weather_now.humidity, sizeof(model->weather_now.humidity), "--%");
  UI_CopyString(model->weather_now.wind_dir, sizeof(model->weather_now.wind_dir), "--");
  UI_CopyString(model->weather_now.wind_scale, sizeof(model->weather_now.wind_scale), "--");
  UI_CopyString(model->weather_now.update_time, sizeof(model->weather_now.update_time), "--");
  UI_CopyString(model->forecast[0].date, sizeof(model->forecast[0].date), "--");
  UI_CopyString(model->forecast[1].date, sizeof(model->forecast[1].date), "--");
  UI_CopyString(model->forecast[2].date, sizeof(model->forecast[2].date), "--");

  if (UI_ModelLock(osWaitForever))
  {
    *model = g_uiModel;
    UI_ModelUnlock();
  }
}

static uint8_t UI_ModelGetWifiOk(void)
{
  uint8_t wifi_ok = 0;

  if (UI_ModelLock(osWaitForever))
  {
    wifi_ok = g_uiModel.wifi_ok;
    UI_ModelUnlock();
  }

  return wifi_ok;
}

static void UI_ModelSetTimeDate(const char *time, const char *date)
{
  if (UI_ModelLock(osWaitForever))
  {
    UI_CopyString(g_uiModel.time, sizeof(g_uiModel.time), time);
    UI_CopyString(g_uiModel.date, sizeof(g_uiModel.date), date);
    UI_ModelUnlock();
  }
}

static void UI_ModelSetNetwork(uint8_t wifi_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.wifi_ok = wifi_ok ? 1U : 0U;
    if (!wifi_ok)
    {
      g_uiModel.api_now_ok = 0;
      g_uiModel.api_daily_ok = 0;
      g_uiModel.parse_now_ok = 0;
      g_uiModel.parse_daily_ok = 0;
    }
    UI_ModelUnlock();
  }
}

static void UI_ModelSetApiNowOk(uint8_t api_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.api_now_ok = api_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

static void UI_ModelSetApiDailyOk(uint8_t api_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.api_daily_ok = api_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

static void UI_ModelSetParseNowOk(uint8_t parse_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.parse_now_ok = parse_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

static void UI_ModelSetParseDailyOk(uint8_t parse_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.parse_daily_ok = parse_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

static void UI_ModelSetRtcOk(uint8_t rtc_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.rtc_ok = rtc_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

static void UI_ModelSetNtpOk(uint8_t ntp_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.ntp_ok = ntp_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

static void UI_ModelUpdateWeather(const WeatherNow *weather_now)
{
  if (weather_now == NULL)
  {
    return;
  }

  if (UI_ModelLock(osWaitForever))
  {
    UI_CopyString(g_uiModel.weather_now.city, sizeof(g_uiModel.weather_now.city), weather_now->city);
    UI_CopyString(g_uiModel.weather_now.weather, sizeof(g_uiModel.weather_now.weather), weather_now->weather);
    UI_CopyString(g_uiModel.weather_now.temperature, sizeof(g_uiModel.weather_now.temperature), weather_now->temperature);
    UI_CopyString(g_uiModel.weather_now.humidity, sizeof(g_uiModel.weather_now.humidity), weather_now->humidity);
    UI_CopyString(g_uiModel.weather_now.wind_dir, sizeof(g_uiModel.weather_now.wind_dir), weather_now->wind_dir);
    UI_CopyString(g_uiModel.weather_now.wind_scale, sizeof(g_uiModel.weather_now.wind_scale), weather_now->wind_scale);
    UI_CopyString(g_uiModel.weather_now.update_time, sizeof(g_uiModel.weather_now.update_time), weather_now->update_time);
    g_uiModel.weather_now.valid = weather_now->valid ? 1U : 0U;
    g_uiModel.parse_now_ok = weather_now->valid ? 1U : 0U;
    UI_ModelUnlock();
  }

  UI_PostMessage(UI_MSG_WEATHER_UPDATE);
}

static void UI_ModelUpdateForecast(const ForecastDay forecast[3])
{
  if (forecast == NULL)
  {
    return;
  }

  if (UI_ModelLock(osWaitForever))
  {
    memcpy(g_uiModel.forecast, forecast, sizeof(g_uiModel.forecast));
    g_uiModel.parse_daily_ok = 1;
    UI_ModelUnlock();
  }

  UI_PostMessage(UI_MSG_WEATHER_UPDATE);
}

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

  UI_ModelSetRtcOk(1);
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

      if (expect != NULL && strstr((char *)espRxBuf, expect) != NULL)
      {
        return 1;
      }

      if (strstr((char *)espRxBuf, "ERROR") != NULL ||
          strstr((char *)espRxBuf, "FAIL") != NULL)
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

static int ESP_SendRawAndReadUntil(const char *data, const char *expect, uint32_t timeout_ms)
{
  if (data == NULL)
  {
    return 0;
  }

  if (HAL_UART_Transmit(&huart1, (uint8_t *)data, strlen(data), 1000) != HAL_OK)
  {
    return 0;
  }

  return ESP_ReadUntil(expect, timeout_ms);
}

static int ESP_CheckWiFi(void)
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

static int ESP_SyncTimeBySNTP(void)
{
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;
  int result = 0;
  int retry;

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
      result = RTC_SetDateTime(&sDate, &sTime);
      break;
    }
  }

done:
  ESP_Unlock();
  UI_ModelSetNtpOk(result ? 1U : 0U);
  return result;
}

static int ESP_ConnectWiFi(void)
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

static int ESP_GetWeather(WeatherNow *out)
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

  result = Weather_ParseResponse(out);

done:
  ESP_Unlock();
  return result;
}

static WeatherDailyResult ESP_GetDailyForecast(ForecastDay out[3])
{
  char cmd[128];
  char request[320];
  WeatherDailyResult result = WEATHER_DAILY_HTTP_FAIL;
  int parse_ok;
  int request_len;

  if (out == NULL)
  {
    return WEATHER_DAILY_HTTP_FAIL;
  }

  if (!ESP_Lock(32000))
  {
    return WEATHER_DAILY_HTTP_FAIL;
  }

  ESP_SendCmdRaw("AT+CIPCLOSE\r\n", "OK", 2000);

  if (!ESP_SendCmdRaw("AT+CIPSTART=\"TCP\",\"" WEATHER_DAILY_HOST "\",80\r\n", "OK", 10000) &&
      strstr((char *)espRxBuf, "ALREADY CONNECTED") == NULL)
  {
    goto done;
  }

  snprintf(request,
           sizeof(request),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Connection: close\r\n"
           "\r\n",
           WEATHER_DAILY_PATH,
           WEATHER_DAILY_HOST);

  request_len = (int)strlen(request);
  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", request_len);

  if (!ESP_SendCmdRaw(cmd, ">", 5000))
  {
    goto done;
  }

  if (!ESP_SendRawAndReadUntil(request, "\"daily\"", 30000))
  {
    goto done;
  }

  UI_ModelSetApiDailyOk(1);

  parse_ok = Weather_ParseDailyResponse(out);
  UI_ModelSetParseDailyOk(parse_ok ? 1U : 0U);
  result = parse_ok ? WEATHER_DAILY_PARSE_OK : WEATHER_DAILY_PARSE_FAIL;

done:
  ESP_SendCmdRaw("AT+CIPCLOSE\r\n", "OK", 2000);
  ESP_Unlock();
  return result;
}

static int ExtractString(const char *src,
                         const char *begin,
                         const char *end,
                         char *out,
                         size_t out_size)
{
  const char *p1;
  const char *p2;
  size_t len;

  if (src == NULL || begin == NULL || end == NULL || out == NULL || out_size == 0)
  {
    return 0;
  }

  p1 = strstr(src, begin);
  if (p1 == NULL)
  {
    return 0;
  }

  p1 += strlen(begin);

  p2 = strstr(p1, end);
  if (p2 == NULL)
  {
    return 0;
  }

  len = (size_t)(p2 - p1);
  if (len >= out_size)
  {
    len = out_size - 1;
  }

  memcpy(out, p1, len);
  out[len] = '\0';

  return 1;
}

static int ExtractStringRange(const char *start,
                              const char *end_limit,
                              const char *begin,
                              const char *end,
                              char *out,
                              size_t out_size)
{
  const char *p1;
  const char *p2;
  size_t len;

  if (start == NULL || end_limit == NULL || begin == NULL || end == NULL || out == NULL || out_size == 0)
  {
    return 0;
  }

  p1 = strstr(start, begin);
  if (p1 == NULL || p1 >= end_limit)
  {
    return 0;
  }

  p1 += strlen(begin);

  p2 = strstr(p1, end);
  if (p2 == NULL || p2 > end_limit)
  {
    return 0;
  }

  len = (size_t)(p2 - p1);
  if (len >= out_size)
  {
    len = out_size - 1;
  }

  memcpy(out, p1, len);
  out[len] = '\0';

  return 1;
}

static int Weather_ParseResponse(WeatherNow *out)
{
  char temp_num[8];
  char humidity_num[8];
  int has_weather;
  int has_temperature;

  if (out == NULL)
  {
    return 0;
  }

  memset(out, 0, sizeof(*out));
  UI_CopyString(out->city, sizeof(out->city), WEATHER_CITY);
  UI_CopyString(out->weather, sizeof(out->weather), "--");
  UI_CopyString(out->temperature, sizeof(out->temperature), "--C");
  UI_CopyString(out->humidity, sizeof(out->humidity), "--%");
  UI_CopyString(out->wind_dir, sizeof(out->wind_dir), "--");
  UI_CopyString(out->wind_scale, sizeof(out->wind_scale), "--");
  UI_CopyString(out->update_time, sizeof(out->update_time), "--");

  ExtractString((char *)espRxBuf,
                "\"name\":\"",
                "\"",
                out->city,
                sizeof(out->city));

  has_weather = ExtractString((char *)espRxBuf,
                              "\"text\":\"",
                              "\"",
                              out->weather,
                              sizeof(out->weather));

  has_temperature = ExtractString((char *)espRxBuf,
                                  "\"temperature\":\"",
                                  "\"",
                                  temp_num,
                                  sizeof(temp_num));
  if (!has_temperature)
  {
    has_temperature = ExtractString((char *)espRxBuf,
                                    "\"temp\":\"",
                                    "\"",
                                    temp_num,
                                    sizeof(temp_num));
  }

  if (has_temperature)
  {
    snprintf(out->temperature,
             sizeof(out->temperature),
             "%sC",
             temp_num);
  }

  if (ExtractString((char *)espRxBuf,
                    "\"humidity\":\"",
                    "\"",
                    humidity_num,
                    sizeof(humidity_num)))
  {
    snprintf(out->humidity, sizeof(out->humidity), "%s%%", humidity_num);
  }

  if (!ExtractString((char *)espRxBuf,
                     "\"wind_direction\":\"",
                     "\"",
                     out->wind_dir,
                     sizeof(out->wind_dir)))
  {
    ExtractString((char *)espRxBuf,
                  "\"windDir\":\"",
                  "\"",
                  out->wind_dir,
                  sizeof(out->wind_dir));
  }

  if (!ExtractString((char *)espRxBuf,
                     "\"wind_scale\":\"",
                     "\"",
                     out->wind_scale,
                     sizeof(out->wind_scale)))
  {
    ExtractString((char *)espRxBuf,
                  "\"windScale\":\"",
                  "\"",
                  out->wind_scale,
                  sizeof(out->wind_scale));
  }

  if (!ExtractString((char *)espRxBuf,
                     "\"last_update\":\"",
                     "\"",
                     out->update_time,
                     sizeof(out->update_time)))
  {
    ExtractString((char *)espRxBuf,
                  "\"obsTime\":\"",
                  "\"",
                  out->update_time,
                  sizeof(out->update_time));
  }

  if (!has_weather || !has_temperature)
  {
    return 0;
  }

  out->valid = 1;
  return 1;
}

static int Weather_ParseDailyResponse(ForecastDay out[3])
{
  const char *p;
  const char *obj_start;
  const char *obj_end;
  int i;

  if (out == NULL)
  {
    return 0;
  }

  p = strstr((char *)espRxBuf, "\"daily\":[");
  if (p == NULL)
  {
    return 0;
  }

  for (i = 0; i < 3; i++)
  {
    obj_start = strstr(p, "{");
    if (obj_start == NULL)
    {
      return 0;
    }

    obj_end = strstr(obj_start, "}");
    if (obj_end == NULL)
    {
      return 0;
    }

    memset(&out[i], 0, sizeof(out[i]));
    UI_CopyString(out[i].date, sizeof(out[i].date), "--");
    UI_CopyString(out[i].text_day, sizeof(out[i].text_day), "--");
    UI_CopyString(out[i].text_night, sizeof(out[i].text_night), "--");
    UI_CopyString(out[i].high, sizeof(out[i].high), "--");
    UI_CopyString(out[i].low, sizeof(out[i].low), "--");
    UI_CopyString(out[i].wind_direction, sizeof(out[i].wind_direction), "--");
    UI_CopyString(out[i].wind_scale, sizeof(out[i].wind_scale), "--");

    ExtractStringRange(obj_start, obj_end,
                       "\"date\":\"",
                       "\"",
                       out[i].date,
                       sizeof(out[i].date));

    ExtractStringRange(obj_start, obj_end,
                       "\"text_day\":\"",
                       "\"",
                       out[i].text_day,
                       sizeof(out[i].text_day));

    ExtractStringRange(obj_start, obj_end,
                       "\"text_night\":\"",
                       "\"",
                       out[i].text_night,
                       sizeof(out[i].text_night));

    ExtractStringRange(obj_start, obj_end,
                       "\"high\":\"",
                       "\"",
                       out[i].high,
                       sizeof(out[i].high));

    ExtractStringRange(obj_start, obj_end,
                       "\"low\":\"",
                       "\"",
                       out[i].low,
                       sizeof(out[i].low));

    ExtractStringRange(obj_start, obj_end,
                       "\"wind_direction\":\"",
                       "\"",
                       out[i].wind_direction,
                       sizeof(out[i].wind_direction));

    ExtractStringRange(obj_start, obj_end,
                       "\"wind_scale\":\"",
                       "\"",
                       out[i].wind_scale,
                       sizeof(out[i].wind_scale));

    if (strcmp(out[i].date, "--") == 0 ||
        strcmp(out[i].text_day, "--") == 0 ||
        strcmp(out[i].high, "--") == 0 ||
        strcmp(out[i].low, "--") == 0)
    {
      return 0;
    }

    p = obj_end + 1;
  }

  return 1;
}
/* USER CODE END Application */

