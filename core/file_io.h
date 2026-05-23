#pragma once
#include <stdio.h>

struct FileDeleter {
    void operator()(FILE* f) const noexcept {
        if (f) fclose(f);
    }
};
using UniqueFile = std::unique_ptr<FILE, FileDeleter>;

FILE* oxfopen(const char* fn, const char* mode);
FILE* my_oxfopen(const char* fn, const char* mode);
UniqueFile oxfopen_unique(const char* fn, const char* mode);
UniqueFile my_oxfopen_unique(const char* fn, const char* mode);