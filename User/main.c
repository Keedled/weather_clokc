#include "stm32f10x.h"                  // Device header
#include "MyUSART.h"  
#include "MyDMA.h"
#include "MySPI.h"
#include "ST7735.h"
#include "esp32_c3.h"                   // 已正确包含 esp32_c3.h
#include <string.h>                     // 添加字符串处理头文件

// FreeRTOS 头文件
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

//这里定义一些全局变量，用来表示时间、天气、网络状态等显示的起始位置
LocationStart Year_Mon_Day = {.HeightStart = 0, .WidthStart = 0, .length = 5};
LocationStart Tempture = {.HeightStart = 0, .WidthStart = 12*8, .length = 3};
LocationStart Hour_Min = {.HeightStart = 5*16, .WidthStart = 6*8, .length = 6};
LocationStart WIFI = {.HeightStart = 6*16, .WidthStart = 0, .length = 17};
LocationStart Weather_L = {.HeightStart = 16, .WidthStart = 0, .length = 48};
LocationStart City = {.HeightStart = 32, .WidthStart = 56, .length = 48};
LocationStart ConutLocation = {.HeightStart = 7*16, .WidthStart = 0, .length = 0};


// ==================== FreeRTOS 任务函数定义 ====================

// 互斥锁：保护USART/ESP32通信（多任务共享串口，必须加锁）
SemaphoreHandle_t xUartMutex;
//计数器
int Count = 0;
/**
 * 辅助函数：显示天气图标
 */
static void ShowWeatherImg(char *weather_str) {
    if(strcmp(weather_str, "Clear") == 0 || strcmp(weather_str, "Sunny") == 0) {
        Z_ST7735_ShowImg(Weather_L.WidthStart, Weather_L.HeightStart, "qing");
    }
    else if(strcmp(weather_str, "Cloud") == 0 || strcmp(weather_str, "Overc") == 0) {
        Z_ST7735_ShowImg(Weather_L.WidthStart, Weather_L.HeightStart, "duoyun");
    }
    else if(strcmp(weather_str, "rain") == 0) {
        Z_ST7735_ShowImg(Weather_L.WidthStart, Weather_L.HeightStart, "yu");
    }
    else if(strcmp(weather_str, "snow") == 0) {
        Z_ST7735_ShowImg(Weather_L.WidthStart, Weather_L.HeightStart, "xue");
    }
    else if(strcmp(weather_str, "feng") == 0) {
        Z_ST7735_ShowImg(Weather_L.WidthStart, Weather_L.HeightStart, "feng");
    }
    else {
        Z_ST7735_ShowString(Weather_L.WidthStart, Weather_L.HeightStart, weather_str, 5, ST7735_WHITE);
    }
}

/**
 * 任务函数1：天气更新任务
 * 定期获取天气信息并更新显示（温度 + 天气图标 + 城市）
 */
void vWeatherTask(void *pvParameters) {
    while(1) {
        Weather weather;
        
        // 获取互斥锁，保护串口通信
        if(xSemaphoreTake(xUartMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
            esp32_c3_get_weather(&weather);
            
            
            // 更新温度显示
            char tempture[4];
            snprintf(tempture, sizeof(tempture), " %c\"", weather.temperature[0]);
            Z_ST7735_ShowString(Tempture.WidthStart, Tempture.HeightStart, tempture, 3, ST7735_WHITE);
            
            // 更新天气图标
            ShowWeatherImg(weather.weather);
            
            // 更新城市
            Z_ST7735_ShowString(City.WidthStart, City.HeightStart, weather.location, 7, ST7735_WHITE);

			xSemaphoreGive(xUartMutex);  // 释放互斥锁
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000));  // 60秒更新一次
    }
}

/**
 * 任务函数2：时间显示任务
 * 定期更新时间显示
 */
