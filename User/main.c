#include "stm32f10x.h"
#include "stm32f10x_usart.h"
#include "misc.h"
#include <stdio.h>
#include <stdarg.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "Delay.h"
#include "DHT22.h"
#include "I2C.h"
#include "OLED.h"
#include "Key.h"
#include "LED.h"
#include "Buzzer.h"
#include "Alarm.h"
#include "ESP.h"
#include "W25Q64.h"

/* ==================== 数据结构 ==================== */
typedef struct {
    float temp;
    float humi;
    uint8_t ok;      /* 1=采集成功，0=失败 */
    uint8_t alarm;   /* 1=超限报警，0=正常 */
} SensorData_t;

/* ==================== 句柄 ==================== */
QueueHandle_t     xSensorQueue;     /* 传感器数据队列：Sensor→Display */
SemaphoreHandle_t xOledMutex;       /* OLED互斥量 */
SemaphoreHandle_t xPrintfMutex;     /* 串口打印互斥量 */
SemaphoreHandle_t xFlashMutex;      /* Flash读写互斥量 */
SemaphoreHandle_t xDataMutex;       /* 全局数据互斥量 */
SemaphoreHandle_t xKeySemaphore;    /* 按键二值信号量 */
TimerHandle_t     xStatusTimer;     /* 状态刷新定时器(100ms)：LED+蜂鸣器的唯一控制者 */

/* 全局最新数据（WifiTask用，互斥量保护） */
SensorData_t g_latest_data = {0};

/* ==================== 人机交互/系统状态标志 ====================
 * 均为单字节uint8_t，在Cortex-M3上读写天然原子，用volatile保证
 * 各任务/中断可见，无需互斥量。KeyTask写，状态定时器/显示任务读。 */
volatile uint8_t g_armed = 1;   /* 布防：1=布防(允许报警)，0=撤防(长按切换) */
volatile uint8_t g_muted = 0;   /* 消音：1=本次报警静音(单击)，恢复正常自动清零 */
volatile uint8_t g_alarm = 0;   /* 当前是否报警(由SensorTask更新) */

/* ==================== 函数声明 ==================== */
void SensorTask(void *pvParameters);
void DisplayTask(void *pvParameters);
void WifiTask(void *pvParameters);
void KeyTask(void *pvParameters);
void Hardware_Init(void);
void USART1_Init(void);
void PrintfSafe(const char *fmt, ...);
void StatusTimerCallback(TimerHandle_t xTimer);

