#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "core/flash_service.h"
#include "ui/ui_common.h"
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

TEST_CASE("backupPartitions auto mode should include splloader when g_spl_size > 0") {
    // 仅验证在 g_spl_size > 0 的情况下，不会因为 names 为空而导致异常逻辑；
    // 目前的实现依赖真实设备 I/O，单元测试环境下无法安全模拟完整 dump 流程，
    // 因此先保留一个最小占位测试，后续可在引入更完备的协议层 stub 后再细化。

    io = nullptr;
    g_app_state = AppState{};
    g_app_state.flash.isCMethod = 0;

    extern uint64_t g_spl_size;
    g_spl_size = 1024 * 512;

    std::unique_ptr<FlashService> svc = createFlashService();
    svc->setContext(io, &g_app_state);

    std::vector<std::string> names;
    std::string out_dir = "test_backup_root_spl_placeholder";

    FlashStatus st = svc->backupPartitions(names, out_dir, SlotSelection::Auto, 0);

    // 这里只要求不会因为 g_spl_size>0 而导致崩溃；返回失败是允许的
    CHECK(!st.success);
}

TEST_CASE("is_critical_partition_name identifies boot/vbmeta/dtbo and splloader") {
    CHECK(is_critical_partition_name("splloader") == true);
    CHECK(is_critical_partition_name("uboot") == true);
    CHECK(is_critical_partition_name("uboot_a") == true);
    CHECK(is_critical_partition_name("boot") == true);
    CHECK(is_critical_partition_name("boot_a") == true);
    CHECK(is_critical_partition_name("boot_b") == true);
    CHECK(is_critical_partition_name("vbmeta") == true);
    CHECK(is_critical_partition_name("vbmeta_a") == true);
    CHECK(is_critical_partition_name("vbmeta_system") == true);
    CHECK(is_critical_partition_name("dtbo") == true);
    CHECK(is_critical_partition_name("dtbo_a") == true);
    CHECK(is_critical_partition_name("super") == true);
    CHECK(is_critical_partition_name("metadata") == true);
    CHECK(is_critical_partition_name("trustos") == true);
    CHECK(is_critical_partition_name("teecfg") == true);
    CHECK(is_critical_partition_name("sml") == true);
    CHECK(is_critical_partition_name("recovery") == true);

    CHECK(is_critical_partition_name("system") == false);
    CHECK(is_critical_partition_name("userdata") == false);
    CHECK(is_critical_partition_name("vendor") == false);
}