void vTimeTask(void *pvParameters) {
    while(1) {
        Time time;
        
        if(xSemaphoreTake(xUartMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
            esp32_c3_get_time(&time);
            
            
            // 更新年月日
            char year_mon_day[13];
            snprintf(year_mon_day, sizeof(year_mon_day),
                    "2026:%.*s:%.*s",
                    3, time.month,
                    2, time.day);
            Z_ST7735_ShowString(Year_Mon_Day.WidthStart, Year_Mon_Day.HeightStart, year_mon_day, 12, ST7735_WHITE);
            
            // 更新时分
            char hour_min[6];
            snprintf(hour_min, sizeof(hour_min), "%.*s:%.*s", 2, time.hour, 2, time.min);
            Z_ST7735_ShowString(Hour_Min.WidthStart, Hour_Min.HeightStart, hour_min, strlen(hour_min), ST7735_WHITE);
			xSemaphoreGive(xUartMutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10秒更新一次
    }
}

/**
 * 任务函数3：网络状态任务
 * 定期检查网络状态
 */
void vNetworkTask(void *pvParameters) {
    while(1) {
        Network network;
        
        if(xSemaphoreTake(xUartMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
            esp32_c3_get_network(&network);
            
            
            if(network.state == '2') {
                char wifi_state[18];
                snprintf(wifi_state, sizeof(wifi_state), "Connected:%s", network.ssid);
                Z_ST7735_ShowString(WIFI.WidthStart, WIFI.HeightStart, wifi_state, 17, ST7735_WHITE);
            }
			xSemaphoreGive(xUartMutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000));  // 30秒检查一次
    }
}

void vCountTask(void *pvParameters) {
    while(1) {
        // 显示计数器
        if(xSemaphoreTake(xUartMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
            Z_ST7735_ShowChar(ConutLocation.WidthStart, ConutLocation.HeightStart, Count+'0', ST7735_WHITE);
            xSemaphoreGive(xUartMutex);
        }
        
        // 计数器递增（在释放锁之后）
        Count++;
        if(Count > 9) Count = 0;
        
        vTaskDelay(pdMS_TO_TICKS(2000));  // 2秒增加一次
    }
}

int main(void) {
    // ====== 硬件初始化（只做一次，在调度器启动前完成）======
    Z_ST7735S_Init();
    MyUSART_Init();
    Z_ST7735S_RefreshAll(ST7735_BLACK);
    USART1_FlushRx();
    esp32_c3_init();
    
    // ====== 创建互斥锁（必须在创建任务之前）======
    xUartMutex = xSemaphoreCreateMutex();
    
    // 检查互斥锁是否创建成功
    if(xUartMutex == NULL) {
        // 互斥锁创建失败，显示错误信息
        Z_ST7735_ShowString(0, 0, "Mutex Failed!", 13, ST7735_RED);
        while(1);  // 卡住，不继续运行
    }
    
    // ====== 初始显示：获取一次数据并显示 ======
    // 这样启动时不需要等待任务延迟，立即看到内容
    Time time;
    Weather weather;
    Network network;
    
    if(xSemaphoreTake(xUartMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        esp32_c3_get_time(&time);
        Delay_s(1);
        esp32_c3_get_weather(&weather);
        Delay_s(1);
        esp32_c3_get_network(&network);
        xSemaphoreGive(xUartMutex);
        
        // 显示时间
        char year_mon_day[13];
        snprintf(year_mon_day, sizeof(year_mon_day),
                "2026:%.*s:%.*s",
                3, time.month,
                2, time.day);
        Z_ST7735_ShowString(Year_Mon_Day.WidthStart, Year_Mon_Day.HeightStart, year_mon_day, 12, ST7735_WHITE);
        
        char hour_min[6];
        snprintf(hour_min, sizeof(hour_min), "%.*s:%.*s", 2, time.hour, 2, time.min);
        Z_ST7735_ShowString(Hour_Min.WidthStart, Hour_Min.HeightStart, hour_min, strlen(hour_min), ST7735_WHITE);
        
        // 显示天气
        char tempture[4];
        snprintf(tempture, sizeof(tempture), " %c\"", weather.temperature[0]);
        Z_ST7735_ShowString(Tempture.WidthStart, Tempture.HeightStart, tempture, 3, ST7735_WHITE);
        
        ShowWeatherImg(weather.weather);
        Z_ST7735_ShowString(City.WidthStart, City.HeightStart, weather.location, 7, ST7735_WHITE);
        
        // 显示网络
        if(network.state == '2') {
            char wifi_state[18];
            snprintf(wifi_state, sizeof(wifi_state), "Connected:%s", network.ssid);
            Z_ST7735_ShowString(WIFI.WidthStart, WIFI.HeightStart, wifi_state, 17, ST7735_WHITE);
        }
		//显示一个计数器
		Z_ST7735_ShowChar(ConutLocation.WidthStart, ConutLocation.HeightStart, Count+'0', ST7735_WHITE);
    } else {
        Z_ST7735_ShowString(0, 0, "Get Lock Failed!", 15, ST7735_RED);
        while(1);
    }
    
    // ====== 创建 FreeRTOS 任务 ======
    // 任务启动后各自负责获取数据并更新显示，不需要在main中重复做
    xTaskCreate(vWeatherTask, "Weather", 128, NULL, 2, NULL);
    xTaskCreate(vTimeTask,    "Time",    128, NULL, 2, NULL);
    xTaskCreate(vNetworkTask, "Network", 128, NULL, 1, NULL);
    xTaskCreate(vCountTask,   "Count",   128, NULL, 1, NULL);  // 添加计数器任务
    
    // ====== 启动 FreeRTOS 调度器 ======
    vTaskStartScheduler();
    
    // 调度器启动失败才会执行到这里
    while(1) {
    }
}