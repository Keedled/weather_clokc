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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum {
    PAGE_HOME = 0,
    PAGE_WEATHER,
    PAGE_FORECAST,
    PAGE_INDOOR,
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

typedef struct {
    char time[16];
    char date[20];
    char city[20];
    char weather[20];
    char temperature[12];
    char humidity[12];
    uint8_t wifi_ok;
    uint8_t api_ok;
} UiModel;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define WIFI_SSID       "keeled"
#define WIFI_PASSWORD   "85291153"

#define WEATHER_HTTP_CMD "AT+HTTPCGET=\"https://api.seniverse.com/v3/weather/now.json?key=SwSCOVN3uoHLMsZOG&location=beijing&language=en&unit=c\"\r\n"
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
    .date = "2026-07-02",
    .city = "Beijing",
    .weather = "--",
    .temperature = "--C",
    .humidity = "--%",
    .wifi_ok = 0,
    .api_ok = 0
};

static osMutexId_t espMutexHandle;
const osMutexAttr_t espMutex_attributes = {
  .name = "espMutex"
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
static int ESP_CheckWiFi(void);
static int ESP_ConnectWiFi(void);
static int ESP_GetWeather(void);
static int ExtractString(const char *src, const char *begin, const char *end, char *out, size_t out_size);
static void Weather_ParseResponse(void);
static void UI_PostMessage(uint32_t msg);
/* USER CODE END FunctionPrototypes */

void StartKeyTask(void *argument);
void StartUiTask(void *argument);
void StartTaskTask(void *argument);
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
  TimeTaskHandle = osThreadNew(StartTaskTask, NULL, &TimeTask_attributes);

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

static void UI_DrawHomePage(void)
{
  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "HOME", ST7735_WHITE);
  UI_DrawText(10, 30, g_uiModel.time, ST7735_GREEN);
  UI_DrawText(10, 55, g_uiModel.date, ST7735_WHITE);
  UI_DrawText(10, 85, g_uiModel.city, ST7735_YELLOW);
  UI_DrawText(10, 110, g_uiModel.weather, ST7735_CYAN);
  UI_DrawText(75, 110, g_uiModel.temperature, ST7735_CYAN);
}

static void UI_DrawWeatherPage(void)
{
  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "WEATHER", ST7735_WHITE);
  UI_DrawText(5, 30, "City:", ST7735_WHITE);
  UI_DrawText(55, 30, g_uiModel.city, ST7735_GREEN);
  UI_DrawText(5, 55, "Text:", ST7735_WHITE);
  UI_DrawText(55, 55, g_uiModel.weather, ST7735_YELLOW);
  UI_DrawText(5, 80, "Temp:", ST7735_WHITE);
  UI_DrawText(55, 80, g_uiModel.temperature, ST7735_CYAN);
  UI_DrawText(5, 105, "Humi:", ST7735_WHITE);
  UI_DrawText(55, 105, g_uiModel.humidity, ST7735_CYAN);
}

static void UI_DrawForecastPage(void)
{
  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "FORECAST", ST7735_WHITE);
  UI_DrawText(10, 40, "3 Days", ST7735_GREEN);
  UI_DrawText(10, 65, "Wait Weather", ST7735_CYAN);
}

static void UI_DrawIndoorPage(void)
{
  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "INDOOR", ST7735_WHITE);
  UI_DrawText(10, 40, "Temp Humi", ST7735_GREEN);
  UI_DrawText(10, 65, "Wait Sensor", ST7735_CYAN);
}

static void UI_DrawDebugPage(void)
{
  char buf[32];

  Z_ST7735S_RefreshAll(ST7735_BLACK);

  UI_DrawText(5, 5, "DEBUG", ST7735_WHITE);
  UI_DrawText(5, 30, "WiFi:", ST7735_WHITE);
  UI_DrawText(55, 30, g_uiModel.wifi_ok ? "OK" : "NO", ST7735_GREEN);
  UI_DrawText(5, 55, "API:", ST7735_WHITE);
  UI_DrawText(55, 55, g_uiModel.api_ok ? "OK" : "NO", ST7735_GREEN);

  snprintf(buf, sizeof(buf), "%lu", osKernelGetTickCount());
  UI_DrawText(5, 85, "Tick:", ST7735_WHITE);
  UI_DrawText(55, 85, buf, ST7735_CYAN);
}

