#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include "../common.h"
#include "../third_party/nlohmann/json.hpp"
// 命令枚举
enum CICommands
{
    CMD_BOOT = 0,
    CMD_KICK_BOOT = 1,
    CMD_ONCE_KICK_BOOT = 2,
    CMD_REBOOT = 3,
    CMD_POWER_OFF = 4,
    CMD_SEND_FILE = 5,
    CMD_EXECUTE = 6,
    CMD_SET_BAUDRATE = 7,
    CMD_SET_EXEC_ADDR = 8,
    CMD_REPARTITION = 9,
    CMD_WRITE_PART = 10,
    CMD_READ_PART = 11,
    CMD_ERASE_PART = 12,
    CMD_RESET_CURRENT_STATUS = 13,
    CMD_SET_BLK_SIZE = 14,
    CMD_SET_KEEP_CHARGE = 15,
    CMD_SET_HIGH_SPEED = 16,
    CMD_SET_NO_FDL_MODE = 17,
    CMD_SET_FBLK_SIZE = 18,
    CMD_SET_EXEC_ADDR_V2 = 19,
    CMD_SET_USE_CPTABLE = 20
};
// 刷机包信息
struct PackageInfo {
    std::string name;
    std::string author;
    std::string author_contact;
    std::string version;
    std::string partition_list_path; // 包内分区表文件路径
    std::string device_info; // 设备信息
    std::vector<std::string> images;
    bool UseCustomPartitionList = false; // 是否强制使用包内分区表文件
    bool isUseCI = false; // CI 工作流
    bool isPac = false; // 是否为 PAC 包
    // JSON文件对应项（示例）：
    // "NAME": "Package Name",
    // "AUTHOR": "Author Name",
    // "AUTHOR_CONTACT": "Author Contact",
    // "VERSION": "1.0",
    // "PARTITION_LIST_PATH": "partitions.txt",
    // "DEVICE_INFO": "Device specific information",
    // "IMAGES": ["image1.bin", "image2.bin"],
    // "USE_CUSTOM_PARTITION_LIST": "TRUE",
    // "USE_CI": "TRUE",
    // "IS_PAC": "FALSE"
    // IS_PAC为TRUE时解析PAC并烧录，此时USE_CI和USE_CUSTOM_PARTITION_LIST不能被设置，否则报错
    // USE_CI和USE_CUSTOM_PARTITION_LIST被设置时IS_PAC必须为FALSE，否则报错
    // 保存镜像的文件夹名必须是“Images”, 镜像名必须是分区名.img/.bin
};
struct CmdReturn {
    bool success;
    std::string message;
};
class PackageParser 
{
    PackageParser(std::string path);
    ~PackageParser();
public:
    PackageInfo getPackageInfo();
    CmdReturn CICmdExcecuter(CICommands cmd, std::vector<std::string> args = {});
    bool isBadPackage = false;
private:
    std::string package_path;
    PackageInfo info;
    PackageInfo parsePackage();
    int CI_boot_device(int waitTime, bool isKick, bool isOnce);
    char* execfile = nullptr;
};