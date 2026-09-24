#ifndef __ESP_H
#define __ESP_H

#include "stm32f10x.h"

void USART3_Init(void);
void UART3_SendString(char *str);
uint8_t ESP_SendCmd(char *cmd, char *expect, uint32_t timeout_ms);
void ESP_ConnectStateMachine(void);
uint8_t ESP_SendData(char *data);
uint8_t ESP_IsOnline(void);

#endif
