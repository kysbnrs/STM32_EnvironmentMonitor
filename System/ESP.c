#include "ESP.h"
#include "Delay.h"
#include "string.h"
#include "stm32f10x_usart.h" 
#include "stdio.h"
#include "Key.h"

// ========== USART3初始化（PB10=TX, PB11=RX）==========
void USART3_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    
    // USART3在APB1！不是APB2
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    // PB10 = USART3_TX，复用推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    // PB11 = USART3_RX，浮空输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &USART_InitStructure);
    USART_Cmd(USART3, ENABLE);
}

// ========== 发字符串 ==========
void UART3_SendString(char *str)
{
    while(*str)
    {
        while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
        USART_SendData(USART3, *str++);
    }
}

// ========== 发AT命令 + 等应答 + 超时 ==========
// 返回0=成功（收到expect），1=超时失败
uint8_t ESP_SendCmd(char *cmd, char *expect, uint32_t timeout_ms)
{
    char buf[256] = {0};
    uint16_t idx = 0;
    uint32_t start = GetTick_ms();
    
    // 只有cmd非空时才发命令
    if(strlen(cmd) > 0)
    {
        UART3_SendString(cmd);
        UART3_SendString("\r\n");
    }
    
    while(GetTick_ms() - start < timeout_ms)
    {
		
		Key_Scan();  // 加这行：等应答的时候也扫按键
		
        if(USART_GetFlagStatus(USART3, USART_FLAG_RXNE) == SET)
        {
            uint8_t ch = USART_ReceiveData(USART3);
            if(idx < sizeof(buf) - 1)
                buf[idx++] = ch;
            buf[idx] = '\0';
            
            if(strstr(buf, expect) != NULL)
                return 0;
        }
    }
    return 1;
}

typedef enum {
    ESP_IDLE = 0,
    ESP_CWMODE,
    ESP_CWJAP,
    ESP_CIPSTART,
    ESP_ONLINE
} ESP_State;

static ESP_State esp_state = ESP_IDLE;
static uint32_t check_time = 0;

// 改成你自己的
#define WIFI_SSID     "520"
#define WIFI_PASS     "88888888"
#define TCP_SERVER    "10.116.135.232"
#define TCP_PORT      "8080"

void ESP_ConnectStateMachine(void)
{
    char cmd[128];
    
    switch(esp_state)
    {
		case ESP_IDLE:
		    /* 先发AT测试ESP有没有响应 */
		    if(ESP_SendCmd("AT", "OK", 3000) == 0)
		    {
		        printf("[ESP] AT OK\r\n");
		        esp_state = ESP_CWMODE;
		    }
		    else
		    {
		        printf("[ESP] AT FAIL\r\n");
		        Delay_ms(1000);
		    }
		    break;
            
        case ESP_CWMODE:
            if(ESP_SendCmd("AT+CWMODE=1", "OK", 3000) == 0)
            {
                printf("[ESP] CWMODE OK\r\n");
                esp_state = ESP_CWJAP;
            }
            else
            {
                printf("[ESP] CWMODE FAIL\r\n");
                esp_state = ESP_IDLE;
                Delay_ms(1000);
            }
            break;
            
        case ESP_CWJAP:
            snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
            if(ESP_SendCmd(cmd, "WIFI GOT IP", 10000) == 0)
            {
                printf("[ESP] WiFi GOT IP\r\n");
				ESP_SendCmd("AT+CIFSR", "OK", 2000);  // 加这行：查询ESP的IP
                esp_state = ESP_CIPSTART;
            }
            else
            {
                printf("[ESP] WiFi FAIL\r\n");
                esp_state = ESP_IDLE;
                Delay_ms(1000);
            }
            break;
            
        case ESP_CIPSTART:
            snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%s", TCP_SERVER, TCP_PORT);
            if(ESP_SendCmd(cmd, "CONNECT", 5000) == 0)
            {
                printf("[ESP] TCP Connected\r\n");
                check_time = GetTick_ms();
                esp_state = ESP_ONLINE;
            }
            else
            {
                printf("[ESP] TCP FAIL\r\n");
                esp_state = ESP_IDLE;
                Delay_ms(1000);
            }
            break;
            
        case ESP_ONLINE:
            if(GetTick_ms() - check_time >= 5000)
            {
                check_time = GetTick_ms();
                if(ESP_SendCmd("AT+CIPSTATUS", "STATUS:3", 2000) != 0)
                {
                    printf("[ESP] Reconnecting...\r\n");
                    esp_state = ESP_IDLE;
                }
            }
            break;
    }
}

uint8_t ESP_SendData(char *data)
{
    char cmd[64];
    uint16_t len = strlen(data);
    
    if(esp_state != ESP_ONLINE)
        return 1;
    
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", len);
    if(ESP_SendCmd(cmd, ">", 2000) != 0)
    {
        esp_state = ESP_IDLE;
        return 1;
    }
    
    UART3_SendString(data);
    
    if(ESP_SendCmd("", "SEND OK", 3000) != 0)
    {
        esp_state = ESP_IDLE;
        return 1;
    }
    
    return 0;
}

uint8_t ESP_IsOnline(void)
{
    return (esp_state == ESP_ONLINE);
}

