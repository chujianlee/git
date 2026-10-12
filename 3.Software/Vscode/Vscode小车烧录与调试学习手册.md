# VS Code 小车工程烧录与调试学习手册

## 1. 先分清两个工程

这个文件夹里有两类程序：

- `1.Vscode-servo\servo`：舵机调试程序，只用于舵机。
- `Vscode-Shibo-Robot-PIOProj-main\Shibo-Robot-PIOProj-main`：小车主程序，用于 ESP32、网页控制、IMU 和两个轮子。

本次烧录使用的是第二个目录：

`D:\download\智能小车项目\3.Software\Vscode\Vscode-Shibo-Robot-PIOProj-main\Shibo-Robot-PIOProj-main`

打开文件夹时，必须打开**包含 `platformio.ini` 的那一层**，不要只打开外层压缩包目录。

## 2. 连接顺序

1. 小车轮子架空，先不要落地。
2. 用 USB-C 连接主控板和电脑。
3. 暂时关闭 ATK-XCOM 或其他串口助手，避免占用串口。
4. 电池问题还没有解决时，只用 USB-C 做烧录和架空测试，不进行落地或弹跳测试。

## 3. 在 VS Code 中编译

1. 打开 VS Code。
2. 选择“文件 → 打开文件夹”。
3. 选择上面的 `Shibo-Robot-PIOProj-main` 工程根目录。
4. 点击左侧 PlatformIO 小蚂蚁图标。
5. 找到 `Project Tasks → esp32dev → General → Build`。
6. 看到 `SUCCESS` 才表示编译通过。

也可以在 VS Code 集成终端执行：

```powershell
cd D:\download\智能小车项目\3.Software\Vscode\Vscode-Shibo-Robot-PIOProj-main\Shibo-Robot-PIOProj-main
pio run
```

## 4. 选择正确的串口

电脑可能同时显示多个 CH340，例如 COM4、COM5。不要只凭端口号猜，先看设备是否能被 ESP32 识别。

在工程终端执行：

```powershell
pio device list
```

本次实际验证结果：

- COM4：上传时出现 `Invalid head of packet`，没有完成烧录。
- COM5：识别为 `ESP32-D0WDQ6`，写入成功并显示 `Hash of data verified.`。

所以这次实际烧录端口是 **COM5**。下次重新插拔 USB 后，端口号可能变化，要重新确认。

## 5. 烧录操作

确认串口助手已经关闭后，在工程终端执行：

```powershell
pio run -t upload --upload-port COM5
```

成功标志是同时看到：

```text
Chip is ESP32...
Hash of data verified.
Hard resetting via RTS pin...
========================= [SUCCESS] =========================
```

烧录成功只代表固件写进 ESP32，不代表电池供电、两个轮子或落地平衡已经验证。

## 6. 串口监视

烧录完成后，关闭 PlatformIO 上传任务，再打开串口监视器：

```powershell
pio device monitor --port COM5 --baud 115200
```

如果使用 ATK-XCOM：

- 端口选择 COM5；
- 波特率选择 `115200`；
- 发送格式选择换行；
- 同一时间只能让一个软件打开 COM5。

启动时重点观察：

- `MOT: Init FOC ...`：两个电机的 FOC 初始化过程；
- `PPcheck`、`Zeroelec.angle`：编码器和磁铁对齐信息；
- `Robot Go` 后才允许运动输出。

## 7. 当前工程的网页控制

这份原始小车工程的热点配置在 `src/wifi_config.cpp`：

- 热点：`WLROBOT-TD`
- 密码：`wlrobot123`
- 地址：`http://192.168.1.11/`

手机或电脑要先连接 `WLROBOT-TD`，再访问上面的地址。不要继续使用旧地址 `192.168.4.1`。

## 8. 电池问题与测试边界

当前现象是 USB-C 能供电，独立电池不能稳定供电。这个问题优先检查电池、主板电源接口、拨动开关、插头线序和电源路径；重新烧录不能修复物理供电故障。

在电池没有确认前：

- 只做 USB-C 架空测试；
- 不把车放地上运行；
- 不做跳跃测试；
- 看到单轮异常、猛烈摆动或电机发热时立即停止并断电。

## 9. 这次烧录的工程版本说明

本次按你的路径烧录的是原始 `Vscode-Shibo-Robot-PIOProj-main` 工程。它和 `D:\download\智能小车项目\robotcat-firmware` 的模块化改进版不是同一份代码；原始工程没有后者新增的低压诊断、`MOTOR_STATUS`、`WHEEL_SPEED` 和双轮同步补偿模块。

如果下一步要继续调双轮速度或电池诊断，应先明确要烧录哪一份工程，避免在两个版本之间来回切换。

