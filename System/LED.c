#include "LED.h"
#include "stm32f10x.h"
#define LED_PORT    GPIOA
#define LED_PIN     GPIO_Pin_5
#define LED_CLK     RCC_APB2Periph_GPIOA

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(LED_CLK, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);
    
    GPIO_ResetBits(LED_PORT, LED_PIN);
}

void LED_On(void)   { GPIO_SetBits(LED_PORT, LED_PIN); }
void LED_Off(void)  { GPIO_ResetBits(LED_PORT, LED_PIN); }

void LED_Toggle(void)
{
    LED_PORT->ODR ^= LED_PIN;
}
