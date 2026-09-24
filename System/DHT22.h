#ifndef __DHT22_H
#define __DHT22_H

#include "stm32f10x.h"

// 引脚定义（换引脚只改这里）
#define DHT22_PORT    GPIOA
#define DHT22_PIN     GPIO_Pin_1
#define DHT22_CLK     RCC_APB2Periph_GPIOA

// 函数声明
void    DHT22_Init(void);
uint8_t DHT22_ReadData(float *temperature, float *humidity);

#endif
