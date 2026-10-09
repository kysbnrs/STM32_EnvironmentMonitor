#ifndef __ALARM_H
#define __ALARM_H

#include "stm32f10x.h"

#define TEMP_MAX  30.0f
#define HUMI_MAX  80.0f

/* 返回1=报警，0=正常。只判断，不操作硬件 */
uint8_t Alarm_Check(float temp, float humi);

#endif
