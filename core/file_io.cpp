#include "file_io.h"
#include "../common.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

FILE* xfopen(const char* fn, const char* mode) {
#ifdef _WIN32
    // Windows下处理中文路径：UTF-8 -> UTF-16 -> _wfopen
    FILE* file = nullptr;
    
    // 计算需要的宽字符缓冲区大小
    int wpath_len = MultiByteToWideChar(CP_UTF8, 0, fn, -1, nullptr, 0);
    int wmode_len = MultiByteToWideChar(CP_UTF8, 0, mode, -1, nullptr, 0);
    
    if (wpath_len <= 0 || wmode_len <= 0) {
        return nullptr;  // 转换失败
    }
    
    // 分配宽字符缓冲区
    wchar_t* wpath = (wchar_t*)malloc(wpath_len * sizeof(wchar_t));
    wchar_t* wmode = (wchar_t*)malloc(wmode_len * sizeof(wchar_t));
    
    if (wpath && wmode) {
        // 转换UTF-8到UTF-16
        MultiByteToWideChar(CP_UTF8, 0, fn, -1, wpath, wpath_len);
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, wmode_len);
        
        // 使用宽字符版本打开文件
        file = _wfopen(wpath, wmode);
    }
    
    // 释放内存
    free(wpath);
    free(wmode);
    
    return file;
#else
    // Linux/macOS等系统直接使用fopen，因为这些系统默认使用UTF-8
    return fopen(fn, mode);
#endif
}

FILE *my_fopen(const char *fn, const char *mode) {
	if (savepath[0]) {
		size_t fn_len = strlen(fn);
        size_t path_len = strlen(savepath);
		char* fix_fn = NEWN char[path_len + fn_len + 2]; // +2 for '/' and '\0'
        if (!fix_fn) return nullptr;
		char* ch;
		if ((ch = const_cast<char*>(strrchr(fn, '/')))) snprintf(fix_fn, path_len + fn_len + 2, "%s/%s", savepath, ch + 1);
		else if ((ch = const_cast<char*>(strrchr(fn, '\\')))) snprintf(fix_fn, path_len + fn_len + 2, "%s/%s", savepath, ch + 1);
		else snprintf(fix_fn, path_len + fn_len + 2, "%s/%s", savepath, fn);
		FILE* fe = fopen(fix_fn, mode);
		delete[] fix_fn;
		return fe;
	}
	else return fopen(fn, mode);
}

// SavePath 拼接版
FILE* my_xfopen(const char* fn, const char* mode) {
    FILE* file = nullptr;
    char* fix_fn = nullptr;
    
    if (savepath[0]) {
        // 分配内存
        size_t fn_len = strlen(fn);
        size_t path_len = strlen(savepath);
        fix_fn = (char*)malloc(path_len + fn_len + 2);
        if (!fix_fn) return nullptr;
        
        // 查找文件名部分
        const char* filename_part;
        const char* ch;
        
        if ((ch = strrchr(fn, '/'))) 
            filename_part = ch + 1;
        else if ((ch = strrchr(fn, '\\'))) 
            filename_part = ch + 1;
        else 
            filename_part = fn;
        
        // 拼接路径（使用snprintf更安全）
        snprintf(fix_fn, path_len + fn_len + 2, "%s/%s", savepath, filename_part);
    }
    
    // 确定要打开的文件路径
    const char* path_to_open = fix_fn ? fix_fn : fn;
    
#ifdef _WIN32
    // Windows下使用宽字符版本处理中文路径
    int wpath_len = MultiByteToWideChar(CP_UTF8, 0, path_to_open, -1, nullptr, 0);
    int wmode_len = MultiByteToWideChar(CP_UTF8, 0, mode, -1, nullptr, 0);

	if (wpath_len <= 0 || wmode_len <= 0) {
        return nullptr;  // 转换失败
    }
    
    wchar_t* wpath = (wchar_t*)malloc(wpath_len * sizeof(wchar_t));
    wchar_t* wmode = (wchar_t*)malloc(wmode_len * sizeof(wchar_t));
    
    if (wpath && wmode) {
        MultiByteToWideChar(CP_UTF8, 0, path_to_open, -1, wpath, wpath_len);
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, wmode_len);
        file = _wfopen(wpath, wmode);
        
        free(wpath);
        free(wmode);
    } else {
        // 内存分配失败时的处理
        if (wpath) free(wpath);
        if (wmode) free(wmode);
    }
#else
    // Linux/Mac等系统直接使用fopen
    file = file = fopen(path_to_open, mode);
#endif
    
    // 释放临时路径内存
    if (fix_fn) {
        free(fix_fn);
    }
    
    return file;
}

FILE* oxfopen(const char* fn, const char* mode) {
	FILE* file = xfopen(fn, mode);
	if(file == nullptr) file = fopen(fn, mode); // fallback
	return file;
}
FILE* my_oxfopen(const char* fn, const char* mode) {
	FILE* file = my_xfopen(fn, mode);
	if(file == nullptr) file = my_fopen(fn, mode); // fallback
	return file;
}
