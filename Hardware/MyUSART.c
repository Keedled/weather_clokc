#include "MyUSART.h"
#include "MyDMA.h"

void MyUSART_Init(void) {
    // 启用 GPIOA 和 USART1 的时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    // 配置串口1
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; // TX
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10; // RX
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}

void USART_Write_Data(uint8_t *data, uint16_t length) {
    while (length--) {
        USART_SendData(USART1, *data++);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    }
}

void USART_Write_String(const char *str) {
    while (*str) {
        USART_SendData(USART1, *str++);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    }
}

uint16_t USART_Receive_Data(void) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET);
    return USART_ReceiveData(USART1);
}

void USART_Write_Data_DMA(uint8_t *data, uint16_t length) {
    MyDMA_USART1_Init((uint32_t)data, length); // 初始化 DMA
    MyDMA_USART1_Start(length); // 启动 DMA 传输
}

void USART1_FlushRx(void)
{
    volatile uint16_t tmp;
    // 读 SR 再读 DR：可清 FE/NE/ORE 等
    tmp = USART1->SR;
    tmp = USART1->DR;
    (void)tmp;

    // 把 FIFO/残留字节读空
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
        (void)USART_ReceiveData(USART1);
    }
}