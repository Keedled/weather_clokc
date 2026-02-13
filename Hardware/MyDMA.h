#ifndef __MYDMA_H
#define __MYDMA_H

#include "stm32f10x.h"

void MyDMA_USART1_Init(uint32_t memoryAddr, uint16_t size);
void MyDMA_USART1_Start(uint16_t size);

#endif