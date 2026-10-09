#include "ESP.h"
#include "Delay.h"
#include "string.h"
#include "stm32f10x_usart.h"
#include "stdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* main.c中定义的线程安全打印函数 */
extern void PrintfSafe(const char *fmt, ...);

/* ============================================================
 * 接收环形缓冲区（Ring Buffer）
 * 单生产者（USART3中断写）单消费者（Wifi任务读）模型
 * 大小必须是2的幂，用 &(SIZE-1) 代替取模运算
 * ============================================================ */
#define UART3_BUF_SIZE  512

static volatile uint8_t  rx_buf[UART3_BUF_SIZE];
static volatile uint16_t rx_head = 0;   /* 中断写入位置（生产者） */
static volatile uint16_t rx_tail = 0;   /* 任务读取位置（消费者） */

static SemaphoreHandle_t xUart3RxSem = NULL;  /* 接收二值信号量 */

/* 中断里调用：往缓冲区存一个字节，满了就丢弃 */
static void RingBuf_PutFromISR(uint8_t ch)
{
    uint16_t next = (uint16_t)((rx_head + 1) & (UART3_BUF_SIZE - 1));
    if(next != (rx_tail & (UART3_BUF_SIZE - 1)))  /* 没满 */
    {
        rx_buf[rx_head & (UART3_BUF_SIZE - 1)] = ch;
        rx_head = next;
    }
    /* 满了直接丢弃，保证中断快速退出 */
}

/* 任务里调用：从缓冲区取一个字节，返回-1表示空 */
static int16_t RingBuf_Get(void)
{
    if(rx_head == rx_tail)
        return -1;
    uint8_t ch = rx_buf[rx_tail & (UART3_BUF_SIZE - 1)];
    rx_tail = (uint16_t)((rx_tail + 1) & (UART3_BUF_SIZE - 1));
    return (int16_t)ch;
}

/* 任务里调用：清空缓冲区（丢弃历史数据） */
static void RingBuf_Flush(void)
{
    /* head被中断改写，清空操作需要进临界区，防止读到中间值 */
    taskENTER_CRITICAL();
    rx_tail = rx_head;
    taskEXIT_CRITICAL();
}

// ========== USART3初始化（PB10=TX, PB11=RX），开启接收中断 ==========
void USART3_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* USART3在APB1！不是APB2 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* PB10 = USART3_TX，复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* PB11 = USART3_RX，上拉输入（空闲保持高电平，
       防止模块未接/接触不良时悬空噪声持续触发中断形成风暴） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &USART_InitStructure);

    /* 创建接收信号量（调度器启动前也可以创建，堆是静态分配的） */
    if(xUart3RxSem == NULL)
        xUart3RxSem = xSemaphoreCreateBinary();

    /* 配置NVIC：优先级6（数值>=5，受FreeRTOS管理，可在ISR中用FromISR API） */
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* NVIC配置好，但暂不开启RXNE中断（等调度器启动、Wifi任务运行后再开），
       避免调度器启动前ESP8266上电数据触发FromISR API */

    USART_Cmd(USART3, ENABLE);
}

/* Wifi任务启动后调用：正式打开接收中断 */
void ESP_RxITEnable(void)
{
    RingBuf_Flush();
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
}

/* ========== USART3接收中断服务函数 ========== */
void USART3_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint16_t sr = USART3->SR;   /* SR只读一次，后面配合读DR完成标志清除 */

    /* 1. 错误标志（ORE溢出/FE帧错误/NE噪声/PE校验错）：
          读SR再读DR清除，错误字节直接丢弃不入缓冲区。
          这些标志不清除会导致中断退出后立即重入，形成中断风暴。 */
    if(sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE))
    {
        (void)USART3->DR;
    }
    /* 2. 正常收到一个字节：存入环形缓冲区，通知Wifi任务 */
    else if(sr & USART_SR_RXNE)
    {
        uint8_t ch = (uint8_t)USART3->DR;
        RingBuf_PutFromISR(ch);

        if(xUart3RxSem != NULL)
            xSemaphoreGiveFromISR(xUart3RxSem, &xHigherPriorityTaskWoken);
    }

    /* 如果唤醒了更高优先级任务，退出中断后立即切换过去 */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ========== 发字符串（发送，阻塞查询TXE，数据量小不影响） ==========
