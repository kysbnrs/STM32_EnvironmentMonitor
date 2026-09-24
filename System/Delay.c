#include "stm32f10x.h"
#include "Delay.h"

volatile uint32_t g_tick = 0;  // 全局毫秒计数器

// SysTick中断服务函数，每1ms触发一次
void SysTick_Handler(void)
{
    g_tick++;
}

// 获取当前毫秒数
uint32_t GetTick_ms(void)
{
    return g_tick;
}

// 微秒延时（保留，DHT22/I2C要用）
void Delay_us(uint32_t xus)
{
    uint32_t saved_load = SysTick->LOAD;
    uint32_t saved_ctrl = SysTick->CTRL;
    
    // 用SysTick查询模式做精确微秒延时
    SysTick->LOAD = 72 * xus;
    SysTick->VAL = 0x00;
    SysTick->CTRL = 0x00000005;   // 72MHz时钟源，启动，不开中断
    while(!(SysTick->CTRL & 0x00010000));
    SysTick->CTRL = 0x00000004;   // 关闭
    
    // 恢复1ms中断配置
    SysTick->LOAD = saved_load;
    SysTick->VAL = 0x00;
    SysTick->CTRL = saved_ctrl;
}

// 毫秒延时（改成基于g_tick，不阻塞中断）
void Delay_ms(uint32_t xms)
{
    uint32_t start = g_tick;
    while(g_tick - start < xms);
}

void Delay_s(uint32_t xs)
{
    while(xs--)
    {
        Delay_ms(1000);
    }
}
