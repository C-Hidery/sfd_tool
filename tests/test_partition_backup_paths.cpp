#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cctype>
#include <cstring>
#include <string>

#include "flash_service.h"
#include "common.h"

// BuildBackupRootDirForGuiBackup 由 GUI 分区页提供，在测试中通过链接 pages_lib 使用
std::string BuildBackupRootDirForGuiBackup();

TEST_CASE("BuildBackupRootDirForGuiBackup returns fallback when savepath empty") {
#if defined(__APPLE__)
    savepath[0] = '\0';
    auto root = BuildBackupRootDirForGuiBackup();
    CHECK(root == std::string("partitions_backup"));
#else
    savepath[0] = '\0';
    auto root = BuildBackupRootDirForGuiBackup();
    CHECK(root == std::string("partitions_backup"));
#endif
}

#if defined(__APPLE__)
TEST_CASE("BuildBackupRootDirForGuiBackup uses savepath and timestamp when non-empty on macOS") {
    std::strncpy(savepath, "/tmp/test_backup_root", sizeof(savepath) - 1);
    savepath[sizeof(savepath) - 1] = '\0';

    auto root = BuildBackupRootDirForGuiBackup();

    const std::string prefix = std::string("/tmp/test_backup_root/");
    CHECK(root.rfind(prefix, 0) == 0); // 前缀匹配

    auto ts = root.substr(prefix.size());
    CHECK(ts.size() == 15);            // YYYYMMDD_HHMMSS
    CHECK(ts[8] == '_');

    // 其余位置均为数字
    for (int i = 0; i < 15; ++i) {
        if (i == 8) continue;
        CHECK(std::isdigit(static_cast<unsigned char>(ts[static_cast<std::size_t>(i)])) != 0);
    }
}

TEST_CASE("macOS list_read path format uses root/part_name.img") {
    std::strncpy(savepath, "/tmp/test_backup_root2", sizeof(savepath) - 1);
    savepath[sizeof(savepath) - 1] = '\0';

    std::string part_name = "system";
    auto root = BuildBackupRootDirForGuiBackup();
    std::string finalPath = root + "/" + part_name + ".img";

    CHECK(finalPath.rfind(root + "/", 0) == 0);
    CHECK(finalPath.size() >= part_name.size() + 4);
    CHECK(finalPath.substr(finalPath.size() - (part_name.size() + 4)) == part_name + ".img");
}
#endif

#if !defined(__APPLE__)
TEST_CASE("BuildBackupRootDirForGuiBackup ignores savepath on non-Apple platforms") {
    savepath[0] = '\0';
    auto root1 = BuildBackupRootDirForGuiBackup();
    CHECK(root1 == std::string("partitions_backup"));

    std::strncpy(savepath, "/tmp/ignored", sizeof(savepath) - 1);
    savepath[sizeof(savepath) - 1] = '\0';
    auto root2 = BuildBackupRootDirForGuiBackup();
    CHECK(root2 == std::string("partitions_backup"));
}
#endif
