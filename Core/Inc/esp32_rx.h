#ifndef __ESP32_RX_H
#define __ESP32_RX_H

#include <stdint.h>

#include "stm32f4xx_hal.h"

typedef enum {
  ESP32_RX_URC_NONE = 0,
  ESP32_RX_URC_MQTT_MESSAGE,
  ESP32_RX_URC_MQTT_DISCONNECTED
} Esp32RxUrcType;

typedef struct {
  Esp32RxUrcType type;
  char topic[128];
  char payload[256];
} Esp32RxUrcEvent;

int ESP32_RxInit(void);
void ESP32_RxTask(void *argument);

void ESP32_RxBeginCommand(void);
void ESP32_RxCancelCommand(void);
int ESP32_RxWaitFor(const char *expect,
                    char *response,
                    uint16_t response_size,
                    uint32_t timeout_ms);

int ESP32_RxGetUrc(Esp32RxUrcEvent *event, uint32_t timeout_ms);

void ESP32_RxUartIrqHandler(UART_HandleTypeDef *huart);
void ESP32_RxUartRxCpltCallback(UART_HandleTypeDef *huart);
void ESP32_RxUartErrorCallback(UART_HandleTypeDef *huart);

#endif /* __ESP32_RX_H */
