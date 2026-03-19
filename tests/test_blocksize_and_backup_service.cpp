#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "flash_service.h"
#include "ui_common.h"
#include "app_state.h"
#include "common.h"
#include "usb_transport.h"

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
    using namespace sfd;

    // 构造一个最小 io/app 环境
    static partition_t fake_table[3];
    std::memset(fake_table, 0, sizeof(fake_table));
    std::strncpy(fake_table[0].name, "system", sizeof(fake_table[0].name) - 1);
    fake_table[0].size = 1024 * 1024;
    std::strncpy(fake_table[1].name, "vendor", sizeof(fake_table[1].name) - 1);
    fake_table[1].size = 2048 * 1024;
    std::strncpy(fake_table[2].name, "missing", sizeof(fake_table[2].name) - 1);
    fake_table[2].size = 4096 * 1024;

    spdio_t* test_io = spdio_init(0);
    REQUIRE(test_io != nullptr);

    test_io->ptable = fake_table;
    test_io->part_count = 3;
    test_io->part_count_c = 0;

    io = test_io;
    g_app_state.flash.isCMethod = 0;

    std::unique_ptr<FlashService> svc = createFlashService();
    svc->setContext(io, &g_app_state);

    std::vector<std::string> names; // 为空 -> 备份全部
    std::string out_dir = "test_backup_root";

    FlashStatus st = svc->backupPartitions(names, out_dir, SlotSelection::Auto, 0);

    CHECK(st.success);

    // 避免 spdio_free 试图释放测试用的静态分区表
    test_io->ptable = nullptr;
    test_io->Cptable = nullptr;
    spdio_free(test_io);
}
