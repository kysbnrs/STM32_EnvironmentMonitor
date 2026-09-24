#ifndef __W25Q64_H
#define __W25Q64_H

#include "stm32f10x.h"
// ========== 温湿度历史记录存储 ==========
typedef struct {
    float temp;     // 温度，4字节
    float humi;     // 湿度，4字节
} Record_t;         // 每条记录共8字节

#define RECORD_SECTOR_ADDR   0x000000   // 存在第0个扇区
#define RECORD_START_ADDR    0x000010   // 记录从这个地址开始存（开头留16字节备用）
#define RECORD_MAX_COUNT     500         // 最多存500条（500*8=4000字节，一个扇区4096字节够）
void W25Q64_Init(void);
void W25Q64_ReadID(uint8_t *id);
void W25Q64_WriteEnable(void);
void W25Q64_WaitBusy(void);
void W25Q64_SectorErase(uint32_t addr);
void W25Q64_PageProgram(uint32_t addr, uint8_t *data, uint16_t len);
void W25Q64_ReadData(uint32_t addr, uint8_t *data, uint16_t len);

// 温湿度记录相关
uint8_t W25Q64_WriteRecord(float temp, float humi);  // 写一条新记录，返回0=成功
uint8_t W25Q64_ReadLatestRecord(Record_t *record);    // 读最新一条记录，返回0=成功，1=没有记录
#endif

