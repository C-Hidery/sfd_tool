#include "doctest.h"

#include "flash_service.h"

extern "C" {
    extern char savepath[256];
}

TEST_CASE("BuildBackupRootDirForGuiBackup returns fallback when savepath empty") {
#if defined(__APPLE__)
    savepath[0] = '\0';
    auto root = BuildBackupRootDirForGuiBackup();
    CHECK(root == std::string("partitions_backup"));
#else
    auto root = BuildBackupRootDirForGuiBackup();
    CHECK(root == std::string("partitions_backup"));
#endif
}
