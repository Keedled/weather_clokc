#include"esp32_c3.h"
void esp32_c3_init(void)
{
    memset(rx_buf,0,RX_BUF_SIZE);
    //设置一下SNTP服务器
    const char sntp_enable[] = "AT+CIPSNTPCFG=1,8,\"cn.ntp.org.cn\",\"ntp.sjtu.edu.cn\"\r\n";
	USART_Write_String(sntp_enable); 
    for(int i = 0; i < RX_BUF_SIZE; i++) {
			rx_buf[i] = USART_Receive_Data();
            if ( i >= 1 && rx_buf[i-1] == 'O' && rx_buf[i] == 'K') { // OK结束
				rx_buf[i + 1] = '\0';
				break;
			}
			if( i >= 4 && rx_buf[i-4] == 'E' && rx_buf[i-3] == 'R' && rx_buf[i-2] == 'R' && rx_buf[i-1] == 'O' && rx_buf[i] == 'R'){
				rx_buf[i + 1] = '\0';
				break;
			}
    }
}

void esp32_c3_get_time(Time *time)
{
    memset(rx_buf,0,RX_BUF_SIZE);
    char sntp_get[] = "AT+CIPSNTPTIME?\r\n";
	USART_Write_String(sntp_get); 
    for(int i = 0; i < RX_BUF_SIZE; i++) {
			rx_buf[i] = USART_Receive_Data();
            if ( i >= 1 && rx_buf[i-1] == 'O' && rx_buf[i] == 'K') { // OK结束
				rx_buf[i + 1] = '\0';
				break;
			}
			if( i >= 4 && rx_buf[i-4] == 'E' && rx_buf[i-3] == 'R' && rx_buf[i-2] == 'R' && rx_buf[i-1] == 'O' && rx_buf[i] == 'R'){
				rx_buf[i + 1] = '\0';
				break;
			}
    }
    memcpy(time->week,  rx_buf+ 31, 3);
    memcpy(time->month, rx_buf+ 35, 3);
    memcpy(time->day,   rx_buf +39, 2);
    memcpy(time->hour,  rx_buf+ 42, 2);
    memcpy(time->min,   rx_buf+ 45, 2);
    memcpy(time->year,  year,       4);

}

void esp32_c3_get_weather(Weather *weather)
{
    memset(rx_buf,0,RX_BUF_SIZE);
    const char http_cmd[] = "AT+HTTPCGET=\"https://api.seniverse.com/v3/weather/now.json?key=SwSCOVN3uoHLMsZOG&location=beijing&language=en&unit=c\"\r\n";
    USART_Write_String(http_cmd);
		//Delay_s(2);
    for(int i = 0; i < RX_BUF_SIZE; i++) {
        rx_buf[i] = USART_Receive_Data();
        if ( i >= 1 && rx_buf[i-1] == 'O' && rx_buf[i] == 'K') { // OK结束
            rx_buf[i + 1] = '\0';
            break;
        }
        if( i >= 4 && rx_buf[i-4] == 'E' && rx_buf[i-3] == 'R' && rx_buf[i-2] == 'R' && rx_buf[i-1] == 'O' && rx_buf[i] == 'R'){
            rx_buf[i + 1] = '\0';
            break;
        }
    }
		
    memcpy(weather->location,     rx_buf+ 178, 7);
    memcpy(weather->temperature,  rx_buf+ 336, 1);
    memcpy(weather->weather,      rx_buf+ 303, 5);
    // 确保字符串以 \0 结尾
    weather->weather[5] = '\0';
}

void esp32_c3_get_network(Network *network)
{
    memset(rx_buf,0,RX_BUF_SIZE);
    char wifi_get[] = "AT+CWSTATE?\r\n";
	USART_Write_String(wifi_get); 
    for(int i = 0; i < RX_BUF_SIZE; i++) {
			rx_buf[i] = USART_Receive_Data();
            if ( i >= 1 && rx_buf[i-1] == 'O' && rx_buf[i] == 'K') { // OK结束
				rx_buf[i + 1] = '\0';
				break;
			}
			if( i >= 4 && rx_buf[i-4] == 'E' && rx_buf[i-3] == 'R' && rx_buf[i-2] == 'R' && rx_buf[i-1] == 'O' && rx_buf[i] == 'R'){
				rx_buf[i + 1] = '\0';
				break;
			}
    }
    network->state = rx_buf[23];
    memcpy(network->ssid,      rx_buf+ 26, 6);
}