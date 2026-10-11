/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * Minimal AES-128-ECB block encryption used by the EXTENDED `e_bl` command
 * (the device HUK is used as the key to seal "VerifiedBoot-UNLOCK").
 *
 * `pt_len` must be a multiple of 16; the function encrypts every 16-byte
 * block independently (ECB) and writes `pt_len` bytes to `ct`.
 */
void aes128_ecb_encrypt(const uint8_t key[16],
                        const uint8_t* pt, size_t pt_len,
                        uint8_t* ct);
