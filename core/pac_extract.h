#pragma once
#include <string>
#include "result.h"
#include "spd_protocol.h"  // for Stages enum

bool pac_extract(const char* fn, const char* folder);
bool pac_flash(const char* folder, spdio_t* io);
std::string ExtractPartitionsWithTags(const std::string& xmlContent);
std::string FindFirstXMLFile(const std::string& folderPath);
std::string FindFDLInExtFloder(const char* folder, Stages mode);

namespace sfd {

Result<void> pac_extract_result(const char* fn, const char* folder);

} // namespace sfd
