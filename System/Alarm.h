#ifndef __ALARM_H
#define __ALARM_H

#include "stm32f10x.h"

#define TEMP_MAX  30.0f
#define HUMI_MAX  80.0f

void Alarm_Check(float temp, float humi);

#endif
