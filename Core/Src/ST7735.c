#include "ST7735.h"
#include "fonts.h"

#define Z_ST7735S_RST_PORT LCD_RST_GPIO_Port
#define Z_ST7735S_RST_GPIO LCD_RST_Pin

#define Z_ST7735S_DC_PORT LCD_DC_GPIO_Port
#define Z_ST7735S_DC_GPIO LCD_DC_Pin

#define Z_ST7735S_CS_PORT LCD_CS_GPIO_Port
#define Z_ST7735S_CS_GPIO LCD_CS_Pin

#define Z_ST7735S_WIDTH  128
#define Z_ST7735S_HEIGHT 160

void Z_ST7735_ShowNum(int x,int y,int num,uint16_t rgb) {
    if (num < 0 || num > 9) return;
    Z_ST7735S_SpecifyScope(x,x+7,y,y+15);
    for(int i = 0;i<16;i++){
        uint8_t tmp = Z_ST7735S_Font[num][i];
        for(int j = 0;j<8;j++){
            if((tmp&0x80) == 0x80)Z_ST7735S_Send16bitsRGB(rgb);
            else Z_ST7735S_Send16bitsRGB(ST7735_BLACK);
            tmp = tmp<<1;
        }
        
    }
}

void Z_ST7735_ShowString(int x,int y,const char *str,int cnt,uint16_t rgb){
    for(int i = 0;i<cnt ;i++){
        Z_ST7735_ShowChar(x+i*8,y,str[i],rgb);
    }
}

void Z_ST7735_ShowChar(int x,int y,char ch,uint16_t rgb){
    Z_ST7735S_SpecifyScope(x,x+7,y,y+15);
    
    const uint8_t *font_data = NULL;
    
    if(ch >= '0' && ch <= '9') {
        font_data = Z_ST7735S_Font[ch-'0'];
    }
    else if(ch >= 'A' && ch <= 'Z') {
        font_data = Z_ST7735S_FontAlpha[ch-'A'];
    }
    else if(ch >= 'a' && ch <= 'z') {
        font_data = Z_ST7735S_FontAlphaLower[ch-'a'];
    }
    else if(ch == ' ') {
        font_data = Z_ST7735S_FontSpace;
    }
    else if(ch == ':') {
        font_data = Z_ST7735S_FontColon;
    }
    else if(ch == '-') {
        font_data = Z_ST7735S_FontMinus;
    }
    else if(ch == '%') {
        font_data = Z_ST7735S_FontPercent;
    }
    else if(ch == '"'){//" = °
        font_data = Z_ST7735S_Du;
    }
    else {
        // 不支持的字符，显示空格
        font_data = Z_ST7735S_FontSpace;
    }
    
    for(int i = 0;i<16;i++){
        uint8_t tmp = font_data[i];
        for(int j = 0;j<8;j++){
            if((tmp&0x80) == 0x80)Z_ST7735S_Send16bitsRGB(rgb);
            else Z_ST7735S_Send16bitsRGB(ST7735_BLACK);
            tmp = tmp<<1;
        }
    }
}


void Z_ST7735S_SetRST(uint8_t val){
    if(val==0) HAL_GPIO_WritePin(Z_ST7735S_RST_PORT, Z_ST7735S_RST_GPIO, GPIO_PIN_RESET);
    else HAL_GPIO_WritePin(Z_ST7735S_RST_PORT, Z_ST7735S_RST_GPIO, GPIO_PIN_SET);
}

void Z_ST7735S_SetDC(uint8_t val){
    if(val==0) HAL_GPIO_WritePin(Z_ST7735S_DC_PORT, Z_ST7735S_DC_GPIO, GPIO_PIN_RESET);
    else HAL_GPIO_WritePin(Z_ST7735S_DC_PORT, Z_ST7735S_DC_GPIO, GPIO_PIN_SET);
}

static void Z_ST7735S_SetCS(uint8_t val){
    if(val==0) HAL_GPIO_WritePin(Z_ST7735S_CS_PORT, Z_ST7735S_CS_GPIO, GPIO_PIN_RESET);
    else HAL_GPIO_WritePin(Z_ST7735S_CS_PORT, Z_ST7735S_CS_GPIO, GPIO_PIN_SET);
}

static void Z_ST7735S_SendByte(uint8_t data){
    HAL_SPI_Transmit(&hspi3, &data, 1, HAL_MAX_DELAY);
}

void Z_ST7735S_SendCommand(uint8_t command){
    Z_ST7735S_SetDC(0);    // D/C = 0 for command
    Z_ST7735S_SetCS(0);
    Z_ST7735S_SendByte(command);
    Z_ST7735S_SetCS(1);
}

void Z_ST7735S_SendData(uint8_t data){
    Z_ST7735S_SetDC(1);    // D/C = 1 for data
    Z_ST7735S_SetCS(0);
    Z_ST7735S_SendByte(data);
    Z_ST7735S_SetCS(1);
}
 
