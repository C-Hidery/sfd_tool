#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "pac_extract.h"
#include "result.h"

using sfd::ErrorCode;
using sfd::Result;

TEST_CASE("pac_extract_result rejects empty args") {
    auto r1 = sfd::pac_extract_result(nullptr, "out");
    CHECK_FALSE(r1);
    CHECK(r1.code == ErrorCode::InvalidArgument);

    auto r2 = sfd::pac_extract_result("pac.pac", "");
    CHECK_FALSE(r2);
    CHECK(r2.code == ErrorCode::InvalidArgument);
}
