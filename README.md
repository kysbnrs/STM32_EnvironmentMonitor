# 基于 STM32 + FreeRTOS 的智能家居环境监测系统

一个以 STM32F103C8T6 为主控、运行 FreeRTOS 实时操作系统的多任务环境监测终端。系统周期性采集温湿度，在 OLED 上本地显示，通过 ESP8266 经 WiFi/TCP 远程上传，并把历史数据环形存储到外部 Flash；同时支持按键布防/撤防、阈值越限声光报警与掉线状态指示。

本项目由裸机轮询版本重构为 FreeRTOS 多任务架构，重点体现任务划分、队列、信号量、互斥量、软件定时器与中断管理等 RTOS 工程实践。

## 功能特性

- 🌡️ **温湿度采集**：DHT22 单总线通信，每 2 秒采样一次，支持负温度，带校验和验证，读取失败有错误处理
- 📺 **本地显示**：0.96 寸 OLED（SSD1306，软件 I2C 驱动），实时显示温湿度、布防/报警状态及最近一条历史记录
- 📶 **远程上传**：ESP-01S 通过 AT 指令连接 WiFi，TCP 协议每 10 秒上传一次温湿度，连接状态机 + 断线自动重连
- 💾 **历史存储**：W25Q64 外部 Flash（软件 SPI 驱动），环形存储 500 条记录，上电读出最近一条，断电不丢失
- 🔔 **越限报警**：温度 > 30℃ 或湿度 > 80% 时触发，OLED 显示 ALARM，蜂鸣器鸣响，恢复正常自动解除
- 🔘 **按键交互**：PA0 外部中断 + 二值信号量唤醒，四态状态机消抖；**长按 1 秒切换布防/撤防，报警时短按消音**
- 💡 **状态指示灯**：LED 由 100ms 软件定时器统一渲染——撤防常灭、报警快闪、在线常亮、离线慢闪
- 🖥️ **线程安全串口**：USART1 经互斥量保护的 `PrintfSafe` 输出调试信息，波特率 115200

## 硬件接线

| 模块 | STM32 引脚 | 通信方式 |
|------|-----------|----------|
| DHT22 温湿度 | PA1 | 单总线（One-Wire） |
| OLED 显示屏 | PB8(SCL)、PB9(SDA) | 软件 I2C |
| W25Q64 Flash | PB12(CS)、PB13(MISO)、PB14(CLK)、PB15(MOSI) | 软件 SPI（模式 0） |
| ESP-01S WiFi | PB10(USART3 发送 TX)、PB11(USART3 接收 RX) | UART3，115200 |
| 按键 | PA0 | GPIO 上拉输入，EXTI0 下降沿中断 |
| LED 指示灯 | PA5 | GPIO 推挽输出，高电平点亮 |
| 有源蜂鸣器 | PB0 | GPIO 推挽输出，低电平鸣响 |
| 调试串口 | PA9(USART1 发送 TX)、PA10(USART1 接收 RX) | UART1，115200，8N1 |

## 系统架构（FreeRTOS）

- **内核配置**：抢占式调度（`configUSE_PREEMPTION=1`）+ 同优先级时间片轮转（`configUSE_TIME_SLICING=1`）；系统节拍 1000Hz（1 tick = 1ms）；堆内存管理 `heap_4`，总堆大小 17KB；最大优先级数 5；开启栈溢出检测（等级 2）与互斥量优先级继承。
- **启动流程**：`main` 中先设置 NVIC 全抢占分组（Group 4，FreeRTOS 强制要求）→ 初始化全部硬件 → 创建互斥量/信号量/队列/软件定时器 → 创建 4 个任务 → `vTaskStartScheduler()` 启动调度器，此后由内核接管。

### 任务划分

| 任务 | 优先级 | 栈大小 | 调度/阻塞方式 | 主要职责 |
|------|:---:|:---:|------|------|
| SensorTask（采集） | 3 | 256 字 / 1KB | 周期 2s，`vTaskDelay` | 读 DHT22、阈值判断、写 Flash、更新全局数据、向队列发送数据 |
| DisplayTask（显示） | 2 | 256 字 / 1KB | 队列阻塞接收（超时 1s）+ 500ms 周期 | 接收数据刷新 OLED，显示状态行与最近历史记录 |
| WifiTask（联网） | 2 | 512 字 / 2KB | 10ms 轮询，每 10s 上传 | 运行 ESP 连接状态机、TCP 上传、在线检测 |
| KeyTask（按键） | 1 | 128 字 / 512B | 阻塞等待按键信号量（超时 20ms） | 唤醒后执行按键状态机扫描消抖 |
| 定时器服务任务 | 2 | 256 字 | 内核创建 | 执行软件定时器回调 |
| Idle（空闲任务） | 0 | 128 字 | 内核创建 | 系统空闲时运行 |

