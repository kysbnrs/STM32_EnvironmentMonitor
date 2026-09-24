#ifndef __I2C_H
#define __I2C_H

#include "stm32f10x.h"

void I2C_GPIO_Init(void);
void I2C_Start(void);
void I2C_Stop(void);
void I2C_SendByte(uint8_t byte);
uint8_t I2C_WaitAck(void);

#endif
