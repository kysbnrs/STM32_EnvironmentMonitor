# 基于STM32的智能家居环境监测系统

一个基于STM32F103C8T6的裸机环境监测系统，实现温湿度采集、OLED本地显示、WiFi远程上传、Flash历史记录存储、按键控制和超限报警功能。

## 功能特性

- 🌡️ **温湿度采集**：DHT22单总线通信，2秒采样一次，支持负温度，校验和验证
- 📺 **本地显示**：0.96寸OLED（SSD1306，软件I2C驱动），实时显示温湿度和报警状态
- 📶 **远程上传**：ESP-01S通过AT指令连WiFi，TCP协议上传温湿度数据，断线自动重连
- 💾 **历史存储**：W25Q64 Flash（软件SPI驱动），环形存储500条记录，上电读出最近一条，断电不丢失
- 🔔 **超限报警**：温度>30℃或湿度>80%时蜂鸣器报警，OLED显示ALARM，恢复正常自动解除
- 🔘 **按键控制**：四态状态机消抖（20ms），单击翻转LED，长按1秒触发蜂鸣器
- 🖥️ **串口调试**：USART1 printf输出调试信息，波特率115200

## 硬件接线

| 模块 | STM32引脚 | 通信方式 |
|------|-----------|----------|
| DHT22温湿度 | PA1 | 单总线 |
| OLED显示屏 | PB8(SCL), PB9(SDA) | 软件I2C |
| W25Q64 Flash | PB12(CS), PB13(MISO), PB14(CLK), PB15(MOSI) | 软件SPI |
| ESP-01S WiFi | PB10(TX), PB11(RX) | USART3 (115200) |
| 按键 | PA0 | GPIO上拉输入（低电平有效） |
| LED | PA5 | GPIO推挽输出（高电平点亮） |
| 蜂鸣器 | PB0 | GPIO推挽输出（低电平触发） |
| 串口调试 | PA9(TX), PA10(RX) | USART1 (115200) |

## 技术栈

- **主控芯片**：STM32F103C8T6，Cortex-M3，72MHz
- **开发环境**：Keil MDK 5，标准外设库（StdPeriph_Lib）
- **系统架构**：裸机轮询 + 状态机（无RTOS）
- **时基**：SysTick 1ms中断，g_tick全局毫秒计数器
- **通信协议**：
  - 单总线（DHT22，关中断保证微秒级时序）
  - 软件I2C（OLED，开漏输出+上拉）
  - 软件SPI（W25Q64，全双工，模式0）
  - USART（ESP-01S AT指令 + printf调试）

## 关键设计

### 按键状态机
四态流转（IDLE→DEBOUNCE→PRESSED→RELEASE），非阻塞设计，每次调用只做一次状态判断，消抖20ms，长按检测1秒。

### ESP连接状态机
五态管理（IDLE→CWMODE→CWJAP→CIPSTART→ONLINE），每步AT指令带超时，失败自动回退重试，ONLINE状态每5秒检测连接状态，断线自动重连。

### Flash存储设计
- 记录结构体8字节（温度float + 湿度float）
- 环形存储500条，写指针存在Flash固定地址
- 读-改-写方式：读扇区到RAM→修改→擦除扇区→写回
- 上电自动读出最近一条记录显示在OLED

### DHT22时序
- 起始信号：拉低2ms + 拉高30μs
- 响应检测：80μs低 + 80μs高
- 数据位：50μs低起始，高电平26-28μs为0，70μs为1
- 采样点：低电平结束后延时40μs采样
- 读取期间关中断，防止时序被打断

## 项目结构

```
├── User/               # 主函数与中断服务
│   ├── main.c          # 主循环，业务逻辑调度
│   ├── main.h
│   ├── stm32f10x_it.c  # 中断服务函数（SysTick）
│   └── stm32f10x_conf.h
├── System/             # 外设驱动层
│   ├── DHT22.c/h       # 单总线温湿度驱动
│   ├── OLED.c/h        # SSD1306 OLED驱动
│   ├── I2C.c/h         # 软件I2C时序驱动
│   ├── W25Q64.c/h      # SPI Flash驱动
│   ├── ESP.c/h         # ESP8266 AT指令驱动
│   ├── Key.c/h         # 按键状态机
│   ├── LED.c/h         # LED驱动
│   ├── Buzzer.c/h      # 蜂鸣器驱动
│   ├── Alarm.c/h       # 超限报警逻辑
│   └── Delay.c/h       # SysTick延时 + GetTick
├── Library/            # STM32标准外设库
├── Start/              # 启动文件 + 系统初始化
└── project.uvprojx     # Keil工程文件
```

## 使用说明

### 编译环境
- Keil MDK 5.x
- STM32F10x标准外设库

### WiFi配置
修改 `System/ESP.c` 中的配置：
```c
#define WIFI_SSID     "你的WiFi名称"
#define WIFI_PASS     "你的WiFi密码"
#define TCP_SERVER    "服务器IP地址"
#define TCP_PORT      "服务器端口"
```

### 报警阈值
修改 `System/Alarm.h`：
```c
#define TEMP_MAX  30.0f   // 温度报警阈值（℃）
#define HUMI_MAX  80.0f   // 湿度报警阈值（%）
```

### 烧录方式
使用ST-Link / J-Link下载器，烧录生成的hex文件或axf文件。

## 演示视频

[【STM32项目】基于STM32的环境监测系统演示视频 - B站](https://www.bilibili.com/video/BV1Xtaw6yEkv?vd_source=0b2f1bd0fad3d2b0ecf5ffb96cf0e8f7)

## 作者

周宏鹏 - 嵌入式开发爱好者
