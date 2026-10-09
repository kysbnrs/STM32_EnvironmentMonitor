#include "Key.h"
#include "Delay.h"
#include "stm32f10x.h"
#include "stm32f10x_exti.h"
#include "misc.h"
#include "FreeRTOS.h"
#include "semphr.h"

#define KEY_PORT    GPIOA
#define KEY_PIN     GPIO_Pin_0
#define KEY_CLK     RCC_APB2Periph_GPIOA
#define KEY_READ()  GPIO_ReadInputDataBit(KEY_PORT, KEY_PIN)

#define DEBOUNCE_MS    20     /* 消抖20ms */
#define LONG_PRESS_MS  1000   /* 长按1秒 */

/* 二值信号量句柄，在main.c中创建 */
extern SemaphoreHandle_t xKeySemaphore;

/* 系统状态标志，在main.c中定义（单字节+volatile，原子访问，无需互斥量） */
extern volatile uint8_t g_armed;  /* 布防/撤防 */
extern volatile uint8_t g_muted;  /* 消音 */
extern volatile uint8_t g_alarm;  /* 当前是否报警 */

typedef enum {
    KEY_IDLE = 0,
    KEY_DEBOUNCE,
    KEY_PRESSED,
    KEY_RELEASE
} KeyState;

void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(KEY_CLK | RCC_APB2Periph_AFIO, ENABLE);

    /* GPIO配置：上拉输入 */
    GPIO_InitStructure.GPIO_Pin = KEY_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);

    /* 外部中断线配置：PA0 -> EXTI0，下降沿触发 */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    EXTI_InitStructure.EXTI_Line = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    /* NVIC配置：优先级6（数值>=5，受FreeRTOS管理，可在ISR中调用FromISR API） */
    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/* 外部中断0服务函数：按键按下时触发，释放二值信号量唤醒KeyTask */
void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xKeySemaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void Key_Scan(void)
{
    static KeyState state = KEY_IDLE;
    static uint32_t press_time = 0;
    static uint8_t long_triggered = 0;

    switch(state)
    {
        case KEY_IDLE:
            if(KEY_READ() == 0)  /* 检测到按下 */
            {
                state = KEY_DEBOUNCE;
                press_time = GetTick_ms();
                long_triggered = 0;
            }
            break;

        case KEY_DEBOUNCE:
            if(GetTick_ms() - press_time >= DEBOUNCE_MS)
            {
                if(KEY_READ() == 0)
                    state = KEY_PRESSED;  /* 真按下 */
                else
                    state = KEY_IDLE;     /* 抖动 */
            }
            break;

        case KEY_PRESSED:
            /* 长按1秒：布防/撤防切换（只改标志，LED/蜂鸣器由状态定时器渲染） */
            if(!long_triggered && (GetTick_ms() - press_time >= LONG_PRESS_MS))
            {
                long_triggered = 1;
                g_armed = !g_armed;
                if(g_armed) g_muted = 0;  /* 重新布防时取消消音 */
            }
            /* 松手检测 */
            if(KEY_READ() == 1)
            {
                state = KEY_RELEASE;
                press_time = GetTick_ms();
            }
            break;

        case KEY_RELEASE:
            if(GetTick_ms() - press_time >= DEBOUNCE_MS)
            {
                if(KEY_READ() == 1)  /* 真松开 */
                {
                    /* 短按松手且当前正在报警：报警消音；正常时短按无效 */
                    if(!long_triggered && g_alarm)
                        g_muted = 1;
                    state = KEY_IDLE;
                }
                else
                    state = KEY_PRESSED;  /* 抖动，回去 */
            }
            break;
    }
}
