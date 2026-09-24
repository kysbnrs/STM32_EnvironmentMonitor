#include "Key.h"
#include "Delay.h"
#include "LED.h"
#include "Buzzer.h"
#include "stm32f10x.h"
#define KEY_PORT    GPIOA
#define KEY_PIN     GPIO_Pin_0
#define KEY_CLK     RCC_APB2Periph_GPIOA
#define KEY_READ()  GPIO_ReadInputDataBit(KEY_PORT, KEY_PIN)

#define DEBOUNCE_MS    20     // 消抖20ms
#define LONG_PRESS_MS  1000   // 长按1秒

typedef enum {
    KEY_IDLE = 0,
    KEY_DEBOUNCE,
    KEY_PRESSED,
    KEY_RELEASE
} KeyState;

void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(KEY_CLK, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = KEY_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // 上拉输入
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);
}

void Key_Scan(void)
{
    static KeyState state = KEY_IDLE;
    static uint32_t press_time = 0;
    static uint8_t long_triggered = 0;
    
    switch(state)
    {
        case KEY_IDLE:
            if(KEY_READ() == 0)  // 检测到按下
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
                    state = KEY_PRESSED;  // 真按下
                else
                    state = KEY_IDLE;     // 抖动
            }
            break;
            
        case KEY_PRESSED:
            // 长按检测
            if(!long_triggered && (GetTick_ms() - press_time >= LONG_PRESS_MS))
            {
                long_triggered = 1;
                Buzzer_On();  // 长按→蜂鸣器响
            }
            // 松手检测
            if(KEY_READ() == 1)
            {
                state = KEY_RELEASE;
                press_time = GetTick_ms();
            }
            break;
            
        case KEY_RELEASE:
            if(GetTick_ms() - press_time >= DEBOUNCE_MS)
            {
                if(KEY_READ() == 1)  // 真松开
                {
                    Buzzer_Off();
                    if(!long_triggered)
                        LED_Toggle();  // 单击→翻转LED
                    state = KEY_IDLE;
                }
                else
                    state = KEY_PRESSED;  // 抖动，回去
            }
            break;
    }
}