> 栈深度参数单位为字（word），Cortex-M3 上 1 字 = 4 字节。采集任务优先级最高，保证采样时序；联网任务栈最大，因 AT 指令拼接使用了较多局部数组。

### 任务间通信与同步

- **消息队列 `xSensorQueue`**：深度 5、元素为 `SensorData_t`（温度、湿度、采集成功标志、报警标志），采用**生产者—消费者**模型解耦采集与显示。采集任务 `xQueueSend(..., 0)` 队列满时不阻塞以保证采样节拍；显示任务 `xQueueReceive(..., 1000)` 空队列时阻塞让出 CPU，数据按值拷贝，天然线程安全。
- **互斥量（Mutex）4 个**，分别保护被多任务访问的共享资源，`xSemaphoreTake` 均带超时（50/100ms），避免任务永久阻塞形成死锁：
  - `xOledMutex`：OLED 显示（仅 DisplayTask 使用，集中管理显示设备）
  - `xPrintfMutex`：串口打印，封装为线程安全的 `PrintfSafe()`
  - `xFlashMutex`：W25Q64 读写
  - `xDataMutex`：全局最新数据 `g_latest_data`（采集任务写、联网任务读）
- **二值信号量 `xKeySemaphore`**：用于**中断到任务的同步**。按键 EXTI0 中断里 `xSemaphoreGiveFromISR` 释放信号量并 `portYIELD_FROM_ISR` 触发切换，KeyTask 平时阻塞等待，无按键时不占用 CPU。
- **软件定时器 `xStatusTimer`**：100ms 周期自动重载，是 LED 与蜂鸣器的**唯一控制者**。把人机反馈从业务任务中剥离，集中由系统状态渲染，避免多个任务争抢同一 GPIO。LED 状态优先级：撤防常灭 > 报警快闪（100ms 翻转）> 在线常亮 > 离线慢闪（500ms 翻转）；蜂鸣器仅在「报警 && 布防 && 未消音」时鸣响。
- **全局状态标志**：`g_armed`（布防）、`g_muted`（消音）、`g_alarm`（报警）均为 `volatile uint8_t` 单字节变量，在 Cortex-M3 上读写天然原子，配合 `volatile` 保证多任务/中断可见性，因此不再额外加互斥量。

### 中断管理

- `main` 中调用 `NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)` 设置为 4 位全抢占、0 位子优先级，这是 FreeRTOS 在 Cortex-M3 上的强制要求，否则中断优先级会被错误编码。
- `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`：**只有抢占优先级数值 ≥ 5（逻辑更低）的中断，才能在 ISR 中调用 `...FromISR()` 系列 API**。本工程按键 EXTI0 与 USART3 接收中断的抢占优先级均设为 6，满足该约束。
- ESP8266 接收采用「USART3 中断 + 512 字节环形缓冲区 + 二值信号量」的事件驱动模型：中断只做入队（单生产者），WiFi 任务出队解析（单消费者）；缓冲区大小取 2 的幂，用 `&(SIZE-1)` 代替取模运算；中断内对 ORE/FE/NE/PE 错误标志统一「读 SR 再读 DR」清除，防止中断风暴。
- 提供栈溢出钩子 `vApplicationStackOverflowHook` 与 malloc 失败钩子 `vApplicationMallocFailedHook`，钩子内仅打印并停机（不再获取互斥量，避免二次死锁）。

## 通信协议

- **单总线（DHT22）**：主机拉低起始信号，按高电平时长区分数据位（约 26~28μs 为 0、约 70μs 为 1），读取期间保证微秒级时序不被打断。
- **软件 I2C（OLED）**：GPIO 模拟 SCL/SDA 时序，开漏输出配合上拉电阻，OLED 地址 0x78。
- **软件 SPI（W25Q64）**：GPIO 模拟片选/时钟/收发，全双工模式 0，驱动外部 Flash。
- **UART（ESP8266 + 调试）**：USART3 收发 AT 指令与数据，USART1 重定向 `printf` 调试输出，均为 115200、8N1。

## 关键设计

### 多任务职责分离与资源保护
按「采集—显示—联网—交互」拆分为独立任务，通过队列传递数据、互斥量保护外设与全局变量、信号量做中断同步，避免了裸机大循环中各模块相互阻塞、时序耦合的问题；任一任务在等待（延时/队列/信号量）时都会让出 CPU 给其他任务。

