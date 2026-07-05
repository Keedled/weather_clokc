#include "esp32_rx.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "usart.h"

#define ESP32_RX_BYTE_QUEUE_LEN       1024U
#define ESP32_RX_AT_EVENT_QUEUE_LEN   4U
#define ESP32_RX_URC_QUEUE_LEN        4U
#define ESP32_RX_LINE_BUF_SIZE        8192U
#define ESP32_RX_RESPONSE_BUF_SIZE    8192U
#define ESP32_RX_TASK_STACK_WORDS     512U

typedef enum {
  ESP32_RX_AT_EVENT_DATA = 0,
  ESP32_RX_AT_EVENT_TERMINAL
} Esp32RxAtEvent;

static osMessageQueueId_t rxByteQueueHandle;
static osMessageQueueId_t atEventQueueHandle;
static osMessageQueueId_t urcQueueHandle;
static osMutexId_t responseMutexHandle;
static osThreadId_t rxTaskHandle;

static StaticQueue_t rxByteQueueCb;
static uint8_t rxByteQueueStorage[ESP32_RX_BYTE_QUEUE_LEN * sizeof(uint8_t)];
static StaticQueue_t atEventQueueCb;
static uint8_t atEventQueueStorage[ESP32_RX_AT_EVENT_QUEUE_LEN * sizeof(Esp32RxAtEvent)];
static StaticQueue_t urcQueueCb;
static uint8_t urcQueueStorage[ESP32_RX_URC_QUEUE_LEN * sizeof(Esp32RxUrcEvent)];
static StaticSemaphore_t responseMutexCb;
static StaticTask_t rxTaskCb;
static StackType_t rxTaskStack[ESP32_RX_TASK_STACK_WORDS];

static uint8_t uartRxByte;
static char lineBuf[ESP32_RX_LINE_BUF_SIZE];
static uint16_t lineLen;

static char responseBuf[ESP32_RX_RESPONSE_BUF_SIZE];
static uint16_t responseLen;
static uint8_t commandActive;

static const osMessageQueueAttr_t rxByteQueue_attributes = {
  .name = "espRxByteQ",
  .cb_mem = &rxByteQueueCb,
  .cb_size = sizeof(rxByteQueueCb),
  .mq_mem = rxByteQueueStorage,
  .mq_size = sizeof(rxByteQueueStorage)
};

static const osMessageQueueAttr_t atEventQueue_attributes = {
  .name = "espAtEventQ",
  .cb_mem = &atEventQueueCb,
  .cb_size = sizeof(atEventQueueCb),
  .mq_mem = atEventQueueStorage,
  .mq_size = sizeof(atEventQueueStorage)
};

static const osMessageQueueAttr_t urcQueue_attributes = {
  .name = "espUrcQ",
  .cb_mem = &urcQueueCb,
  .cb_size = sizeof(urcQueueCb),
  .mq_mem = urcQueueStorage,
  .mq_size = sizeof(urcQueueStorage)
};

static const osMutexAttr_t responseMutex_attributes = {
  .name = "espRespMutex",
  .cb_mem = &responseMutexCb,
  .cb_size = sizeof(responseMutexCb)
};

static const osThreadAttr_t rxTask_attributes = {
  .name = "EspRxTask",
  .cb_mem = &rxTaskCb,
  .cb_size = sizeof(rxTaskCb),
  .stack_mem = rxTaskStack,
  .stack_size = sizeof(rxTaskStack),
  .priority = (osPriority_t)osPriorityAboveNormal
};

static int ESP32_RxStartUart(void);
static void ESP32_RxContinueUart(void);
static void ESP32_RxProcessByte(uint8_t ch);
static void ESP32_RxProcessLine(const char *line, uint16_t len);
static void ESP32_RxAppendResponse(const char *data, uint16_t len);
static int ESP32_RxLineEquals(const char *line, uint16_t len, const char *text);
static int ESP32_RxLineEndsWith(const char *line, uint16_t len, const char *suffix);
static int ESP32_RxBufferEndsWith(const char *buf, uint16_t len, const char *suffix);
static int ESP32_RxResponseMatchesExpect(const char *buf, uint16_t len, const char *expect);
static int ESP32_RxParseMqttLine(const char *line, Esp32RxUrcEvent *event);
static void ESP32_RxFlushAtEvents(void);
static void ESP32_RxCopyResponse(char *dst, uint16_t dst_size);

