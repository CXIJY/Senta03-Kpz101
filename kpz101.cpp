/*
 * 这个文件用于控制 Thorlabs KPZ101 压电控制器。
 * 依赖本机安装的 Kinesis SDK，并与 VS Code 的构建配置配合使用。
 */

#include <windows.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Thorlabs.MotionControl.KCube.Piezo.h"

#ifdef _MSC_VER
#pragma comment(lib, "Thorlabs.MotionControl.KCube.Piezo.lib")
#endif

// 限制输入值在允许范围内。
static double clampValue(double x, double lo, double hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// 将 SDK 返回码转换成可读异常。
static void checkError(short err, const char* funcName)
{
    if (err != 0)
    {
        throw std::runtime_error(std::string(funcName) +
                                 " failed, error code = " +
                                 std::to_string(err));
    }
}

class KPZ101PiezoStage
{
public:
    // 保存设备参数，但此时还不连接设备。
    KPZ101PiezoStage(const std::string& serialNo,
                     double maxTravelUm,
                     double maxVoltageV)
        : serialNo_(serialNo),
          maxTravelUm_(maxTravelUm),
          maxVoltageV_(maxVoltageV),
          opened_(false)
    {
    }

    // 建立连接并切换到软件开环控制。
    void open()
    {
        checkError(TLI_BuildDeviceList(), "TLI_BuildDeviceList");
        checkError(PCC_Open(serialNo_.c_str()), "PCC_Open");
        opened_ = true;

        if (!PCC_StartPolling(serialNo_.c_str(), 200))
        {
            throw std::runtime_error("PCC_StartPolling failed");
        }

        Sleep(300);

        if (!PCC_LoadSettings(serialNo_.c_str()))
        {
            throw std::runtime_error("PCC_LoadSettings failed");
        }

        checkError(PCC_Enable(serialNo_.c_str()), "PCC_Enable");
        Sleep(200);

        checkError(PCC_SetPositionControlMode(serialNo_.c_str(), PZ_OpenLoop),
                   "PCC_SetPositionControlMode(PZ_OpenLoop)");

        Sleep(100);

        checkError(PCC_SetVoltageSource(serialNo_.c_str(), PZ_SoftwareOnly),
                   "PCC_SetVoltageSource");
    }

    // 直接按电压控制，适合开环模式。
    void setVoltage(double voltageV)
    {
        voltageV = clampValue(voltageV, -maxVoltageV_, maxVoltageV_);

        short deviceUnits = static_cast<short>(
            std::lround((voltageV / maxVoltageV_) * 32767.0)
        );

        checkError(PCC_SetOutputVoltage(serialNo_.c_str(), deviceUnits),
                   "PCC_SetOutputVoltage");
    }

    // 按位移做线性近似，本质上还是输出电压。
    void moveToUm(double targetUm)
    {
        targetUm = clampValue(targetUm, 0.0, maxTravelUm_);

        double voltageV = (targetUm / maxTravelUm_) * maxVoltageV_;
        setVoltage(voltageV);
    }

    // 回到零电压状态。
    void moveToZero()
    {
        setVoltage(0.0);
    }

    // 停止轮询并断开设备。
    void close()
    {
        if (opened_)
        {
            try
            {
                moveToZero();
                Sleep(100);
            }
            catch (...)
            {
            }

            PCC_StopPolling(serialNo_.c_str());
            PCC_Close(serialNo_.c_str());
            opened_ = false;
        }
    }

    ~KPZ101PiezoStage()
    {
        close();
    }

private:
    std::string serialNo_;
    double maxTravelUm_;
    double maxVoltageV_;
    bool opened_;
};

int main()
{
    // 改成你的 KPZ101 序列号。
    const std::string serialNo = "2925XXXX";

    // 这里填压电执行器的真实满量程位移。
    const double maxTravelUm = 20.0;

    // 这里必须与你的执行器额定电压和控制器设置一致。
    const double maxVoltageV = 75.0;

    try
    {
        KPZ101PiezoStage stage(serialNo, maxTravelUm, maxVoltageV);

        stage.open();

        std::cout << "Move to 5 um..." << std::endl;
        stage.moveToUm(5.0);
        Sleep(1000);

        std::cout << "Move to 10 um..." << std::endl;
        stage.moveToUm(10.0);
        Sleep(1000);

        std::cout << "Move to 15 um..." << std::endl;
        stage.moveToUm(15.0);
        Sleep(1000);

        std::cout << "Back to 0 um..." << std::endl;
        stage.moveToUm(0.0);
        Sleep(1000);

        stage.close();

        std::cout << "Done." << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
