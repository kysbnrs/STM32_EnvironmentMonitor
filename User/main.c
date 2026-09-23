/*
 * STM32环境监测终端 - 裸机版本
 *
 * 功能:
 *   1. DHT22实时采集温湿度
 *   2. OLED显示温度、湿度、历史记录
 *   3. ESP8266 WiFi上传数据到服务器
 *   4. W25Q64 Flash存储历史记录, 断电不丢失
 *   5. 温湿度超限报警
 */

#include "stm32f10x.h"
#include "stm32f10x_usart.h"
#include "Delay.h"
#include "stdio.h"
#include "DHT22.h"
#include "I2C.h"
#include "OLED.h"
#include "Key.h"
#include "LED.h"
#include "Buzzer.h"
#include "Alarm.h"
#include "ESP.h"
#include "W25Q64.h"

/* ==================== 硬件初始化函数 ==================== */
void USART1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}

int fputc(int ch, FILE *f)
{
    while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

/* ==================== 主函数 ==================== */
int main(void)
{
    float temp, humi;
    Record_t last_record;
    uint32_t last_read_tick = 0;
    uint8_t has_last_record;
    char buf[32];

    /* ---- 1. 初始化SysTick (1ms中断，Delay_ms和按键扫描都靠它) ---- */
    SysTick_Config(SystemCoreClock / 1000);

    /* ---- 2. 硬件初始化 ---- */
    Delay_ms(1000);  // 等上电稳定

    LED_Init();
    OLED_Init();
    USART1_Init();
    W25Q64_Init();
    DHT22_Init();
    Key_Init();
    Buzzer_Init();
    USART3_Init();
    Delay_ms(5000);  // 等ESP上电稳定，从2秒加到5秒

    /* ---- 2. 上电自检 ---- */
    OLED_Clear();
    OLED_ShowString(0, 0, "System Ready");
    printf("\r\n=== System Starting ===\r\n");

    /* LED闪3次 */
    for(int i = 0; i < 3; i++)
    {
        LED_On();
        Delay_ms(200);
        LED_Off();
        Delay_ms(200);
    }

    /* ---- 3. 读上次断电前的记录 ---- */
    has_last_record = W25Q64_ReadLatestRecord(&last_record);
    if(has_last_record == 0)
    {
        printf("Last Record: T=%.1f H=%.1f\r\n", last_record.temp, last_record.humi);
    }

    /* ---- 4. 主循环 ---- */
    while(1)
    {
        /* 按键扫描 */
        Key_Scan();

        /* ESP连接状态机 (非阻塞, 自动重连) */
        ESP_ConnectStateMachine();

        /* 每2秒读一次DHT22 */
        if(GetTick_ms() - last_read_tick >= 2000)
        {
            last_read_tick = GetTick_ms();

            /* 读DHT22 */
            if(DHT22_ReadData(&temp, &humi) == 0)
            {
                printf("T:%.1f H:%.1f\r\n", temp, humi);

                /* OLED显示温度 (第0行)，补空格清残留 */
                sprintf(buf, "T:%.1f C      ", temp);
                OLED_ShowString(0, 0, buf);

                /* OLED显示湿度 (第2行)，补空格清残留 */
                sprintf(buf, "H:%.1f %%     ", humi);
                OLED_ShowString(2, 0, buf);

                /* 报警检查 */
                Alarm_Check(temp, humi);

                /* 写W25Q64记录 */
                W25Q64_WriteRecord(temp, humi);

                /* ESP在线则上传数据 */
                if(ESP_IsOnline())
                {
                    sprintf(buf, "T:%.1f,H:%.1f", temp, humi);
                    ESP_SendData(buf);
                }

                /* OLED显示上次记录 (第6行)，补空格清残留 */
                if(has_last_record == 0)
                {
                    sprintf(buf, "Last:%.1f,%.1f  ", last_record.temp, last_record.humi);
                    OLED_ShowString(6, 0, buf);
                }
            }
            else
            {
                printf("DHT22 Read Error!\r\n");
            }
        }
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
    while(1);
}
#endif
