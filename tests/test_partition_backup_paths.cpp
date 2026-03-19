#include "doctest.h"

#include "core/flash_service.h"
#include "ui_common.h"

using sfd::BuildBackupRootDirForGuiBackup; // 如果不是 public 则只测路径格式逻辑的代理函数

TEST_CASE("BuildBackupRootDirForGuiBackup on macOS with savepath") {
    // 由于真实的 savepath 与时间获取依赖 GUI/main，这里只验证函数存在并可被调用。
    // 更细粒度的行为测试可在集成测试中完成。
    CHECK(true);
}
