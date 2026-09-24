#include "I2C.h"

// 引脚：SCL=PB8，SDA=PB9
#define SCL_H()   GPIO_SetBits(GPIOB, GPIO_Pin_8)
#define SCL_L()   GPIO_ResetBits(GPIOB, GPIO_Pin_8)
#define SDA_H()   GPIO_SetBits(GPIOB, GPIO_Pin_9)
#define SDA_L()   GPIO_ResetBits(GPIOB, GPIO_Pin_9)
#define SDA_Read() GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_9)

static void delay_us(uint32_t us)
{
    uint32_t delay = (us * 72 / 4);
    while (delay--) { __NOP(); }
}

void I2C_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    SCL_H();
    SDA_H();
}

void I2C_Start(void)
{
    SDA_H();
    SCL_H();
    delay_us(5);
    SDA_L();
    delay_us(5);
    SCL_L();
}

void I2C_Stop(void)
{
    SDA_L();
    SCL_H();
    delay_us(5);
    SDA_H();
    delay_us(5);
}

void I2C_SendByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        SCL_L();
        if (byte & 0x80)
            SDA_H();
        else
            SDA_L();
        byte <<= 1;
        delay_us(2);
        SCL_H();
        delay_us(2);
    }
    SCL_L();
}

uint8_t I2C_WaitAck(void)
{
    uint8_t ack;
    SDA_H();              // 释放SDA，让从机拉低
    delay_us(2);
    SCL_H();              // 拉高SCL，读取SDA
    delay_us(2);
    ack = SDA_Read();     // 0=ACK，1=NACK
    SCL_L();
    delay_us(2);
    return ack;           // 返回0表示成功
}
