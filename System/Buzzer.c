#include "Buzzer.h"
#include "stm32f10x.h"
#define BUZ_PORT    GPIOB
#define BUZ_PIN     GPIO_Pin_0
#define BUZ_CLK     RCC_APB2Periph_GPIOB

void Buzzer_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(BUZ_CLK, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = BUZ_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BUZ_PORT, &GPIO_InitStructure);
    
    GPIO_SetBits(BUZ_PORT, BUZ_PIN);  // 初始关闭
}

void Buzzer_Off(void)  { GPIO_SetBits(BUZ_PORT, BUZ_PIN); }
void Buzzer_On(void) { GPIO_ResetBits(BUZ_PORT, BUZ_PIN); }