int ESP32_RxInit(void)
{
  if (rxByteQueueHandle == NULL)
  {
    rxByteQueueHandle = osMessageQueueNew(ESP32_RX_BYTE_QUEUE_LEN,
                                          sizeof(uint8_t),
                                          &rxByteQueue_attributes);
  }

  if (atEventQueueHandle == NULL)
  {
    atEventQueueHandle = osMessageQueueNew(ESP32_RX_AT_EVENT_QUEUE_LEN,
                                           sizeof(Esp32RxAtEvent),
                                           &atEventQueue_attributes);
  }

  if (urcQueueHandle == NULL)
  {
    urcQueueHandle = osMessageQueueNew(ESP32_RX_URC_QUEUE_LEN,
                                       sizeof(Esp32RxUrcEvent),
                                       &urcQueue_attributes);
  }

  if (responseMutexHandle == NULL)
  {
    responseMutexHandle = osMutexNew(&responseMutex_attributes);
  }

  if (rxTaskHandle == NULL)
  {
    rxTaskHandle = osThreadNew(ESP32_RxTask, NULL, &rxTask_attributes);
  }

  return rxByteQueueHandle != NULL &&
         atEventQueueHandle != NULL &&
         urcQueueHandle != NULL &&
         responseMutexHandle != NULL &&
         rxTaskHandle != NULL;
}

void ESP32_RxTask(void *argument)
{
  uint8_t ch;

  (void)argument;

  while (!ESP32_RxStartUart())
  {
    osDelay(100);
  }

  for (;;)
  {
    if (osMessageQueueGet(rxByteQueueHandle, &ch, NULL, osWaitForever) == osOK)
    {
      ESP32_RxProcessByte(ch);
    }
  }
}

void ESP32_RxBeginCommand(void)
{
  ESP32_RxFlushAtEvents();

  if (responseMutexHandle != NULL &&
      osMutexAcquire(responseMutexHandle, osWaitForever) == osOK)
  {
    responseLen = 0;
    responseBuf[0] = '\0';
    commandActive = 1U;
    osMutexRelease(responseMutexHandle);
  }
}

void ESP32_RxCancelCommand(void)
{
  if (responseMutexHandle != NULL &&
      osMutexAcquire(responseMutexHandle, osWaitForever) == osOK)
  {
    commandActive = 0U;
    osMutexRelease(responseMutexHandle);
  }
}

int ESP32_RxWaitFor(const char *expect,
                    char *response,
                    uint16_t response_size,
                    uint32_t timeout_ms)
{
  Esp32RxAtEvent event;
  uint32_t start_tick;
  uint32_t elapsed;
  uint32_t wait_ms;
  int result;
  uint8_t done;

  start_tick = HAL_GetTick();
  result = 0;
  done = 0U;

  while (!done)
  {
    elapsed = HAL_GetTick() - start_tick;
    if (elapsed >= timeout_ms)
    {
      break;
    }

    wait_ms = timeout_ms - elapsed;
    if (osMessageQueueGet(atEventQueueHandle, &event, NULL, wait_ms) != osOK)
    {
      break;
    }

    if (responseMutexHandle != NULL &&
        osMutexAcquire(responseMutexHandle, osWaitForever) == osOK)
    {
      if (ESP32_RxResponseMatchesExpect(responseBuf, responseLen, expect))
      {
        result = 1;
        done = 1U;
      }
      else if (ESP32_RxBufferEndsWith(responseBuf, responseLen, "ERROR\r\n") ||
               ESP32_RxBufferEndsWith(responseBuf, responseLen, "FAIL\r\n") ||
               ESP32_RxBufferEndsWith(responseBuf, responseLen, "busy p...\r\n"))
      {
        result = 0;
        done = 1U;
      }

      osMutexRelease(responseMutexHandle);
    }
  }

  ESP32_RxCopyResponse(response, response_size);
  ESP32_RxCancelCommand();

  return result;
}

int ESP32_RxGetUrc(Esp32RxUrcEvent *event, uint32_t timeout_ms)
{
  if (event == NULL || urcQueueHandle == NULL)
  {
    return 0;
  }

  if (osMessageQueueGet(urcQueueHandle, event, NULL, timeout_ms) == osOK)
  {
    return 1;
  }

  return 0;
}

