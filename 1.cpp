/*
 * 这个文件用于把 KPZ101 压电控制和 SNET03 同步采集合并到一个流程里。
 * 相关参考来自 kpz101.cpp 和 kongzhi/cjk/example/example.cpp。
 */

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Thorlabs.MotionControl.KCube.Piezo.h"

#ifdef _MSC_VER
#pragma comment(lib, "Thorlabs.MotionControl.KCube.Piezo.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

// 限制数值落在允许范围内。
static double clampValue(double value, double lower, double upper)
{
    if (value < lower) return lower;
    if (value > upper) return upper;
    return value;
}

// 统一检查 Kinesis 返回码。
static void checkKinesisError(short errorCode, const char* functionName)
{
    if (errorCode != 0)
    {
        throw std::runtime_error(std::string(functionName) +
                                 " failed, error code = " +
                                 std::to_string(errorCode));
    }
}

// 统一抛出采集卡相关异常。
static void throwSocketError(const std::string& message)
{
    throw std::runtime_error(message + ", winsock error = " +
                             std::to_string(WSAGetLastError()));
}

// 保存 PZT 的连接参数和运动参数。
struct PiezoConfig
{
    std::string serialNo = "29000011";
    double maxTravelUm = 20.0;
    double maxVoltageV = 75.0;
    double rampStepVoltageV = 1.0;
    DWORD rampStepDelayMs = 20;
    int zeroRepeatCount = 3;
    DWORD zeroRepeatDelayMs = 150;
};

// 保存采集卡通信参数和数据保存参数。
struct Snet03Config
{
    std::string ipAddress = "192.168.1.101";
    unsigned short port = 8060;
    std::array<unsigned char, 4> sampleRateCommand = {0xAB, 0xC2, 0x00, 0x01};
    std::array<unsigned char, 4> triggerCommand = {0xAB, 0xC6, 0xAA, 0x00};
    std::array<unsigned char, 4> rangeCommand = {0xAB, 0xC3, 0x00, 0xFF};
    std::array<unsigned char, 4> channelMaskCommand = {0xAB, 0xC4, 0x00, 0x01};
    std::array<unsigned char, 4> startCommand = {0xAB, 0xD1, 0x00, 0xAA};
    int packetBytes = 1220;
    int packetHeaderBytes = 20;
    int socketTimeoutMs = 200;
    DWORD postMotionCaptureMs = 100;
    std::string saveDirectory = "captures";
};

// 保存每一轮运动的目标位移。
struct ShotPlan
{
    double targetUm = 0.0;
};

// 保存整个程序的配置。
struct AppConfig
{
    PiezoConfig piezo;
    Snet03Config snet03;
    std::vector<ShotPlan> shots = {{5.0}, {10.0}, {15.0}};
};

// 保存单次采集得到的结果。
struct CaptureResult
{
    std::vector<unsigned char> rawBytes;
    std::size_t packetCount = 0;
};

