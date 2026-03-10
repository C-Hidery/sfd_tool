#pragma once
#include <string>
#include "result.h"

bool pac_extract(const char* fn, const char* folder);
std::string ExtractPartitionsWithTags(const std::string& xmlContent);
std::string FindFirstXMLFile(const std::string& folderPath);

namespace sfd {

Result<void> pac_extract_result(const char* fn, const char* folder);

} // namespace sfd
