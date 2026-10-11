/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 *
 * Compact AES-128-ECB implementation (encrypt only).
 * Ported from spreadtrum_flash/common.c; kept dependency-free so it can live
 * in core_lib and be covered by unit tests.
 */
#include "aes128.h"

#include <string.h>

namespace {

const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

inline uint8_t aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

inline uint32_t aes_mixcol32(uint32_t c)
{
    uint8_t b0 = (uint8_t)(c), b1 = (uint8_t)(c >> 8),
            b2 = (uint8_t)(c >> 16), b3 = (uint8_t)(c >> 24);
    uint8_t t = (uint8_t)(b0 ^ b1 ^ b2 ^ b3);
    return ((uint32_t)(aes_xtime((uint8_t)(b0 ^ b1)) ^ b0 ^ t)) |
           ((uint32_t)(aes_xtime((uint8_t)(b1 ^ b2)) ^ b1 ^ t) << 8) |
           ((uint32_t)(aes_xtime((uint8_t)(b2 ^ b3)) ^ b2 ^ t) << 16) |
           ((uint32_t)(aes_xtime((uint8_t)(b3 ^ b0)) ^ b3 ^ t) << 24);
}

void aes128_keyexpand(const uint8_t key[16], uint8_t rk[176])
{
    static const uint8_t rcon[11] = { 0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36 };
    uint32_t w[44];
    for (int i = 0; i < 4; i++)
    {
        w[i] = (uint32_t)key[i * 4] |
               ((uint32_t)key[i * 4 + 1] << 8) |
               ((uint32_t)key[i * 4 + 2] << 16) |
               ((uint32_t)key[i * 4 + 3] << 24);
    }
    for (int i = 4; i < 44; i++)
    {
        uint32_t tmp = w[i - 1];
        if ((i & 3) == 0)
        {
            tmp = (tmp >> 8) | (tmp << 24);
            tmp = (uint32_t)aes_sbox[tmp & 0xff] |
                  ((uint32_t)aes_sbox[(tmp >> 8) & 0xff] << 8) |
                  ((uint32_t)aes_sbox[(tmp >> 16) & 0xff] << 16) |
                  ((uint32_t)aes_sbox[(tmp >> 24) & 0xff] << 24);
            tmp ^= (uint32_t)rcon[i >> 2];
        }
        w[i] = w[i - 4] ^ tmp;
    }
    memcpy(rk, w, 44 * sizeof(uint32_t));
}

void aes128_encrypt_block(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16])
{
    uint32_t s0, s1, s2, s3, w[44];
    memcpy(&s0, in, 4); memcpy(&s1, in + 4, 4);
    memcpy(&s2, in + 8, 4); memcpy(&s3, in + 12, 4);
    memcpy(w, rk, 44 * sizeof(uint32_t));

    s0 ^= w[0]; s1 ^= w[1]; s2 ^= w[2]; s3 ^= w[3];

    for (int r = 1; r <= 9; r++)
    {
        uint8_t a[16];
        memcpy(a, &s0, 4); memcpy(a + 4, &s1, 4);
        memcpy(a + 8, &s2, 4); memcpy(a + 12, &s3, 4);
        uint32_t t0 = (uint32_t)aes_sbox[a[0]] |
                      ((uint32_t)aes_sbox[a[5]] << 8) |
                      ((uint32_t)aes_sbox[a[10]] << 16) |
                      ((uint32_t)aes_sbox[a[15]] << 24);
        uint32_t t1 = (uint32_t)aes_sbox[a[4]] |
                      ((uint32_t)aes_sbox[a[9]] << 8) |
                      ((uint32_t)aes_sbox[a[14]] << 16) |
                      ((uint32_t)aes_sbox[a[3]] << 24);
        uint32_t t2 = (uint32_t)aes_sbox[a[8]] |
                      ((uint32_t)aes_sbox[a[13]] << 8) |
                      ((uint32_t)aes_sbox[a[2]] << 16) |
                      ((uint32_t)aes_sbox[a[7]] << 24);
        uint32_t t3 = (uint32_t)aes_sbox[a[12]] |
                      ((uint32_t)aes_sbox[a[1]] << 8) |
                      ((uint32_t)aes_sbox[a[6]] << 16) |
                      ((uint32_t)aes_sbox[a[11]] << 24);
        t0 = aes_mixcol32(t0); t1 = aes_mixcol32(t1);
        t2 = aes_mixcol32(t2); t3 = aes_mixcol32(t3);
        s0 = t0 ^ w[r * 4 + 0];
        s1 = t1 ^ w[r * 4 + 1];
        s2 = t2 ^ w[r * 4 + 2];
        s3 = t3 ^ w[r * 4 + 3];
    }

    uint8_t a[16];
    memcpy(a, &s0, 4); memcpy(a + 4, &s1, 4);
    memcpy(a + 8, &s2, 4); memcpy(a + 12, &s3, 4);
    s0 = (uint32_t)aes_sbox[a[0]] | ((uint32_t)aes_sbox[a[5]] << 8) |
         ((uint32_t)aes_sbox[a[10]] << 16) | ((uint32_t)aes_sbox[a[15]] << 24);
    s1 = (uint32_t)aes_sbox[a[4]] | ((uint32_t)aes_sbox[a[9]] << 8) |
         ((uint32_t)aes_sbox[a[14]] << 16) | ((uint32_t)aes_sbox[a[3]] << 24);
    s2 = (uint32_t)aes_sbox[a[8]] | ((uint32_t)aes_sbox[a[13]] << 8) |
         ((uint32_t)aes_sbox[a[2]] << 16) | ((uint32_t)aes_sbox[a[7]] << 24);
    s3 = (uint32_t)aes_sbox[a[12]] | ((uint32_t)aes_sbox[a[1]] << 8) |
         ((uint32_t)aes_sbox[a[6]] << 16) | ((uint32_t)aes_sbox[a[11]] << 24);
    s0 ^= w[40]; s1 ^= w[41]; s2 ^= w[42]; s3 ^= w[43];

    memcpy(out, &s0, 4); memcpy(out + 4, &s1, 4);
    memcpy(out + 8, &s2, 4); memcpy(out + 12, &s3, 4);
}

} // namespace

void aes128_ecb_encrypt(const uint8_t key[16],
                        const uint8_t* pt, size_t pt_len,
                        uint8_t* ct)
{
    uint8_t rk[176];
    aes128_keyexpand(key, rk);
    for (size_t i = 0; i + 16 <= pt_len; i += 16)
        aes128_encrypt_block(rk, pt + i, ct + i);
}
