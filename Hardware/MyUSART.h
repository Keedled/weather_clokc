#ifndef __MYUSART_H
#define __MYUSART_H

#include "stm32f10x.h"

void MyUSART_Init(void);
void USART_Write_Data(uint8_t *data, uint16_t length);
void USART_Write_String(const char *str);
uint16_t USART_Receive_Data(void);
void USART_Write_Data_DMA(uint8_t *data, uint16_t length);
void USART1_FlushRx(void);

#endif