/* ==================== 线程安全的printf ==================== */
void PrintfSafe(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if(xSemaphoreTake(xPrintfMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        printf("%s", buf);
        xSemaphoreGive(xPrintfMutex);
    }
}

/* ==================== 状态刷新定时器回调（100ms周期） ====================
 * LED和蜂鸣器的【唯一控制者】，统一由系统状态渲染，避免多任务抢硬件。
 * LED状态优先级：撤防常灭 > 报警快闪 > 在线常亮 > 离线慢闪(500ms)。
 * 蜂鸣器：仅当 报警 && 布防 && 未消音 时响。
 * 回调内只做GPIO快操作、不阻塞、不取互斥量。 */
void StatusTimerCallback(TimerHandle_t xTimer)
{
    static uint8_t slow_cnt = 0;
    uint8_t online = ESP_IsOnline();   /* 单字节读，原子 */

    /* ---- LED状态灯 ---- */
    if(!g_armed) {
        LED_Off();                     /* 撤防：灯灭，表示未监控 */
        slow_cnt = 0;
    }
    else if(g_alarm) {
        LED_Toggle();                  /* 报警：每100ms翻转=快闪 */
        slow_cnt = 0;
    }
    else if(online) {
        LED_On();                      /* 正常且在线：常亮 */
        slow_cnt = 0;
    }
    else {
        if(++slow_cnt >= 5) {          /* 正常但离线：500ms翻转=慢闪 */
            slow_cnt = 0;
            LED_Toggle();
        }
    }

    /* ---- 蜂鸣器（报警声） ---- */
    if(g_alarm && g_armed && !g_muted)
        Buzzer_On();
    else
        Buzzer_Off();
}

/* ==================== 主函数 ==================== */
int main(void)
{
    /* FreeRTOS强制要求：NVIC必须用全抢占分组（4位抢占、0位子优先级）。
       否则NVIC_Init写入的中断优先级会被错误编码，在ISR中调用
       xSemaphoreGiveFromISR等FromISR接口会触发configASSERT，
       表现为开串口中断/按按键后系统无打印直接死机。 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 先初始化硬件（此时调度器未启动，不存在任务竞争，不需要互斥量） */
    Hardware_Init();
    printf("\r\n=== FreeRTOS 环境监测启动 ===\r\n");

    /* 创建互斥量（LED/蜂鸣器由状态定时器单一控制，不需要互斥量） */
    xOledMutex    = xSemaphoreCreateMutex();
    xPrintfMutex  = xSemaphoreCreateMutex();
    xFlashMutex   = xSemaphoreCreateMutex();
    xDataMutex    = xSemaphoreCreateMutex();

    /* 创建二值信号量（按键用） */
    xKeySemaphore = xSemaphoreCreateBinary();

    /* 创建队列 */
    xSensorQueue = xQueueCreate(5, sizeof(SensorData_t));

    /* 创建状态刷新软件定时器（100ms周期，自动重载，统一管LED+蜂鸣器） */
    xStatusTimer = xTimerCreate("Status", pdMS_TO_TICKS(100), pdTRUE, 0, StatusTimerCallback);
    xTimerStart(xStatusTimer, 0);

    /* 创建任务 */
    xTaskCreate(SensorTask,  "Sensor",  256, NULL, 3, NULL);
    xTaskCreate(DisplayTask, "Display", 256, NULL, 2, NULL);
    xTaskCreate(WifiTask,    "Wifi",    512, NULL, 2, NULL);
    xTaskCreate(KeyTask,     "Key",     128, NULL, 1, NULL);

    vTaskStartScheduler();
    while(1);
}

/* ==================== 硬件初始化（调度器启动前调用，无需互斥量） ==================== */
void Hardware_Init(void)
{
    LED_Init();
    OLED_Init();
    USART1_Init();
    W25Q64_Init();
    DHT22_Init();
    Key_Init();
    Buzzer_Init();
    USART3_Init();

    /* 开机清屏（不再忙等5秒：那是调试遗留。
       ESP8266上电若未就绪，WifiTask的AT状态机会自动重试，不阻塞启动） */
    OLED_Clear();
}

/* ==================== 传感器任务（生产者） ==================== */
void SensorTask(void *pvParameters)
{
    float temp, humi;
    SensorData_t data;

    while(1)
    {
        if(DHT22_ReadData(&temp, &humi) == 0) {
            data.temp = temp;
            data.humi = humi;
            data.ok = 1;
            data.alarm = Alarm_Check(temp, humi);
            PrintfSafe("T:%.1f H:%.1f %s\r\n", temp, humi,
                       data.alarm ? "ALARM" : "NORMAL");

            /* 更新报警状态（状态定时器据此驱动LED/蜂鸣器）；
               温湿度恢复正常时自动解除本次消音 */
            g_alarm = data.alarm;
            if(!data.alarm) g_muted = 0;

            /* 写Flash（互斥量保护） */
            if(xSemaphoreTake(xFlashMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                W25Q64_WriteRecord(temp, humi);
                xSemaphoreGive(xFlashMutex);
            }

            /* 更新全局最新数据（互斥量保护） */
            if(xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_latest_data = data;
                xSemaphoreGive(xDataMutex);
            }

            /* 发队列通知DisplayTask */
            xQueueSend(xSensorQueue, &data, 0);
        } else {
            data.ok = 0;
            data.alarm = 0;
            PrintfSafe("DHT22 Error!\r\n");
            xQueueSend(xSensorQueue, &data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ==================== 显示任务（消费者） ==================== */
void DisplayTask(void *pvParameters)
{
    SensorData_t data = {0};
    char buf[32];
    Record_t last_record;
    uint8_t has_last_record = 1;  /* 默认无记录，不显示Last行 */

    /* 启动时读一次上次记录（互斥量保护） */
    if(xSemaphoreTake(xFlashMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        has_last_record = W25Q64_ReadLatestRecord(&last_record);
        xSemaphoreGive(xFlashMutex);
    }

    while(1)
    {
        /* 阻塞等队列数据，最多1秒 */
        if(xQueueReceive(xSensorQueue, &data, pdMS_TO_TICKS(1000)) == pdPASS) {
            if(xSemaphoreTake(xOledMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                if(data.ok) {
                    sprintf(buf, "T:%.1f C      ", data.temp);
                    OLED_ShowString(0, 0, buf);
                    sprintf(buf, "H:%.1f %%     ", data.humi);
                    OLED_ShowString(2, 0, buf);
                    /* 状态行（统一由DisplayTask操作OLED）：
                       撤防 > 报警(已消音?) > 正常 */
                    if(!g_armed)
                        OLED_ShowString(4, 0, "DISARMED      ");
                    else if(data.alarm)
                        OLED_ShowString(4, 0, g_muted ? "ALARM MUTE    " : "ALARM!        ");
                    else
                        OLED_ShowString(4, 0, "NORMAL       ");
                } else {
                    OLED_ShowString(0, 0, "DHT22 Error!  ");
                    OLED_ShowString(2, 0, "              ");
                    OLED_ShowString(4, 0, "              ");
                }
                xSemaphoreGive(xOledMutex);
            }
        }

        /* 刷新上次记录 */
        if(has_last_record == 0) {
            if(xSemaphoreTake(xOledMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                sprintf(buf, "Last:%.1f,%.1f  ", last_record.temp, last_record.humi);
                OLED_ShowString(6, 0, buf);
                xSemaphoreGive(xOledMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ==================== WiFi任务 ==================== */
void WifiTask(void *pvParameters)
{
    char buf[32];
    uint32_t last_upload = 0;
    SensorData_t latest = {0};

    ESP_RxITEnable();   /* 调度器已启动，安全开启ESP8266接收中断 */

    while(1)
    {
        ESP_ConnectStateMachine();

        /* 每10秒上传一次 */
        if((xTaskGetTickCount() - last_upload) >= pdMS_TO_TICKS(10000)) {
            last_upload = xTaskGetTickCount();

            /* 读全局最新数据（互斥量保护） */
            if(xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                latest = g_latest_data;
                xSemaphoreGive(xDataMutex);
            }

            if(latest.ok && ESP_IsOnline()) {
                sprintf(buf, "T:%.1f,H:%.1f", latest.temp, latest.humi);
                ESP_SendData(buf);
                PrintfSafe("WiFi uploaded\r\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ==================== 按键任务（中断+信号量） ==================== */
void KeyTask(void *pvParameters)
{
    while(1)
    {
        /* 等按键中断信号量，最多20ms超时（超时也扫一次，保证消抖完整） */
        xSemaphoreTake(xKeySemaphore, pdMS_TO_TICKS(20));
        Key_Scan();
    }
}

/* ==================== 串口 ==================== */
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

/* ==================== 钩子函数 ==================== */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    /* 钩子函数里直接用printf，不能再获取互斥量（可能死锁） */
    printf("Stack Overflow: %s\r\n", pcTaskName);
    while(1);
}

void vApplicationMallocFailedHook(void)
{
    printf("Malloc Failed!\r\n");
    while(1);
}
