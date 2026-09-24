#include "W25Q64.h"
#include "Delay.h"

// 引脚定义（按你的接线）
#define CS_PORT    GPIOB
#define CS_PIN     GPIO_Pin_12
#define CLK_PORT   GPIOB
#define CLK_PIN    GPIO_Pin_14
#define MOSI_PORT  GPIOB
#define MOSI_PIN   GPIO_Pin_15   // DI
#define MISO_PORT  GPIOB
#define MISO_PIN   GPIO_Pin_13   // DO

// 快捷操作
#define CS_LOW()   GPIO_ResetBits(CS_PORT, CS_PIN)
#define CS_HIGH()  GPIO_SetBits(CS_PORT, CS_PIN)
#define CLK_LOW()  GPIO_ResetBits(CLK_PORT, CLK_PIN)
#define CLK_HIGH() GPIO_SetBits(CLK_PORT, CLK_PIN)
#define MOSI_HIGH() GPIO_SetBits(MOSI_PORT, MOSI_PIN)
#define MOSI_LOW()  GPIO_ResetBits(MOSI_PORT, MOSI_PIN)
#define MISO_READ() GPIO_ReadInputDataBit(MISO_PORT, MISO_PIN)

// ========== 初始化 ==========
void W25Q64_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    // CS、CLK、MOSI 推挽输出
    GPIO_InitStructure.GPIO_Pin = CS_PIN | CLK_PIN | MOSI_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    // MISO 上拉输入
    GPIO_InitStructure.GPIO_Pin = MISO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    CS_HIGH();   // CS默认高（未选中）
    CLK_LOW();   // CLK默认低
}

// ========== SPI收发一个字节（核心函数）==========
// 发一个字节的同时，收一个字节（SPI全双工）
uint8_t SPI_ReadWrite(uint8_t data)
{
    uint8_t i, recv = 0;
    for(i = 0; i < 8; i++)
    {
        // 1. 先把数据位放到MOSI上（高位在前）
        if(data & 0x80)
            MOSI_HIGH();
        else
            MOSI_LOW();
        data <<= 1;
        
        // 2. 时钟上升沿，从机采样MOSI，主机也可以读MISO
        CLK_HIGH();
        
        // 3. 读MISO（从机发的数据）
        recv <<= 1;
        if(MISO_READ())
            recv |= 0x01;
        
        // 4. 时钟下降沿，准备下一位
        CLK_LOW();
    }
    return recv;
}

// ========== 读芯片ID ==========
void W25Q64_ReadID(uint8_t *id)
{
    CS_LOW();                  // 拉低CS，选中芯片
    SPI_ReadWrite(0x9F);       // 发读ID命令
    id[0] = SPI_ReadWrite(0xFF); // 读厂商ID（发0xFF只是为了产生时钟）
    id[1] = SPI_ReadWrite(0xFF); // 读内存类型
    id[2] = SPI_ReadWrite(0xFF); // 读容量
    CS_HIGH();                 // 拉高CS，结束
}

// ========== 写使能 ==========
// 写/擦除前必须先发这个命令，否则不生效
void W25Q64_WriteEnable(void)
{
    CS_LOW();
    SPI_ReadWrite(0x06);  // 写使能命令
    CS_HIGH();
}

// ========== 等待芯片不忙 ==========
// 擦除和写入需要时间，要等状态寄存器的忙位清零
void W25Q64_WaitBusy(void)
{
    CS_LOW();
    SPI_ReadWrite(0x05);  // 读状态寄存器命令
    while(SPI_ReadWrite(0xFF) & 0x01);  // 最低位=1表示忙，等它变0
    CS_HIGH();
}

// ========== 扇区擦除 ==========
// addr是扇区地址，比如0x000000就是第一个扇区
void W25Q64_SectorErase(uint32_t addr)
{
    W25Q64_WriteEnable();   // 先写使能
    CS_LOW();
    SPI_ReadWrite(0x20);    // 扇区擦除命令
    SPI_ReadWrite((addr >> 16) & 0xFF);  // 地址高8位
    SPI_ReadWrite((addr >> 8) & 0xFF);   // 地址中8位
    SPI_ReadWrite(addr & 0xFF);          // 地址低8位
    CS_HIGH();
    W25Q64_WaitBusy();    // 等擦除完成
}

