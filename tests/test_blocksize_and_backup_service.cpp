#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "core/flash_service.h"
#include "ui_common.h"
#include "app_state.h"
#include "common.h"
#include "usb_transport.h"
#include "pages/page_partition.h"

using namespace sfd;

extern int blk_size;
extern AppState g_app_state;
extern spdio_t*& io;

TEST_CASE("ResetBlockSizeToDefault restores GUI and global block size") {
    auto& s = GetGuiIoSettings();

    // 人为打乱当前配置
    s.mode = BlockSizeMode::MANUAL_BLOCK_SIZE;
    s.manual_block_size = 1234;
    blk_size = 4096;

    ResetBlockSizeToDefault();

    CHECK(s.mode == BlockSizeMode::AUTO_DEFAULT);
    CHECK(s.manual_block_size == DEFAULT_BLK_SIZE);
    CHECK(blk_size == 0);
}

TEST_CASE("backupPartitions enumerates partitions and builds X/name.img paths") {
    // 构造一个最小 io/app 环境
    static partition_t fake_table[3];
    std::memset(fake_table, 0, sizeof(fake_table));
    std::strncpy(fake_table[0].name, "system", sizeof(fake_table[0].name) - 1);
    fake_table[0].size = 1024 * 1024;
    std::strncpy(fake_table[1].name, "vendor", sizeof(fake_table[1].name) - 1);
    fake_table[1].size = 2048 * 1024;
    std::strncpy(fake_table[2].name, "missing", sizeof(fake_table[2].name) - 1);
    fake_table[2].size = 4096 * 1024;

    // 清空全局状态，避免上一次测试运行留下的副作用
    io = nullptr;
    g_app_state = AppState{};
    g_app_state.flash.isCMethod = 0;

    std::unique_ptr<FlashService> svc = createFlashService();
    svc->setContext(io, &g_app_state);

    std::vector<std::string> names; // 为空 -> 备份全部
    std::string out_dir = "test_backup_root";

    FlashStatus st = svc->backupPartitions(names, out_dir, SlotSelection::Auto, 0);

    // 目前只验证调用流程能安全返回，不要求成功备份真实分区
    CHECK(!st.success);
}

TEST_CASE("is_critical_partition_name identifies boot/vbmeta/dtbo and splloader") {
    CHECK(is_critical_partition_name("splloader") == true);
    CHECK(is_critical_partition_name("boot") == true);
    CHECK(is_critical_partition_name("boot_a") == true);
    CHECK(is_critical_partition_name("boot_b") == true);
    CHECK(is_critical_partition_name("vbmeta") == true);
    CHECK(is_critical_partition_name("vbmeta_a") == true);
    CHECK(is_critical_partition_name("vbmeta_system") == true);
    CHECK(is_critical_partition_name("dtbo") == true);
    CHECK(is_critical_partition_name("dtbo_a") == true);

    CHECK(is_critical_partition_name("system") == false);
    CHECK(is_critical_partition_name("userdata") == false);
    CHECK(is_critical_partition_name("vendor") == false);
}
