# 基于 STM32F407 的智能天气家居控制中枢

本项目基于 STM32F407ZGT6 与 ESP32 AT 通信模块，构建一个集天气信息显示、RTC 时间管理、MQTT 家居控制、远程指令交互与 OTA 固件升级于一体的嵌入式智能家居控制中枢。

系统通过 STM32 负责主控逻辑、UI 显示、任务调度、数据解析和 OTA 写入管理，ESP32 作为联网通信模块提供 WiFi、HTTP、NTP 与 MQTT 通信能力。项目使用 FreeRTOS 进行多任务调度，并加入串口异步消息解复用机制，用于处理天气请求、MQTT 订阅消息和其他 ESP32 AT 返回数据之间的并发接收问题。

## 主要功能

- 天气信息获取、解析与显示
- RTC 时间显示与 NTP 网络校时
- MQTT 远程控制与状态上报
- ESP32 AT 串口通信与异步消息解复用
- 基于 MQTT 的 OTA 固件升级
- Bootloader 固件校验、搬运与跳转

## 工程结构

- `Core/`：主 App 源码
- `Bootloader/`：Bootloader 工程与启动更新逻辑
- `MDK-ARM/`：Keil App 工程配置
- `Tools/`：辅助工具脚本，例如 OTA 固件发送脚本
- `Docs/`：OTA 协议、烧录流程和调试说明

## 开发环境

- MCU：STM32F407ZGT6
- 通信模块：ESP32 AT 固件
- RTOS：FreeRTOS
- IDE：Keil MDK uVision
- 联网协议：HTTP、NTP、MQTT

