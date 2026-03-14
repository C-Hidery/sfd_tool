#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "spd_protocol.h"

TEST_CASE("spd_checksum basic parity") {
    const unsigned char data[] = {0x01, 0x02, 0x03, 0x04};
    unsigned c1 = spd_checksum(0, data, sizeof(data), CHK_FIXZERO);
    unsigned c2 = spd_checksum(0, data, sizeof(data), CHK_FIXZERO);
    CHECK(c1 == c2);
}
