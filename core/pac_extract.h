/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "result.h"
#include "spd_protocol.h"  // for Stages enum


bool pac_extract(const char* fn, const char* folder);
bool pac_flash(spdio_t* io, const char* floder);
std::string ExtractPartitionsWithTags(const std::string& xmlContent);
std::string FindFirstXMLFile(const std::string& folderPath);
std::string FindFDLInExtFloder(const char* folder, Stages mode);

