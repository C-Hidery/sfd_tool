#pragma once
#include <string>

bool pac_extract(const char* fn, const char* folder);
std::string ExtractPartitionsWithTags(const std::string& xmlContent);
std::string FindFirstXMLFile(const std::string& folderPath);
