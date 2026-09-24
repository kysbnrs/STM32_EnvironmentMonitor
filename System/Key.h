#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"

void Key_Init(void);
void Key_Scan(void);  // 状态机扫描，主循环反复调用

#endif
