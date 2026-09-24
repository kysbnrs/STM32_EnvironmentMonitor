#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t page, uint8_t col, char c);
void OLED_ShowString(uint8_t page, uint8_t col, const char *str);
void OLED_DisplayOn(void);
void OLED_DisplayOff(void);

#endif