// ========== 页编程（写数据，最多256字节）==========
void W25Q64_PageProgram(uint32_t addr, uint8_t *data, uint16_t len)
{
    // 1. 长度检查：一页最多256字节
    if(len > 256)
        len = 256;
    
    // 2. 跨页检查：计算当前页还剩多少字节
    //    页大小256字节，页内偏移 = addr & 0xFF（地址的低8位）
    //    当前页剩余空间 = 256 - 页内偏移
    uint16_t page_remain = 256 - (addr & 0xFF);
    if(len > page_remain)
        len = page_remain;  // 只写到页尾，剩下的下次再写
		
    W25Q64_WriteEnable();   // 先写使能
    CS_LOW();
    SPI_ReadWrite(0x02);    // 页编程命令
    SPI_ReadWrite((addr >> 16) & 0xFF);
    SPI_ReadWrite((addr >> 8) & 0xFF);
    SPI_ReadWrite(addr & 0xFF);
    for(uint16_t i = 0; i < len; i++)
        SPI_ReadWrite(data[i]);  // 发数据
    CS_HIGH();
    W25Q64_WaitBusy();    // 等写入完成
}

// ========== 温湿度历史记录存储 ==========

// 写一条新记录（追加到末尾，写满则擦除循环）
uint8_t W25Q64_WriteRecord(float temp, float humi)
{
    Record_t record;
    record.temp = temp;
    record.humi = humi;
    
    // 1. 从前往后找第一个空记录（全0xFF的位置）
    uint32_t addr = RECORD_START_ADDR;
    uint8_t buf[8];
    uint16_t i;
    uint8_t is_empty;
    
    for(i = 0; i < RECORD_MAX_COUNT; i++)
    {
        W25Q64_ReadData(addr, buf, 8);
        // 判断是不是空记录（全0xFF）
        is_empty = 1;
        for(uint8_t j = 0; j < 8; j++)
        {
            if(buf[j] != 0xFF)
            {
                is_empty = 0;
                break;
            }
        }
        if(is_empty)
            break;  // 找到空位置了
        addr += sizeof(Record_t);
    }
    
    // 2. 如果全部写满了，擦除整个扇区，从头开始
    if(i >= RECORD_MAX_COUNT)
    {
        W25Q64_SectorErase(RECORD_SECTOR_ADDR);
        addr = RECORD_START_ADDR;
    }
    
    // 3. 写入新记录
    W25Q64_PageProgram(addr, (uint8_t*)&record, sizeof(Record_t));
    
    return 0;
}

// 读最新一条记录（从后往前找最后一个非空记录）
uint8_t W25Q64_ReadLatestRecord(Record_t *record)
{
    uint32_t addr = RECORD_START_ADDR + (RECORD_MAX_COUNT - 1) * sizeof(Record_t);
    uint8_t buf[8];
    uint16_t i;
    uint8_t is_empty;
    
    for(i = 0; i < RECORD_MAX_COUNT; i++)
    {
        W25Q64_ReadData(addr, buf, 8);
        // 判断是不是有效记录（不是全0xFF）
        is_empty = 1;
        for(uint8_t j = 0; j < 8; j++)
        {
            if(buf[j] != 0xFF)
            {
                is_empty = 0;
                break;
            }
        }
        if(!is_empty)
        {
            // 找到有效记录了，拷贝出来
            record->temp = ((Record_t*)buf)->temp;
            record->humi = ((Record_t*)buf)->humi;
            return 0;  // 成功
        }
        addr -= sizeof(Record_t);
    }
    
    return 1;  // 没有任何记录
}

// ========== 读数据 ==========
void W25Q64_ReadData(uint32_t addr, uint8_t *data, uint16_t len)
{
    CS_LOW();
    SPI_ReadWrite(0x03);    // 读数据命令
    SPI_ReadWrite((addr >> 16) & 0xFF);
    SPI_ReadWrite((addr >> 8) & 0xFF);
    SPI_ReadWrite(addr & 0xFF);
    for(uint16_t i = 0; i < len; i++)
        data[i] = SPI_ReadWrite(0xFF);  // 发0xFF产生时钟，收数据
    CS_HIGH();
}

