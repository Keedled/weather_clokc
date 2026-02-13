#include "MyDMA.h"
#include "stm32f10x_dma.h"

uint16_t MyDMA_Size;					//定义全局变量，用于记住Init函数的Size，供Transfer函数使用

/**
  * 函    数：DMA初始化
  * 参    数：AddrA 原数组的首地址
  * 参    数：AddrB 目的数组的首地址
  * 参    数：Size 转运的数据大小（转运次数）
  * 返 回 值：无
  */
void MyDMA_Init(uint32_t AddrA, uint32_t AddrB, uint16_t Size)
{
	MyDMA_Size = Size;					//将Size写入到全局变量，记住参数Size
	
	/*开启时钟*/
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);						//开启DMA的时钟
	
	/*DMA初始化*/
	DMA_InitTypeDef DMA_InitStructure;										//定义结构体变量
	DMA_InitStructure.DMA_PeripheralBaseAddr = AddrA;						//外设基地址，给定形参AddrA
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;	//外设数据宽度，选择字节
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Enable;			//外设地址自增，选择使能
	DMA_InitStructure.DMA_MemoryBaseAddr = AddrB;							//存储器基地址，给定形参AddrB
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;			//存储器数据宽度，选择字节
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;					//存储器地址自增，选择使能
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;						//数据传输方向，选择由外设到存储器
	DMA_InitStructure.DMA_BufferSize = Size;								//转运的数据大小（转运次数）
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;							//模式，选择正常模式
	DMA_InitStructure.DMA_M2M = DMA_M2M_Enable;								//存储器到存储器，选择使能
	DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;					//优先级，选择中等
	DMA_Init(DMA1_Channel1, &DMA_InitStructure);							//将结构体变量交给DMA_Init，配置DMA1的通道1
	
	/*DMA使能*/
	DMA_Cmd(DMA1_Channel1, DISABLE);	//这里先不给使能，初始化后不会立刻工作，等后续调用Transfer后，再开始
}

/**
  * 函    数：启动DMA数据转运
  * 参    数：无
  * 返 回 值：无
  */
void MyDMA_Transfer(void)
{
	DMA_Cmd(DMA1_Channel1, DISABLE);					//DMA失能，在写入传输计数器之前，需要DMA暂停工作
	DMA_SetCurrDataCounter(DMA1_Channel1, MyDMA_Size);	//写入传输计数器，指定将要转运的次数
	DMA_Cmd(DMA1_Channel1, ENABLE);						//DMA使能，开始工作
	
	while (DMA_GetFlagStatus(DMA1_FLAG_TC1) == RESET);	//等待DMA工作完成
	DMA_ClearFlag(DMA1_FLAG_TC1);						//清除工作完成标志位
}

/**
  * 函    数：USART1 DMA初始化
  * 参    数：memoryAddr 内存地址
  * 参    数：size 数据大小
  * 返 回 值：无
  */
void MyDMA_USART1_Init(uint32_t memoryAddr, uint16_t size) {
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE); // 启用 DMA 时钟

    DMA_InitTypeDef DMA_InitStructure;
    DMA_DeInit(DMA1_Channel4); // USART1_TX 使用 DMA1 通道4

    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR; // USART1 数据寄存器地址
    DMA_InitStructure.DMA_MemoryBaseAddr = memoryAddr; // 内存地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST; // 从内存到外设
    DMA_InitStructure.DMA_BufferSize = size; // 数据大小
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; // 外设地址不自增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable; // 内存地址自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 外设数据宽度：字节
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; // 内存数据宽度：字节
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal; // 普通模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_High; // 高优先级
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable; // 禁止存储器到存储器

    DMA_Init(DMA1_Channel4, &DMA_InitStructure); // 初始化 DMA
}

/**
  * 函    数：启动USART1 DMA发送
  * 参    数：size 数据大小
  * 返 回 值：无
  */
void MyDMA_USART1_Start(uint16_t size) {
    DMA_Cmd(DMA1_Channel4, DISABLE); // 禁用 DMA 通道
    DMA_SetCurrDataCounter(DMA1_Channel4, size); // 设置数据大小
    DMA_Cmd(DMA1_Channel4, ENABLE); // 启用 DMA 通道

    USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE); // 启用 USART1 的 DMA 发送功能

    while (!DMA_GetFlagStatus(DMA1_FLAG_TC4)); // 等待传输完成
    DMA_ClearFlag(DMA1_FLAG_TC4); // 清除标志位
    DMA_Cmd(DMA1_Channel4, DISABLE); // 禁用 DMA 通道
}
