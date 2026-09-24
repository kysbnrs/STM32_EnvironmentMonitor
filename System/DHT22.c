#include "DHT22.h"
#include "Delay.h"

// ========== 切换引脚方向 ==========
// 主机要"说话"时，把引脚设为推挽输出
static void DHT22_SetOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = DHT22_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT22_PORT, &GPIO_InitStructure);
}

// 主机"听话"时，把引脚设为上拉输入
static void DHT22_SetInput(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = DHT22_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(DHT22_PORT, &GPIO_InitStructure);
}

// 快捷读写
#define DHT22_OUT_LOW()   GPIO_ResetBits(DHT22_PORT, DHT22_PIN)
#define DHT22_OUT_HIGH()  GPIO_SetBits(DHT22_PORT, DHT22_PIN)
#define DHT22_IN()        GPIO_ReadInputDataBit(DHT22_PORT, DHT22_PIN)

// ========== 初始化 ==========
void DHT22_Init(void)
{
    RCC_APB2PeriphClockCmd(DHT22_CLK, ENABLE);
    DHT22_SetOutput();
    DHT22_OUT_HIGH();  // 空闲时保持高电平
}

// ========== 发"起床信号" ==========
static void DHT22_Start(void)
{
    DHT22_SetOutput();
    DHT22_OUT_LOW();          // 拉低总线
    Delay_ms(2);              // 保持低电平 2ms（>800μs 即可，给足余量）
    DHT22_OUT_HIGH();         // 拉高
    Delay_us(30);             // 保持高电平 30μs（20~40μs 范围内）
    DHT22_SetInput();         // 释放总线，准备听传感器回应
}

// ========== 等待传感器响应 ==========
// 返回 0 = 响应正常，1 = 没响应
static uint8_t DHT22_CheckResponse(void)
{
    uint32_t timeout = 0;

    // 等传感器拉低（响应信号的低电平 80μs）
    while(DHT22_IN() == Bit_SET)
    {
        timeout++;
        if(timeout > 10000) return 1;  // 超时，传感器没回应
    }

    timeout = 0;
    // 等传感器拉高（响应信号的高电平 80μs）
    while(DHT22_IN() == Bit_RESET)
    {
        timeout++;
        if(timeout > 10000) return 1;
    }

    timeout = 0;
    // 等传感器的高电平结束（80μs 高电平结束，即将开始发数据）
    while(DHT22_IN() == Bit_SET)
    {
        timeout++;
        if(timeout > 10000) return 1;
    }

    return 0;  // 响应正常
}

// ========== 读一个字节 ==========
static uint8_t DHT22_ReadByte(void)
{
    uint8_t byte = 0;
    uint32_t timeout;

    for(uint8_t i = 0; i < 8; i++)
    {
        byte <<= 1;  // 左移一位，给新数据腾位置

        timeout = 0;
        // 等 50μs 低电平结束
        while(DHT22_IN() == Bit_RESET)
        {
            timeout++;
            if(timeout > 10000) break;
        }

        // 低电平结束后，延时 40μs 再采样
        // 如果是 "0"：高电平只有 26~28μs，40μs 后已经变回低电平了
        // 如果是 "1"：高电平有 70μs，40μs 后还是高电平
        Delay_us(40);

        if(DHT22_IN() == Bit_SET)
        {
            byte |= 0x01;  // 这一位是 "1"

            timeout = 0;
            // 等这个 "1" 的高电平结束，才能读下一位
            while(DHT22_IN() == Bit_SET)
            {
                timeout++;
                if(timeout > 10000) break;
            }
        }
        // 如果是 "0"，高电平已经自己结束了，不用等
    }

    return byte;
}

// ========== 读取温湿度（对外接口） ==========
// 返回 0 = 成功，1 = 失败
uint8_t DHT22_ReadData(float *temperature, float *humidity)
{
    uint8_t data[5] = {0};
   

    DHT22_Start();                          // 发起床信号
	__disable_irq();
    if(DHT22_CheckResponse() != 0)         // 等传感器回应
    {
        __enable_irq();                     // 没回应，开中断，返回失败
        return 1;
    }

    // 读 5 个字节（40 位）
    for(uint8_t i = 0; i < 5; i++)
    {
        data[i] = DHT22_ReadByte();
    }

    __enable_irq();     // 数据读完了，恢复中断

    // 校验
    if(data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF))
    {
        return 1;  // 校验失败，数据无效
    }

    // 解析湿度
    uint16_t raw_humi = (data[0] << 8) | data[1];
    *humidity = raw_humi / 10.0f;

    // 解析温度
    int16_t raw_temp = (data[2] << 8) | data[3];
    if(raw_temp & 0x8000)  // 最高位为1 = 负温度
    {
        *temperature = -((raw_temp & 0x7FFF) / 10.0f);
    }
    else
    {
        *temperature = raw_temp / 10.0f;
    }

    return 0;  // 成功
}
