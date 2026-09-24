#include "Alarm.h"
#include "Buzzer.h"
#include "OLED.h"
#include "stm32f10x.h"
static uint8_t alarm_on = 0;

void Alarm_Check(float temp, float humi)
{
    if(temp > TEMP_MAX || humi > HUMI_MAX)
    {
        if(!alarm_on)
        {
            alarm_on = 1;
            Buzzer_On();
            OLED_ShowString(4, 0, "ALARM!        ");
        }
    }
    else
    {
        if(alarm_on)
        {
            alarm_on = 0;
            Buzzer_Off();
            OLED_ShowString(4, 0, "NORMAL       ");
        }
    }
}