static void UI_DrawCurrentPage(void)
{
  switch (g_currentPage)
  {
    case PAGE_HOME:
      UI_DrawHomePage();
      break;

    case PAGE_WEATHER:
      UI_DrawWeatherPage();
      break;

    case PAGE_FORECAST:
      UI_DrawForecastPage();
      break;

    case PAGE_INDOOR:
      UI_DrawIndoorPage();
      break;

    case PAGE_DEBUG:
      UI_DrawDebugPage();
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
          /* 暂时用于刷新当前页面 */
          UI_DrawCurrentPage();
          break;

        case UI_MSG_TIME_UPDATE:
          if (g_currentPage == PAGE_HOME || g_currentPage == PAGE_DEBUG)
          {
            UI_DrawCurrentPage();
          }
          break;

        case UI_MSG_WEATHER_UPDATE:
          if (g_currentPage == PAGE_HOME || g_currentPage == PAGE_WEATHER || g_currentPage == PAGE_DEBUG)
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

/* USER CODE BEGIN Header_StartTaskTask */
/**
* @brief Function implementing the TimeTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskTask */
void StartTaskTask(void *argument)
{
  /* USER CODE BEGIN StartTaskTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartTaskTask */
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
  osDelay(1500);

  for(;;)
  {
    if (ESP_CheckWiFi())
    {
      g_uiModel.wifi_ok = 1;
    }
    else
    {
      g_uiModel.wifi_ok = ESP_ConnectWiFi() ? 1 : 0;
    }

    if (!g_uiModel.wifi_ok)
    {
      g_uiModel.api_ok = 0;
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
  osDelay(8000);

  for(;;)
  {
    if (!g_uiModel.wifi_ok)
    {
      osDelay(WIFI_WAIT_INTERVAL_MS);
      continue;
    }

    if (ESP_GetWeather())
    {
      g_uiModel.api_ok = 1;
      UI_PostMessage(UI_MSG_WEATHER_UPDATE);
      osDelay(WEATHER_UPDATE_INTERVAL_MS);
    }
    else
    {
      g_uiModel.api_ok = 0;
      UI_PostMessage(UI_MSG_WEATHER_UPDATE);
      osDelay(WEATHER_RETRY_INTERVAL_MS);
    }
  }
  /* USER CODE END StartWeatherTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static uint8_t espRxBuf[4096];

static void UI_PostMessage(uint32_t msg)
{
  osMessageQueuePut(uiQueueHandle, &msg, 0, 0);
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
  uint16_t len = 0;
  uint32_t start_tick = HAL_GetTick();

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

static int ESP_GetWeather(void)
{
  const char http_cmd[] = WEATHER_HTTP_CMD;
  int result = 0;

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

  Weather_ParseResponse();
  result = 1;

done:
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

static void Weather_ParseResponse(void)
{
  char temp_num[8];

  ExtractString((char *)espRxBuf,
                "\"name\":\"",
                "\"",
                g_uiModel.city,
                sizeof(g_uiModel.city));

  ExtractString((char *)espRxBuf,
                "\"text\":\"",
                "\"",
                g_uiModel.weather,
                sizeof(g_uiModel.weather));

  if (ExtractString((char *)espRxBuf,
                    "\"temperature\":\"",
                    "\"",
                    temp_num,
                    sizeof(temp_num)))
  {
    snprintf(g_uiModel.temperature,
             sizeof(g_uiModel.temperature),
             "%sC",
             temp_num);
  }

  if (!ExtractString((char *)espRxBuf,
                     "\"humidity\":\"",
                     "\"",
                     g_uiModel.humidity,
                     sizeof(g_uiModel.humidity)))
  {
    snprintf(g_uiModel.humidity, sizeof(g_uiModel.humidity), "--%%");
  }
}
/* USER CODE END Application */