// 管理 Winsock 生命周期。
class WinsockRuntime
{
public:
    // 初始化 Winsock。
    WinsockRuntime()
    {
        WSADATA wsadata{};
        if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
        {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    // 释放 Winsock。
    ~WinsockRuntime()
    {
        WSACleanup();
    }
};

// 负责控制 KPZ101 的开环电压输出。
class KPZ101PiezoStage
{
public:
    // 保存配置但暂不连接设备。
    explicit KPZ101PiezoStage(const PiezoConfig& config)
        : config_(config)
    {
    }

    // 连接 KPZ101 并切换为软件开环控制。
    void open()
    {
        checkKinesisError(TLI_BuildDeviceList(), "TLI_BuildDeviceList");
        checkKinesisError(PCC_Open(config_.serialNo.c_str()), "PCC_Open");
        opened_ = true;

        if (!PCC_StartPolling(config_.serialNo.c_str(), 200))
        {
            throw std::runtime_error("PCC_StartPolling failed");
        }

        Sleep(300);

        if (!PCC_LoadSettings(config_.serialNo.c_str()))
        {
            throw std::runtime_error("PCC_LoadSettings failed");
        }

        checkKinesisError(PCC_Enable(config_.serialNo.c_str()), "PCC_Enable");
        checkKinesisError(PCC_SetPositionControlMode(config_.serialNo.c_str(), PZ_OpenLoop),
                          "PCC_SetPositionControlMode");
        checkKinesisError(PCC_SetVoltageSource(config_.serialNo.c_str(), PZ_SoftwareOnly),
                          "PCC_SetVoltageSource");

        currentVoltageV_ = 0.0;
    }

    // 按电压直接输出到压电控制器。
    void setVoltage(double targetVoltageV)
    {
        const double limitedVoltage = clampValue(targetVoltageV,
                                                 -config_.maxVoltageV,
                                                 config_.maxVoltageV);
        const short deviceUnits = static_cast<short>(
            std::lround((limitedVoltage / config_.maxVoltageV) * 32767.0));

        checkKinesisError(PCC_SetOutputVoltage(config_.serialNo.c_str(), deviceUnits),
                          "PCC_SetOutputVoltage");
        currentVoltageV_ = limitedVoltage;
    }

    // 把位移换算成开环电压。
    double umToVoltage(double targetUm) const
    {
        const double limitedUm = clampValue(targetUm, 0.0, config_.maxTravelUm);
        return (limitedUm / config_.maxTravelUm) * config_.maxVoltageV;
    }

    // 以分步斜坡方式移动到目标位移。
    void rampToUm(double targetUm)
    {
        rampToVoltage(umToVoltage(targetUm));
    }

    // 以分步斜坡方式回到 0V 并多次重复归零。
    void returnToZeroRepeatedly()
    {
        rampToVoltage(0.0);

        for (int index = 0; index < config_.zeroRepeatCount; ++index)
        {
            Sleep(config_.zeroRepeatDelayMs);
            setVoltage(0.0);
        }
    }

    // 安全断开设备。
    void close()
    {
        if (!opened_)
        {
            return;
        }

        try
        {
            returnToZeroRepeatedly();
        }
        catch (...)
        {
        }

        PCC_StopPolling(config_.serialNo.c_str());
        PCC_Close(config_.serialNo.c_str());
        opened_ = false;
    }

    // 析构时自动收尾。
    ~KPZ101PiezoStage()
    {
        close();
    }

private:
    // 以固定步长和固定间隔拉坡到目标电压。
    void rampToVoltage(double targetVoltageV)
    {
        const double limitedTarget = clampValue(targetVoltageV,
                                                -config_.maxVoltageV,
                                                config_.maxVoltageV);
        const double deltaVoltage = limitedTarget - currentVoltageV_;
        const double stepVoltage = (config_.rampStepVoltageV > 0.0)
                                     ? config_.rampStepVoltageV
                                     : std::fabs(deltaVoltage);
        const int stepCount = std::max(1,
                                       static_cast<int>(
                                           std::ceil(std::fabs(deltaVoltage) / stepVoltage)));
        const double startVoltage = currentVoltageV_;

        for (int step = 1; step <= stepCount; ++step)
        {
            const double nextVoltage =
                startVoltage + (deltaVoltage * static_cast<double>(step) / stepCount);
            setVoltage(nextVoltage);
            Sleep(config_.rampStepDelayMs);
        }
    }

    PiezoConfig config_{};
    double currentVoltageV_ = 0.0;
    bool opened_ = false;
};

// 负责和 SNET03 采集卡进行 UDP 通信。
class Snet03CaptureCard
{
public:
    // 保存配置并准备建立连接。
    explicit Snet03CaptureCard(const Snet03Config& config)
        : config_(config)
    {
    }

    // 启动采集并开启接收线程。
    void startCapture()
    {
        reconnectSocket();
        clearCaptureBuffer();
        receiveError_.clear();
        receiving_.store(true);
        receiveThread_ = std::thread(&Snet03CaptureCard::receiveLoop, this);
        sendCommand(config_.sampleRateCommand);
        sendCommand(config_.triggerCommand);
        sendCommand(config_.rangeCommand);
        sendCommand(config_.channelMaskCommand);
        sendCommand(config_.startCommand);
    }

    // 停止采集并取回收到的原始数据。
    CaptureResult stopCapture()
    {
        receiving_.store(false);

        if (receiveThread_.joinable())
        {
            receiveThread_.join();
        }

        if (!receiveError_.empty())
        {
            throw std::runtime_error(receiveError_);
        }

        std::lock_guard<std::mutex> lock(bufferMutex_);
        return CaptureResult{rawBytes_, packetCount_};
    }

    // 关闭当前连接，作为下一轮前的软件侧清零。
    void resetForNextShot()
    {
        closeSocket();
    }

    // 析构时自动停止线程并关闭连接。
    ~Snet03CaptureCard()
    {
        receiving_.store(false);

        if (receiveThread_.joinable())
        {
            receiveThread_.join();
        }

        closeSocket();
    }

private:
    // 把控制命令发给采集卡。
    void sendCommand(const std::array<unsigned char, 4>& command)
    {
        const int sentBytes = send(socket_,
                                   reinterpret_cast<const char*>(command.data()),
                                   static_cast<int>(command.size()),
                                   0);
        if (sentBytes != static_cast<int>(command.size()))
        {
            throwSocketError("send command failed");
        }
    }

    // 重建 UDP 连接并清掉本地旧状态。
    void reconnectSocket()
    {
        closeSocket();

        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ == INVALID_SOCKET)
        {
            throwSocketError("create udp socket failed");
        }

        const DWORD timeoutValue = static_cast<DWORD>(config_.socketTimeoutMs);
        if (setsockopt(socket_,
                       SOL_SOCKET,
                       SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&timeoutValue),
                       sizeof(timeoutValue)) == SOCKET_ERROR)
        {
            throwSocketError("set socket timeout failed");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(config_.port);
        address.sin_addr.S_un.S_addr = inet_addr(config_.ipAddress.c_str());

        if (address.sin_addr.S_un.S_addr == INADDR_NONE)
        {
            throw std::runtime_error("invalid SNET03 ip address");
        }

        if (connect(socket_,
                    reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)) == SOCKET_ERROR)
        {
            throwSocketError("connect snet03 failed");
        }
    }

