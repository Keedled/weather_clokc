#ifndef __MySPI_H
#define __MySPI_H
#include <stddef.h>
#include "stm32f10x.h"
#include "st7735.h"

void MySPI_Init(void);
void MySPI_Start(void);
void MySPI_Stop(void);
uint8_t MySPI_SwapByte(uint8_t ByteSend);
void MySPI_SendData_DMA_Configure(const uint8_t* data,int data_size);
void MySPI_SendData_DMA();

void MySPI_W_SS(BitAction BitValue);

// SPI发送多个字节函数
void MySPI_WriteBytes(const uint8_t *buf, size_t n);


#endif