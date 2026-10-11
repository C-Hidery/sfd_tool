/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 *
 * Known-answer tests for the AES-128-ECB helper used by the EXTENDED `e_bl`
 * command.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aes128.h"

#include <stdint.h>
#include <string.h>

namespace {

void hex_to_bytes(const char* hex, uint8_t* out, size_t n)
{
    auto nibble = [](char c) -> uint8_t
    {
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
        return (uint8_t)(c - 'A' + 10);
    };
    for (size_t i = 0; i < n; i++)
        out[i] = (uint8_t)((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
}

bool eq_hex(const uint8_t* buf, size_t n, const char* hex)
{
    uint8_t expected[64];
    if (n > sizeof(expected)) return false;
    hex_to_bytes(hex, expected, n);
    return memcmp(buf, expected, n) == 0;
}

} // namespace

TEST_CASE("aes128 fips-197 known answer")
{
    // FIPS-197 Appendix C.1
    uint8_t key[16];
    uint8_t pt[16];
    uint8_t ct[16];
    hex_to_bytes("000102030405060708090a0b0c0d0e0f", key, sizeof(key));
    hex_to_bytes("00112233445566778899aabbccddeeff", pt, sizeof(pt));

    aes128_ecb_encrypt(key, pt, sizeof(pt), ct);

    CHECK(eq_hex(ct, sizeof(ct), "69c4e0d86a7b0430d8cdb78070b4c55a"));
}

TEST_CASE("aes128 ecb encrypts multiple independent blocks")
{
    uint8_t key[16];
    uint8_t pt[32];
    uint8_t ct[32];
    hex_to_bytes("000102030405060708090a0b0c0d0e0f", key, sizeof(key));
    hex_to_bytes("00112233445566778899aabbccddeeff"
                 "00112233445566778899aabbccddeeff", pt, sizeof(pt));

    aes128_ecb_encrypt(key, pt, sizeof(pt), ct);

    CHECK(eq_hex(ct, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"));
    CHECK(eq_hex(ct + 16, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"));
}

TEST_CASE("aes128 all-zero vector")
{
    uint8_t key[16] = {0};
    uint8_t pt[16] = {0};
    uint8_t ct[16] = {0};

    aes128_ecb_encrypt(key, pt, sizeof(pt), ct);

    CHECK(eq_hex(ct, sizeof(ct), "66e94bd4ef8a2c3b884cfa59ca342b2e"));
}

TEST_CASE("aes128 ignores trailing partial block")
{
    uint8_t key[16] = {0};
    uint8_t pt[20] = {0};
    uint8_t ct[20];
    memset(ct, 0xAA, sizeof(ct));
    hex_to_bytes("66e94bd4ef8a2c3b884cfa59ca342b2e", ct, 16);
    memset(ct + 16, 0xAA, 4);

    aes128_ecb_encrypt(key, pt, 20, ct);

    // First block changes; the trailing 4 bytes must be left untouched.
    CHECK(eq_hex(ct, 16, "66e94bd4ef8a2c3b884cfa59ca342b2e"));
    CHECK(ct[16] == 0xAA);
    CHECK(ct[19] == 0xAA);
}
