#ifndef __ST7735_H
#define __ST7735_H

#include "stm32f10x.h"
#include "MySPI.h"
#include "Delay.h"
#include "stimage.h"
#include <string.h>

#define ST7735_WIDTH   128
#define ST7735_HEIGHT  160
#define	ST7735_BLACK   0x0000
#define	ST7735_BLUE    0x001F
#define	ST7735_RED     0xF800
#define	ST7735_GREEN   0x07E0
#define ST7735_CYAN    0x07FF
#define ST7735_MAGENTA 0xF81F
#define ST7735_YELLOW  0xFFE0
#define ST7735_WHITE   0xFFFF

typedef struct LocationStart{
    uint8_t HeightStart;
    uint8_t WidthStart;
    uint8_t length;
} LocationStart;

void Z_ST7735S_SetRST(uint8_t val);
void Z_ST7735S_SetDC(uint8_t val);
void Z_ST7735S_SendCommand(uint8_t command);
void Z_ST7735S_SendData(uint8_t data);
void Z_ST7735S_Send16bitsRGB(uint16_t rgb);
void Z_ST7735S_Init(void);
void Z_ST7735S_SpecifyScope(uint8_t xs,uint8_t xe,uint8_t ys,uint8_t ye);
void Z_ST7735S_RefreshAll(uint16_t rgb);
void Z_ST7735S_RefreshAll_DMA(uint16_t rgb);
void Z_ST7735_ShowNum(int x,int y,int num,uint16_t rgb);
void Z_ST7735_ShowLetter(int x,int y,char ch,uint16_t rgb);
void Z_ST7735_ShowImg(int x,int y,char* str);
void Z_ST7735_ShowImg_DMA(int x,int y,char* str);
void Z_ST7735_ShowString(int x,int y,char *str,int cnt,uint16_t rgb);
void Z_ST7735_ShowChar(int x, int y, char ch, uint16_t rgb);

#endif