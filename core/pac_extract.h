#pragma once
#include <string>

bool pac_extract(const char* fn, const char* folder);
bool pac_flash(const char* folder, spdio_t* io);
std::string ExtractPartitionsWithTags(const std::string& xmlContent);
std::string FindFirstXMLFile(const std::string& folderPath);
std::string findBaseForID(const std::string& filename, const std::string& targetID);