void ESP32_RxUartIrqHandler(UART_HandleTypeDef *huart)
{
  HAL_UART_IRQHandler(huart);
}

void ESP32_RxUartRxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart != NULL && huart->Instance == USART1)
  {
    if (rxByteQueueHandle != NULL)
    {
      osMessageQueuePut(rxByteQueueHandle, &uartRxByte, 0, 0);
    }
    ESP32_RxContinueUart();
  }
}

void ESP32_RxUartErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart != NULL && huart->Instance == USART1)
  {
    ESP32_RxContinueUart();
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  ESP32_RxUartRxCpltCallback(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  ESP32_RxUartErrorCallback(huart);
}

static int ESP32_RxStartUart(void)
{
  HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);

  return HAL_UART_Receive_IT(&huart1, &uartRxByte, 1) == HAL_OK;
}

static void ESP32_RxContinueUart(void)
{
  HAL_UART_Receive_IT(&huart1, &uartRxByte, 1);
}

static void ESP32_RxProcessByte(uint8_t ch)
{
  if (lineLen < (uint16_t)(sizeof(lineBuf) - 1U))
  {
    lineBuf[lineLen++] = (char)ch;
  }
  else
  {
    ESP32_RxAppendResponse(lineBuf, lineLen);
    lineLen = 0;
    lineBuf[lineLen++] = (char)ch;
  }

  if (ch == '\n')
  {
    lineBuf[lineLen] = '\0';
    ESP32_RxProcessLine(lineBuf, lineLen);
    lineLen = 0;
  }
}

static void ESP32_RxProcessLine(const char *line, uint16_t len)
{
  Esp32RxUrcEvent urc;
  Esp32RxAtEvent event;

  if (line == NULL || len == 0U)
  {
    return;
  }

  memset(&urc, 0, sizeof(urc));

  if (ESP32_RxParseMqttLine(line, &urc))
  {
    osMessageQueuePut(urcQueueHandle, &urc, 0, 0);
    return;
  }

  if (strstr(line, "+MQTTDISCONNECTED") != NULL)
  {
    urc.type = ESP32_RX_URC_MQTT_DISCONNECTED;
    osMessageQueuePut(urcQueueHandle, &urc, 0, 0);
    return;
  }

  ESP32_RxAppendResponse(line, len);

  if (ESP32_RxLineEquals(line, len, "OK") ||
      ESP32_RxLineEquals(line, len, "ERROR") ||
      ESP32_RxLineEquals(line, len, "FAIL") ||
      ESP32_RxLineEndsWith(line, len, "busy p..."))
  {
    event = ESP32_RX_AT_EVENT_TERMINAL;
    osMessageQueuePut(atEventQueueHandle, &event, 0, 0);
  }
}

static void ESP32_RxAppendResponse(const char *data, uint16_t len)
{
  uint16_t copy_len;

  if (data == NULL || len == 0U || responseMutexHandle == NULL)
  {
    return;
  }

  if (osMutexAcquire(responseMutexHandle, osWaitForever) != osOK)
  {
    return;
  }

  if (commandActive)
  {
    if (responseLen < (uint16_t)(sizeof(responseBuf) - 1U))
    {
      copy_len = len;
      if (copy_len > (uint16_t)(sizeof(responseBuf) - 1U - responseLen))
      {
        copy_len = (uint16_t)(sizeof(responseBuf) - 1U - responseLen);
      }

      memcpy(responseBuf + responseLen, data, copy_len);
      responseLen = (uint16_t)(responseLen + copy_len);
      responseBuf[responseLen] = '\0';
    }
  }

  osMutexRelease(responseMutexHandle);
}

static int ESP32_RxLineEquals(const char *line, uint16_t len, const char *text)
{
  uint16_t start;
  uint16_t end;
  size_t text_len;

  if (line == NULL || text == NULL)
  {
    return 0;
  }

  start = 0U;
  end = len;
  while (start < end && (line[start] == '\r' || line[start] == '\n'))
  {
    start++;
  }
  while (end > start && (line[end - 1U] == '\r' || line[end - 1U] == '\n'))
  {
    end--;
  }

  text_len = strlen(text);
  if ((size_t)(end - start) != text_len)
  {
    return 0;
  }

  return memcmp(line + start, text, text_len) == 0;
}

