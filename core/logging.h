#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

void ERR_EXIT(const char* format, ...);
void DEG_LOG(int type, const char* format, ...);
void print_mem(FILE *f, const uint8_t *buf, size_t len);
void print_string(FILE *f, const void *src, size_t n);
int print_to_string(char* dest, size_t dest_size, const void* src, size_t n, int mode);
