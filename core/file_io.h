#pragma once
#include <stdio.h>

FILE* xfopen(const char* fn, const char* mode);
FILE* my_fopen(const char *fn, const char *mode);
FILE* my_xfopen(const char* fn, const char* mode);
FILE* oxfopen(const char* fn, const char* mode);
FILE* my_oxfopen(const char* fn, const char* mode);