    // 关闭当前 UDP 连接。
    void closeSocket()
    {
        if (socket_ != INVALID_SOCKET)
        {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

    // 清空上一轮缓存，避免混入旧数据。
    void clearCaptureBuffer()
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        rawBytes_.clear();
        packetCount_ = 0;
    }

    // 在后台持续收取采集卡数据包。
    void receiveLoop()
    {
        std::vector<char> packetBuffer(config_.packetBytes);

        while (receiving_.load())
        {
            const int receivedBytes = recv(socket_,
                                           packetBuffer.data(),
                                           static_cast<int>(packetBuffer.size()),
                                           0);

            if (receivedBytes > 0)
            {
                std::lock_guard<std::mutex> lock(bufferMutex_);
                rawBytes_.insert(rawBytes_.end(),
                                 reinterpret_cast<unsigned char*>(packetBuffer.data()),
                                 reinterpret_cast<unsigned char*>(packetBuffer.data()) +
                                     receivedBytes);
                ++packetCount_;
                continue;
            }

            if (receivedBytes == 0)
            {
                continue;
            }

            const int socketError = WSAGetLastError();
            if (!receiving_.load())
            {
                break;
            }

            if (socketError == WSAETIMEDOUT || socketError == WSAEWOULDBLOCK)
            {
                continue;
            }

            receiveError_ = "recv from snet03 failed, winsock error = " +
                            std::to_string(socketError);
            receiving_.store(false);
        }
    }

    Snet03Config config_{};
    SOCKET socket_ = INVALID_SOCKET;
    std::atomic<bool> receiving_{false};
    std::thread receiveThread_{};
    std::mutex bufferMutex_{};
    std::vector<unsigned char> rawBytes_{};
    std::size_t packetCount_ = 0;
    std::string receiveError_{};
};

// 生成适合保存的文件名。
static std::string buildCapturePath(const std::string& directory,
                                    std::size_t shotIndex,
                                    double targetUm)
{
    std::ostringstream fileName;
    const std::time_t now = std::time(nullptr);
    std::tm localTime{};
    localtime_s(&localTime, &now);

    fileName << "shot_" << std::setw(2) << std::setfill('0') << shotIndex
             << "_" << std::put_time(&localTime, "%Y%m%d_%H%M%S")
             << "_" << std::fixed << std::setprecision(1) << targetUm << "um.csv";
    return directory + "\\" + fileName.str();
}

// 确保保存目录已经存在。
static void ensureDirectoryExists(const std::string& directory)
{
    const DWORD attributes = GetFileAttributesA(directory.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        return;
    }

    if (CreateDirectoryA(directory.c_str(), nullptr) == 0)
    {
        const DWORD errorCode = GetLastError();
        if (errorCode != ERROR_ALREADY_EXISTS)
        {
            throw std::runtime_error("failed to create save directory");
        }
    }
}

// 把单包原始数据里的 CH1 样本解析成 CSV 行。
static std::uint64_t writePacketCh1ToCsv(std::ofstream& output,
                                         const unsigned char* packetBytes,
                                         int packetSize,
                                         int headerBytes,
                                         std::uint64_t sampleIndex)
{
    if (packetSize <= headerBytes)
    {
        throw std::runtime_error("invalid SNET03 packet size");
    }

    const int payloadBytes = packetSize - headerBytes;
    if ((payloadBytes % 2) != 0)
    {
        throw std::runtime_error("invalid SNET03 payload length");
    }

    for (int offset = headerBytes; offset < packetSize; offset += 2)
    {
        const unsigned int sampleCode =
            (static_cast<unsigned int>(packetBytes[offset]) << 8) |
            static_cast<unsigned int>(packetBytes[offset + 1]);
        output << sampleIndex << "," << sampleCode << "\n";
        ++sampleIndex;
    }

    return sampleIndex;
}

// 把本轮采集得到的原始字节解析为 CH1 的 CSV 文件。
static std::uint64_t saveCaptureToCsv(const std::string& filePath,
                                      const CaptureResult& capture,
                                      const Snet03Config& config)
{
    if (capture.rawBytes.empty())
    {
        throw std::runtime_error("no capture data received from SNET03");
    }

    if (config.packetHeaderBytes <= 0 || config.packetHeaderBytes >= config.packetBytes)
    {
        throw std::runtime_error("invalid SNET03 packet layout");
    }

    if ((capture.rawBytes.size() % static_cast<std::size_t>(config.packetBytes)) != 0)
    {
        throw std::runtime_error("capture data size is not aligned to SNET03 packets");
    }

    std::ofstream output(filePath, std::ios::binary);
    if (!output)
    {
        throw std::runtime_error("failed to open capture file for writing");
    }

    std::vector<char> outputBuffer(1 << 20);
    output.rdbuf()->pubsetbuf(outputBuffer.data(),
                              static_cast<std::streamsize>(outputBuffer.size()));
    output << "sample_index,ch1_code\n";

    std::uint64_t sampleIndex = 0;
    for (std::size_t packetOffset = 0;
         packetOffset < capture.rawBytes.size();
         packetOffset += static_cast<std::size_t>(config.packetBytes))
    {
        sampleIndex = writePacketCh1ToCsv(output,
                                          capture.rawBytes.data() + packetOffset,
                                          config.packetBytes,
                                          config.packetHeaderBytes,
                                          sampleIndex);
    }

    if (!output.good())
    {
        throw std::runtime_error("failed to write capture file");
    }

    return sampleIndex;
}

// 运行一次“开始运动即采集，运动结束即停采”的完整流程。
static void runSingleShot(std::size_t shotIndex,
                          const ShotPlan& shot,
                          KPZ101PiezoStage& stage,
                          Snet03CaptureCard& captureCard,
                          const Snet03Config& snet03Config)
{
    std::cout << "第 " << shotIndex << " 次开始，目标位移 "
              << shot.targetUm << " um。" << std::endl;

    captureCard.startCapture();
    stage.rampToUm(shot.targetUm);
    Sleep(snet03Config.postMotionCaptureMs);

    const CaptureResult capture = captureCard.stopCapture();
    const std::string filePath =
        buildCapturePath(snet03Config.saveDirectory, shotIndex, shot.targetUm);
    const std::uint64_t sampleCount = saveCaptureToCsv(filePath, capture, snet03Config);

    std::cout << "第 " << shotIndex << " 次采集完成，保存 "
              << capture.packetCount << " 个数据包、"
              << sampleCount << " 个点到 "
              << filePath << "。" << std::endl;

    stage.returnToZeroRepeatedly();
    captureCard.resetForNextShot();

    std::cout << "第 " << shotIndex << " 次回零完成，等待下一次运动。"
              << std::endl;
}

// 主流程负责串起初始化、循环采集和收尾。
int main()
{
    try
    {
        const AppConfig config{};
        ensureDirectoryExists(config.snet03.saveDirectory);

        WinsockRuntime winsock;
        KPZ101PiezoStage stage(config.piezo);
        Snet03CaptureCard captureCard(config.snet03);

        stage.open();

        for (std::size_t index = 0; index < config.shots.size(); ++index)
        {
            runSingleShot(index + 1,
                          config.shots[index],
                          stage,
                          captureCard,
                          config.snet03);
        }

        stage.close();
        std::cout << "全部流程完成。" << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
}
