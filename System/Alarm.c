#include "Alarm.h"

/* 只判断温湿度是否超限，不操作任何硬件。
   蜂鸣器由SensorTask通过互斥量控制，OLED由DisplayTask统一刷新。 */
uint8_t Alarm_Check(float temp, float humi)
{
    if(temp > TEMP_MAX || humi > HUMI_MAX)
        return 1;
    return 0;
}
