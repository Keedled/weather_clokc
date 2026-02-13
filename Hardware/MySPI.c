#include "MySPI.h"

#define SPI1_SCK_PORT    GPIOA
#define SPI1_SCK         GPIO_Pin_5

#define SPI1_MOSI_PORT    GPIOA
#define SPI1_MOSI         GPIO_Pin_7

#define CS_PORT     GPIOA
#define CS_PIN      GPIO_Pin_4

uint8_t rx_dump;//用于接收DMA从外设向内存写入的数据
void MySPI_W_SS(BitAction BitValue){
    GPIO_WriteBit(CS_PORT,CS_PIN,BitValue);
}

void MySPI_Init(void){
    //首先开启GPIOA时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
    //其次配置GPIOA的SPI1_SCK和SPI1_MOSI
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = SPI1_SCK | SPI1_MOSI;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SPI1_SCK_PORT,&GPIO_InitStructure);
    
    //之后配置需要使用的CS引脚，也就是手册上的NSS
    GPIO_InitStructure.GPIO_Pin = CS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(CS_PORT,&GPIO_InitStructure);

    //接下来初始化SPI
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);	//开启SPI1的时钟
    SPI_InitTypeDef SPI_InitStructure;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_Init(SPI1,&SPI_InitStructure);
    SPI_Cmd(SPI1,ENABLE);

    //我们拉高CS引脚
    MySPI_W_SS(Bit_SET);

}
void MySPI_Start(void){
    //拉低CS引脚
    MySPI_W_SS(Bit_RESET);
}
void MySPI_Stop(void){
    //拉高CS引脚
    MySPI_W_SS(Bit_SET);
}
uint8_t MySPI_SwapByte(uint8_t ByteSend){
    //我们检查TXE标志位，如果发现它被置位,我们跳出循环，将数据写入SPI1的DR寄存器
    //当数据被写入之后TXE被重新复位，当数据从TX被送入Shift_Register之后，TXE被置位
    while(SPI_I2S_GetFlagStatus(SPI1,SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1,ByteSend);
    //我们检查RXNE标志位，如果发现它被置位，我们跳出循环，将数据从RX读出,RXNE被复位
    //当数据从Shift_Register被读到RX之后，RXNE被置位
    while(SPI_I2S_GetFlagStatus(SPI1,SPI_I2S_FLAG_RXNE) == RESET);
    return SPI_I2S_ReceiveData(SPI1);
}
//我们实现一个使用DMA收发数据到硬件SPI的函数
uint8_t MySPI_SwapByte_DMA(uint8_t ByteSend){

    //首先开启DMA1的时钟
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    
    // RX-DMA: DMA1_Channel2  外设->存储器
    DMA_InitTypeDef DMA_InitStructure;
    DMA_DeInit(DMA1_Channel2);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)&rx_dump; // 丢弃用缓冲
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC; // P->M
    DMA_InitStructure.DMA_BufferSize         = 1;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Disable;  // 或者固定地址
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);

    // TX-DMA: DMA1_Channel3  存储器->外设
    DMA_DeInit(DMA1_Channel3);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)&ByteSend;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralDST; // M->P
    /* 其他同上，BufferSize=count */
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 使能 SPI 的 DMA 请求
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

    // 顺序：先开RX，再开TX
    DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);

    // 等待完成（两个通道都TC）
    while (DMA_GetFlagStatus(DMA1_FLAG_TC2) == RESET);
    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET);

    // 关DMA通道并清标志
    DMA_Cmd(DMA1_Channel2, DISABLE); DMA_ClearFlag(DMA1_FLAG_TC2);
    DMA_Cmd(DMA1_Channel3, DISABLE); DMA_ClearFlag(DMA1_FLAG_TC3);

    // 等 SPI 不忙
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
    
    return rx_dump;
}


// SPI发送多个字节函数实现
void MySPI_WriteBytes(const uint8_t *buf, size_t n)
{
    if (buf == NULL || n == 0) return;
    MySPI_Start();                       // CS ↓
    for (size_t i = 0; i < n; ++i) {
        (void)MySPI_SwapByte(buf[i]);    // 发送并丢弃回读
    }
    // 收尾：确保总线空闲
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) {}
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET) {}
    MySPI_Stop();                        // CS ↑
}
//初始化DMA1
void MySPI_SendData_DMA_Configure(const uint8_t *data,int data_size){
    
    //首先开启DMA1的时钟
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    
    // RX-DMA: DMA1_Channel2  外设->存储器
    DMA_InitTypeDef DMA_InitStructure;
    DMA_DeInit(DMA1_Channel2);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)&rx_dump; // 丢弃用缓冲
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC; // P->M
    DMA_InitStructure.DMA_BufferSize         = 1;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Disable;  
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);

    // TX-DMA: DMA1_Channel3  存储器->外设
    DMA_DeInit(DMA1_Channel3);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)data;
    DMA_InitStructure.DMA_BufferSize         = data_size;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralDST; // M->P
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;  // 传输大容量数据，使用自增
    /* 其他同上，BufferSize=count */
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 使能 SPI 的 DMA 请求
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
}
void MySPI_SendData_DMA(){
    // 顺序：先开RX，再开TX
    DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);

    // 等待完成（两个通道都TC）
    while (DMA_GetFlagStatus(DMA1_FLAG_TC2) == RESET);
    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET);

    // 关DMA通道并清标志
    DMA_Cmd(DMA1_Channel2, DISABLE); DMA_ClearFlag(DMA1_FLAG_TC2);
    DMA_Cmd(DMA1_Channel3, DISABLE); DMA_ClearFlag(DMA1_FLAG_TC3);

    // 等 SPI 不忙
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);

}