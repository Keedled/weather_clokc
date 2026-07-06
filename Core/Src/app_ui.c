#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "ST7735.h"

#define APP_UI_VERSION_TEXT "OTA3"

typedef enum {
    PAGE_HOME = 0,
    PAGE_WEATHER,
    PAGE_FORECAST,
    PAGE_DEBUG,
    PAGE_MAX
} PageId;

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
    .ntp_ok = 0,
    .mqtt_ok = 0
};

static volatile uint8_t g_weatherForceUpdate = 0;
static osMessageQueueId_t g_uiQueueHandle;
static osMutexId_t uiModelMutexHandle;

static const osMutexAttr_t uiModelMutex_attributes = {
  .name = "uiModelMutex"
};

static int UI_ModelLock(uint32_t timeout_ms);
static void UI_ModelUnlock(void);
static void UI_CopyString(char *dst, size_t dst_size, const char *src);
static void UI_ModelSnapshot(UiModel *model);
static const char *Forecast_DateShort(const char *date);
static void UI_DrawCurrentPage(void);
static void UI_DrawText(int x, int y, const char *text, uint16_t color);
static void UI_DrawHomePage(const UiModel *model);
static void UI_DrawWeatherPage(const UiModel *model);
static void UI_DrawForecastPage(const UiModel *model);
static void UI_DrawDebugPage(const UiModel *model);

int AppUi_Init(void)
{
  uiModelMutexHandle = osMutexNew(&uiModelMutex_attributes);
  return uiModelMutexHandle != NULL;
}

void AppUi_SetMessageQueue(osMessageQueueId_t queue)
{
  g_uiQueueHandle = queue;
}

void AppUi_PostMessage(uint32_t msg)
{
  if (g_uiQueueHandle != NULL)
  {
    osMessageQueuePut(g_uiQueueHandle, &msg, 0, 0);
  }
}

void AppUi_DisplayInit(void)
{
  Z_ST7735S_Init();

  g_currentPage = PAGE_HOME;
  UI_DrawCurrentPage();
}

void AppUi_HandleMessage(uint32_t msg)
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
        AppUi_PostMessage(UI_MSG_WEATHER_UPDATE);
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

uint8_t AppUi_IsWeatherForceUpdateRequested(void)
{
  return g_weatherForceUpdate;
}

void AppUi_ClearWeatherForceUpdate(void)
{
  g_weatherForceUpdate = 0;
}

void AppUi_ModelSetTimeDate(const char *time, const char *date)
{
  if (UI_ModelLock(osWaitForever))
  {
    UI_CopyString(g_uiModel.time, sizeof(g_uiModel.time), time);
    UI_CopyString(g_uiModel.date, sizeof(g_uiModel.date), date);
    UI_ModelUnlock();
  }
}

void AppUi_ModelSetNetwork(uint8_t wifi_ok)
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
      g_uiModel.mqtt_ok = 0;
    }
    UI_ModelUnlock();
  }
}

void AppUi_ModelSetApiNowOk(uint8_t api_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.api_now_ok = api_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

void AppUi_ModelSetApiDailyOk(uint8_t api_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.api_daily_ok = api_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

void AppUi_ModelSetParseNowOk(uint8_t parse_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.parse_now_ok = parse_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

void AppUi_ModelSetParseDailyOk(uint8_t parse_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.parse_daily_ok = parse_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

void AppUi_ModelSetRtcOk(uint8_t rtc_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.rtc_ok = rtc_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

void AppUi_ModelSetNtpOk(uint8_t ntp_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.ntp_ok = ntp_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

void AppUi_ModelSetMqttOk(uint8_t mqtt_ok)
{
  if (UI_ModelLock(osWaitForever))
  {
    g_uiModel.mqtt_ok = mqtt_ok ? 1U : 0U;
    UI_ModelUnlock();
  }
}

void AppUi_ModelUpdateWeather(const WeatherNow *weather_now)
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

  AppUi_PostMessage(UI_MSG_WEATHER_UPDATE);
}

void AppUi_ModelUpdateForecast(const ForecastDay forecast[3])
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

  AppUi_PostMessage(UI_MSG_WEATHER_UPDATE);
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

static const char *Forecast_DateShort(const char *date)
{
  if (date != NULL && strlen(date) >= 10)
  {
    return date + 5;
  }

  return "--";
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

  UI_DrawText(5, 5, "DEBUG " APP_UI_VERSION_TEXT, ST7735_WHITE);
  UI_DrawText(5, 23, "WiFi:", ST7735_WHITE);
  UI_DrawText(65, 23, model->wifi_ok ? "OK" : "NO", model->wifi_ok ? ST7735_GREEN : ST7735_RED);
  UI_DrawText(5, 40, "MQTT:", ST7735_WHITE);
  UI_DrawText(65, 40, model->mqtt_ok ? "OK" : "NO", model->mqtt_ok ? ST7735_GREEN : ST7735_RED);
  UI_DrawText(5, 57, "Now:", ST7735_WHITE);
  UI_DrawText(65, 57, model->api_now_ok ? "OK" : "NO", model->api_now_ok ? ST7735_GREEN : ST7735_RED);
  UI_DrawText(5, 74, "Daily:", ST7735_WHITE);
  UI_DrawText(65, 74, model->api_daily_ok ? "OK" : "NO", model->api_daily_ok ? ST7735_GREEN : ST7735_RED);
  UI_DrawText(5, 91, "JsonN:", ST7735_WHITE);
  UI_DrawText(65, 91, model->parse_now_ok ? "OK" : "NO", model->parse_now_ok ? ST7735_GREEN : ST7735_RED);
  UI_DrawText(5, 108, "JsonD:", ST7735_WHITE);
  UI_DrawText(65, 108, model->parse_daily_ok ? "OK" : "NO", model->parse_daily_ok ? ST7735_GREEN : ST7735_RED);
  UI_DrawText(5, 125, "RTC:", ST7735_WHITE);
  UI_DrawText(65, 125, model->rtc_ok ? "OK" : "NO", model->rtc_ok ? ST7735_GREEN : ST7735_RED);
  UI_DrawText(5, 142, "NTP:", ST7735_WHITE);
  UI_DrawText(65, 142, model->ntp_ok ? "OK" : "NO", model->ntp_ok ? ST7735_GREEN : ST7735_RED);
}