### 按键状态机
四态流转 `IDLE → DEBOUNCE → PRESSED → RELEASE`，非阻塞设计，每次扫描只做一次状态判断，消抖 20ms、长按判定 1s；由外部中断唤醒任务，兼顾低 CPU 占用与可靠识别。长按切换布防/撤防，报警状态下短按消音，恢复正常后自动取消消音。

### ESP8266 连接状态机
五态管理 `IDLE → CWMODE → CWJAP → CIPSTART → ONLINE`，每一步 AT 指令都带超时，失败自动回退到 IDLE 重试；ONLINE 状态每 5 秒发送 `AT+CIPSTATUS` 检测链路（STATUS:3），掉线后自动重连。AT 应答采用环形缓冲区 + 信号量事件驱动等待，不再忙等。

### Flash 存储设计
- 记录结构体 `Record_t` 共 8 字节（温度 float + 湿度 float）。
- 环形存储 500 条（500 × 8 = 4000 字节，一个 4KB 扇区可容纳），写指针保存在扇区起始保留地址。
- 采用「读—改—写」：先把扇区读到 RAM → 修改 → 擦除扇区 → 写回。
- 上电自动读出最近一条记录显示在 OLED。

### 时基与微秒延时
FreeRTOS 接管 SysTick 产生 1ms 系统节拍；通过 Tick Hook（`vApplicationTickHook`）维持原有的 `g_tick` 毫秒计数，使按键/状态机的 `GetTick_ms()` 继续可用。`Delay_us()` 使用 SysTick 查询模式临时实现精确微秒延时（DHT22/I2C 需要），进入前保存、退出后恢复 SysTick 配置，不破坏 RTOS 节拍。

## 项目结构

```
├── FreeRTOS/              # FreeRTOS 内核源码
│   ├── include/           # 内核头文件（task.h/queue.h/semphr.h/timers.h 等）
│   ├── tasks.c            # 任务调度
│   ├── queue.c            # 队列与信号量
│   ├── list.c             # 内核链表
│   ├── timers.c           # 软件定时器
│   ├── heap_4.c           # 堆内存管理（支持释放与相邻块合并）
│   ├── port.c             # Cortex-M3 移植层
│   └── portmacro.h
├── User/                  # 应用层
│   ├── main.c             # 4 个任务 + 队列/信号量/互斥量/软件定时器，业务逻辑
│   ├── FreeRTOSConfig.h   # FreeRTOS 内核配置
│   ├── stm32f10x_it.c     # 中断服务函数
│   └── stm32f10x_conf.h
├── System/                # 外设驱动层
│   ├── DHT22.c/h          # 单总线温湿度驱动
│   ├── OLED.c/h           # SSD1306 OLED 驱动
│   ├── I2C.c/h            # 软件 I2C 时序
│   ├── W25Q64.c/h         # SPI Flash 驱动与环形存储
│   ├── ESP.c/h            # ESP8266 AT 指令、状态机、环形缓冲
│   ├── Key.c/h            # 按键外部中断与状态机
│   ├── LED.c/h            # LED 驱动
│   ├── Buzzer.c/h         # 蜂鸣器驱动
│   ├── Alarm.c/h          # 越限报警判断（纯逻辑，不操作硬件）
│   └── Delay.c/h          # 微秒延时、毫秒计时、Tick Hook
├── Library/               # STM32F10x 标准外设库
├── Start/                 # 启动文件与系统初始化
└── project.uvprojx        # Keil5 工程文件
```

## 使用说明

### 编译环境
- Keil MDK 5.x（ARMCC V5）
- STM32F10x 标准外设库（StdPeriph_Lib）
- FreeRTOS（Cortex-M3 移植，heap_4）

### WiFi 与 TCP 配置
修改 `System/ESP.c` 中的宏定义：
```c
#define WIFI_SSID     "你的WiFi名称"
#define WIFI_PASS     "你的WiFi密码"
#define TCP_SERVER    "服务器IP地址"
#define TCP_PORT      "8080"
```
电脑端可用 TCP/UDP 网络调试助手作为 TCP Server，按上面 IP 与端口监听即可收到上报数据。

### 报警阈值
修改 `System/Alarm.h`：
```c
#define TEMP_MAX  30.0f   // 温度报警阈值（℃）
#define HUMI_MAX  80.0f   // 湿度报警阈值（%）
```

### 烧录方式
使用 ST-Link / J-Link 下载器，烧录编译生成的 hex 或 axf 文件。

## 演示视频

[【STM32项目】基于 STM32 的环境监测系统演示视频 - 哔哩哔哩](https://www.bilibili.com/video/BV1Xtaw6yEkv)

## 作者

周宏鹏 · 嵌入式软件开发方向
