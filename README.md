# 项目说明

## 项目功能简介
- 这个项目用于联动 KPZ101 压电控制器和 SNET03 采集卡。
- 程序会在压电开始运动时启动采集，在运动结束后停止收包，并把第一通道的数据保存成 `csv` 文件。
- 当前采集配置固定为第一通道、`1M` 采样率，采集结果默认保存到 `captures` 目录。

## 技术架构
- `1.cpp` 是主程序，负责把 KPZ101 控制、SNET03 通信、数据保存串成一个完整流程。
- KPZ101 通过本机安装的 Thorlabs Kinesis SDK 控制。
- SNET03 通过 UDP 命令配置和收包，程序会在采集结束后把原始包解析成 `csv`。
- `package_portable.ps1` 用来生成可直接拷贝到其他电脑的便携运行文件夹。

## 本地运行方法
- 先确认电脑已安装 `C:\Program Files\Thorlabs\Kinesis`。
- 打开 `1.cpp`，按实际设备修改序列号、采集卡 IP、位移目标和等待时间。
- 在项目根目录执行：

```powershell
chcp 65001
C:\mingw64\bin\g++.exe -std=gnu++17 -IC:/Program Files/Thorlabs/Kinesis 1.cpp -LC:/Program Files/Thorlabs/Kinesis -l:Thorlabs.MotionControl.KCube.Piezo.lib -lws2_32 -o 1.exe
Copy-Item "C:\Program Files\Thorlabs\Kinesis\Thorlabs.MotionControl.KCube.Piezo.dll" . -Force
Copy-Item "C:\Program Files\Thorlabs\Kinesis\Thorlabs.MotionControl.DeviceManager.dll" . -Force
.\1.exe
```

## 部署方法和命令
- 这是本地硬件控制程序，不涉及单独部署。
- 如果要拷到其他电脑使用，在项目根目录执行：

```powershell
chcp 65001
powershell -ExecutionPolicy Bypass -File .\package_portable.ps1
```

- 生成结果在 `portable_package\1_portable`。
- 把 `1_portable` 文件夹直接压缩后拷到目标电脑即可。
- 目标电脑建议先安装 Thorlabs Kinesis，再启动 `1.exe`。

## 测试方法和常用命令
- 当前主要检查方式是本地编译和启动验证：

```powershell
chcp 65001
C:\mingw64\bin\g++.exe -std=gnu++17 -IC:/Program Files/Thorlabs/Kinesis 1.cpp -LC:/Program Files/Thorlabs/Kinesis -l:Thorlabs.MotionControl.KCube.Piezo.lib -lws2_32 -o 1.exe
.\1.exe
```

- 便携包生成验证：

```powershell
chcp 65001
.\package_portable.ps1
```

## 搜索记录
- `skills.sh`：未检索到与 “Thorlabs PZT + UDP 采集卡同步控制” 直接对应的现成工作流，因此实现主要基于本地现有示例。
  链接：`https://skills.sh/`
- GitHub：参考了 Thorlabs 官方 `Motion_Control_Examples`，用于确认 Kinesis 设备控制方式。
  链接：`https://github.com/Thorlabs/Motion_Control_Examples`
- GitHub 搜索中没有看到公开的 SNET03 完整协议仓库，所以采集卡控制仍以项目里的 `kongzhi/cjk/example/example.cpp` 为准。

## 已完成功能列表
- 已完成 KPZ101 和 SNET03 的联动主流程。
- 已把 SNET03 配置改成只采第一通道、`1M` 采样率。
- 已把采集结果改成保存为 `csv` 文件。
- 已支持单次保存百万级点数的数据。
- 已生成可直接打包拷贝的便携运行文件夹。

## 待办事项
- 用真实设备联机验证不同实验时长下的点数和波形是否符合预期。
- 验证目标电脑在安装 Kinesis 后是否都能稳定识别 KPZ101。
- 根据实际实验需要继续调整位移目标和回零等待时间。