void UART3_SendString(char *str)
{
    while(*str)
    {
        while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
        USART_SendData(USART3, *str++);
    }
}

// ========== 发AT命令 + 等应答 + 超时（事件驱动，不再忙等） ==========
// 返回0=成功（收到expect），1=超时失败
uint8_t ESP_SendCmd(char *cmd, char *expect, uint32_t timeout_ms)
{
    char buf[256] = {0};
    uint16_t idx = 0;
    uint32_t start = GetTick_ms();

    /* 发命令前清空接收缓冲区，防止历史应答干扰本次判断 */
    RingBuf_Flush();

    /* 只有cmd非空时才发命令 */
    if(strlen(cmd) > 0)
    {
        UART3_SendString(cmd);
        UART3_SendString("\r\n");
    }

    while(1)
    {
        /* 1. 把缓冲区里已有的数据全部取出来匹配 */
        int16_t ch;
        while((ch = RingBuf_Get()) != -1)
        {
            if(idx < sizeof(buf) - 1)
                buf[idx++] = (char)ch;
            buf[idx] = '\0';

            if(strstr(buf, expect) != NULL)
                return 0;  /* 收到期望应答 */
        }

        /* 2. 检查超时 */
        uint32_t elapsed = GetTick_ms() - start;
        if(elapsed >= timeout_ms)
            return 1;  /* 超时 */

        /* 3. 缓冲区空了，阻塞等信号量（最多等剩余超时时间），
              期间任务休眠让出CPU，中断收到数据会唤醒本任务 */
        xSemaphoreTake(xUart3RxSem,
                       pdMS_TO_TICKS(timeout_ms - elapsed));
    }
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
#define TCP_SERVER    "10.68.133.232"
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
                PrintfSafe("[ESP] AT OK\r\n");
                esp_state = ESP_CWMODE;
            }
            else
            {
                PrintfSafe("[ESP] AT FAIL\r\n");
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;

        case ESP_CWMODE:
            if(ESP_SendCmd("AT+CWMODE=1", "OK", 3000) == 0)
            {
                PrintfSafe("[ESP] CWMODE OK\r\n");
                esp_state = ESP_CWJAP;
            }
            else
            {
                PrintfSafe("[ESP] CWMODE FAIL\r\n");
                esp_state = ESP_IDLE;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;

        case ESP_CWJAP:
            snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
            if(ESP_SendCmd(cmd, "WIFI GOT IP", 10000) == 0)
            {
                PrintfSafe("[ESP] WiFi GOT IP\r\n");
                ESP_SendCmd("AT+CIFSR", "OK", 2000);  // 查询ESP的IP
                esp_state = ESP_CIPSTART;
            }
            else
            {
                PrintfSafe("[ESP] WiFi FAIL\r\n");
                esp_state = ESP_IDLE;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;

        case ESP_CIPSTART:
            snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%s", TCP_SERVER, TCP_PORT);
            if(ESP_SendCmd(cmd, "CONNECT", 5000) == 0)
            {
                PrintfSafe("[ESP] TCP Connected\r\n");
                check_time = GetTick_ms();
                esp_state = ESP_ONLINE;
            }
            else
            {
                PrintfSafe("[ESP] TCP FAIL\r\n");
                esp_state = ESP_IDLE;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;

        case ESP_ONLINE:
            if(GetTick_ms() - check_time >= 5000)
            {
                check_time = GetTick_ms();
                if(ESP_SendCmd("AT+CIPSTATUS", "STATUS:3", 2000) != 0)
                {
                    PrintfSafe("[ESP] Reconnecting...\r\n");
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