static int ESP32_RxLineEndsWith(const char *line, uint16_t len, const char *suffix)
{
  size_t suffix_len;
  uint16_t end;

  if (line == NULL || suffix == NULL)
  {
    return 0;
  }

  end = len;
  while (end > 0U && (line[end - 1U] == '\r' || line[end - 1U] == '\n'))
  {
    end--;
  }

  suffix_len = strlen(suffix);
  if (suffix_len == 0U || (size_t)end < suffix_len)
  {
    return 0;
  }

  return memcmp(line + end - suffix_len, suffix, suffix_len) == 0;
}

static int ESP32_RxBufferEndsWith(const char *buf, uint16_t len, const char *suffix)
{
  size_t suffix_len;

  if (buf == NULL || suffix == NULL)
  {
    return 0;
  }

  suffix_len = strlen(suffix);
  if (suffix_len == 0U || (size_t)len < suffix_len)
  {
    return 0;
  }

  return memcmp(buf + len - suffix_len, suffix, suffix_len) == 0;
}

static int ESP32_RxResponseMatchesExpect(const char *buf, uint16_t len, const char *expect)
{
  size_t expect_len;
  uint16_t end;

  if (buf == NULL || expect == NULL)
  {
    return 0;
  }

  if (ESP32_RxBufferEndsWith(buf, len, expect))
  {
    return 1;
  }

  if (strchr(expect, '\r') != NULL || strchr(expect, '\n') != NULL)
  {
    return 0;
  }

  end = len;
  while (end > 0U && (buf[end - 1U] == '\r' || buf[end - 1U] == '\n'))
  {
    end--;
  }

  expect_len = strlen(expect);
  if (expect_len == 0U || (size_t)end < expect_len)
  {
    return 0;
  }

  return memcmp(buf + end - expect_len, expect, expect_len) == 0;
}

static int ESP32_RxParseMqttLine(const char *line, Esp32RxUrcEvent *event)
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

  if (line == NULL || event == NULL)
  {
    return 0;
  }

  p = strstr(line, "+MQTTSUBRECV:");
  if (p == NULL)
  {
    return 0;
  }

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

  comma = strchr(t2 + 1, ',');
  if (comma == NULL)
  {
    return 0;
  }

  data_len = atoi(comma + 1);
  comma = strchr(comma + 1, ',');
  if (comma == NULL || data_len < 0)
  {
    return 0;
  }

  payload_start = comma + 1;
  available_len = strlen(payload_start);
  while (available_len > 0U &&
         (payload_start[available_len - 1U] == '\r' ||
          payload_start[available_len - 1U] == '\n'))
  {
    available_len--;
  }

  if (available_len < (size_t)data_len)
  {
    return 0;
  }

  memset(event, 0, sizeof(*event));
  event->type = ESP32_RX_URC_MQTT_MESSAGE;

  topic_len = (size_t)(t2 - t1 - 1);
  if (topic_len >= sizeof(event->topic))
  {
    topic_len = sizeof(event->topic) - 1U;
  }
  memcpy(event->topic, t1 + 1, topic_len);
  event->topic[topic_len] = '\0';

  copy_len = data_len;
  if (copy_len >= (int)sizeof(event->payload))
  {
    copy_len = (int)sizeof(event->payload) - 1;
  }
  if (copy_len > 0)
  {
    memcpy(event->payload, payload_start, (size_t)copy_len);
  }
  event->payload[copy_len] = '\0';

  return 1;
}

static void ESP32_RxFlushAtEvents(void)
{
  Esp32RxAtEvent event;

  if (atEventQueueHandle == NULL)
  {
    return;
  }

  while (osMessageQueueGet(atEventQueueHandle, &event, NULL, 0) == osOK)
  {
  }
}

static void ESP32_RxCopyResponse(char *dst, uint16_t dst_size)
{
  uint16_t copy_len;

  if (dst == NULL || dst_size == 0U || responseMutexHandle == NULL)
  {
    return;
  }

  dst[0] = '\0';

  if (osMutexAcquire(responseMutexHandle, osWaitForever) != osOK)
  {
    return;
  }

  copy_len = responseLen;
  if (copy_len >= dst_size)
  {
    copy_len = (uint16_t)(dst_size - 1U);
  }

  memcpy(dst, responseBuf, copy_len);
  dst[copy_len] = '\0';

  osMutexRelease(responseMutexHandle);
}