void Z_ST7735S_Send16bitsRGB(uint16_t rgb){
    Z_ST7735S_SendData(rgb>>8);
    Z_ST7735S_SendData(rgb);
}
void Z_ST7735S_Init(void){
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
    Z_ST7735S_SetCS(1);
    Z_ST7735S_SetDC(0);
    Z_ST7735S_SetRST(0);
    HAL_Delay(1);
    Z_ST7735S_SetRST(1);
    HAL_Delay(120);
    //厂家提供的固定的初始化代码
    Z_ST7735S_SendCommand(0x11); //Sleep out
    HAL_Delay(120);              //Delay 120ms
    //------------------------------------ST7735S Frame Rate-----------------------------------------// 
    Z_ST7735S_SendCommand(0xB1); 
    Z_ST7735S_SendData(0x05); 
    Z_ST7735S_SendData(0x3C); 
    Z_ST7735S_SendData(0x3C); 
    Z_ST7735S_SendCommand(0xB2); 
    Z_ST7735S_SendData(0x05); 
    Z_ST7735S_SendData(0x3C); 
    Z_ST7735S_SendData(0x3C); 
    Z_ST7735S_SendCommand(0xB3); 
    Z_ST7735S_SendData(0x05); 
    Z_ST7735S_SendData(0x3C); 
    Z_ST7735S_SendData(0x3C); 
    Z_ST7735S_SendData(0x05); 
    Z_ST7735S_SendData(0x3C); 
    Z_ST7735S_SendData(0x3C); 
    //------------------------------------End ST7735S Frame Rate---------------------------------// 
    Z_ST7735S_SendCommand(0xB4); //Dot inversion 
    Z_ST7735S_SendData(0x03); 
    //------------------------------------ST7735S Power Sequence---------------------------------// 
    Z_ST7735S_SendCommand(0xC0); 
    Z_ST7735S_SendData(0x28); 
    Z_ST7735S_SendData(0x08); 
    Z_ST7735S_SendData(0x04); 
    Z_ST7735S_SendCommand(0xC1); 
    Z_ST7735S_SendData(0XC0); 
    Z_ST7735S_SendCommand(0xC2); 
    Z_ST7735S_SendData(0x0D); 
    Z_ST7735S_SendData(0x00); 
    Z_ST7735S_SendCommand(0xC3); 
    Z_ST7735S_SendData(0x8D); 
    Z_ST7735S_SendData(0x2A); 
    Z_ST7735S_SendCommand(0xC4); 
    Z_ST7735S_SendData(0x8D); 
    Z_ST7735S_SendData(0xEE); 
    //---------------------------------End ST7735S Power Sequence-------------------------------------// 
    Z_ST7735S_SendCommand(0xC5); //VCOM 
    Z_ST7735S_SendData(0x1A); 
    Z_ST7735S_SendCommand(0x36); //MX, MY, RGB mode 
    Z_ST7735S_SendData(0xC0); 
    //------------------------------------ST7735S Gamma Sequence---------------------------------// 
    Z_ST7735S_SendCommand(0xE0); 
    Z_ST7735S_SendData(0x04); 
    Z_ST7735S_SendData(0x22); 
    Z_ST7735S_SendData(0x07); 
    Z_ST7735S_SendData(0x0A); 
    Z_ST7735S_SendData(0x2E); 
    Z_ST7735S_SendData(0x30); 
    Z_ST7735S_SendData(0x25); 
    Z_ST7735S_SendData(0x2A); 
    Z_ST7735S_SendData(0x28); 
    Z_ST7735S_SendData(0x26); 
    Z_ST7735S_SendData(0x2E); 
    Z_ST7735S_SendData(0x3A); 
    Z_ST7735S_SendData(0x00); 
    Z_ST7735S_SendData(0x01); 
    Z_ST7735S_SendData(0x03); 
    Z_ST7735S_SendData(0x13); 
    Z_ST7735S_SendCommand(0xE1); 
    Z_ST7735S_SendData(0x04); 
    Z_ST7735S_SendData(0x16); 
    Z_ST7735S_SendData(0x06); 
    Z_ST7735S_SendData(0x0D); 
    Z_ST7735S_SendData(0x2D); 
    Z_ST7735S_SendData(0x26); 
    Z_ST7735S_SendData(0x23); 
    Z_ST7735S_SendData(0x27); 
    Z_ST7735S_SendData(0x27); 
    Z_ST7735S_SendData(0x25); 
    Z_ST7735S_SendData(0x2D); 
    Z_ST7735S_SendData(0x3B); 
    Z_ST7735S_SendData(0x00); 
    Z_ST7735S_SendData(0x01); 
    Z_ST7735S_SendData(0x04); 
    Z_ST7735S_SendData(0x13); 
    //------------------------------------End ST7735S Gamma Sequence-----------------------------// 
    Z_ST7735S_SendCommand(0x3A); //设置颜色模式
    //65k mode 
    Z_ST7735S_SendData(0x05); 
    Z_ST7735S_SendCommand(0x29); //Display on 
}    

//指定范围
void Z_ST7735S_SpecifyScope(uint8_t xs,uint8_t xe,uint8_t ys,uint8_t ye){
    Z_ST7735S_SendCommand(0x2A);    //指定列范围
    Z_ST7735S_SendData(0x00);
    Z_ST7735S_SendData(xs);
    Z_ST7735S_SendData(0x00);
    Z_ST7735S_SendData(xe);
    
    Z_ST7735S_SendCommand(0x2B);    //指定行范围
    Z_ST7735S_SendData(0x00);
    Z_ST7735S_SendData(ys);
    Z_ST7735S_SendData(0x00);
    Z_ST7735S_SendData(ye);
    
    Z_ST7735S_SendCommand(0x2C);    //开始内存写入
}
 
void Z_ST7735S_RefreshAll(uint16_t rgb){
    Z_ST7735S_SpecifyScope(0,127,0,159);
    for(uint16_t j=0;j<160;++j){
        for(uint16_t i=0;i<128;++i){
            Z_ST7735S_Send16bitsRGB(rgb);
        }
    }
}


void Z_ST7735_ShowImg(int x,int y,const char* str){
    (void)x;
    (void)y;
    (void)str;
    /* 图片资源未移植，暂不显示 */
}

void Z_ST7735_ShowImg_DMA(int x,int y,const char* str){
    (void)x;
    (void)y;
    (void)str;
    /* 图片资源和 SPI DMA 发送未移植，暂不显示 */
}
