# 架构说明

## 文件 / 模块职责
- `1.cpp`：联动主程序，负责初始化 KPZ101、控制 SNET03、同步采集、把第一通道保存成 CSV，并在每轮后回零。
- `kpz101.cpp`：单独验证 KPZ101 控制链路的参考程序。
- `package_portable.ps1`：生成可拷贝到其他电脑的便携运行文件夹。
- `PORTABLE_README.txt`：告诉目标电脑使用者怎么启动程序，以及为什么仍建议先安装 Kinesis。
- `kongzhi/cjk/example/example.cpp`：SNET03 官方示例风格的基础收包程序，用来提供 UDP 命令格式和基础通信流程。
- `kongzhi/cjk/example/SNET03_*.m`：读取示例二进制文件并按通道拆分的参考脚本。
- `.vscode/c_cpp_properties.json`：告诉 VS Code 去哪里找 Kinesis 头文件。
- `.vscode/tasks.json`：提供当前项目的 g++ 编译命令。

## 模块之间的调用关系
- `main()` 先创建配置，再初始化 Winsock、KPZ101 和 SNET03。
- `runSingleShot()` 负责单次实验流程：
  1. 启动 SNET03 采集线程
  2. 让 KPZ101 按分步斜坡运动
  3. 运动结束后停止收包
  4. 按采集卡数据包格式提取 CH1，并保存成 CSV
  5. 把 KPZ101 回到 0V
  6. 重建 SNET03 UDP 连接，等待下一轮
- `KPZ101PiezoStage` 只负责压电输出和回零，不直接处理采集逻辑。
- `Snet03CaptureCard` 只负责发送采集卡命令、收 UDP 包和维护本轮缓存。
- `package_portable.ps1` 会把 `1.exe`、必需 DLL、默认设置文件和说明文档组装到 `portable_package\1_portable`。

## 关键设计决定和原因
- 用“软件分步斜坡”代替一次性跳变电压：
  这样能在程序里明确知道运动开始和结束的时刻，便于和采集线程同步。
- 采集线程在后台持续收包：
  这样 PZT 运动和 UDP 接收不会互相阻塞，更接近“同步开始、同步结束”的目标。
- 每轮结束后重建 UDP 连接：
  当前提供的 SNET03 示例里没有看到明确停采命令，所以用重连方式做软件侧清零，更稳。
- 采集时先收原始包，结束后再转 CSV：
  这样采集阶段更稳，不容易因为文本写盘过慢影响 UDP 收包，同时最终输出仍是你需要的 `csv`。
- 便携包只打进运行期必需文件：
  这样便于你直接压缩拷走，同时避免把整个 Kinesis 安装目录都带上。
