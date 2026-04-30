#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "../third_party/miniz/miniz.h"

#ifdef _WIN32
    #include <direct.h>
    #define mkdir(path, mode) _mkdir(path)
    #define PATH_SEP '\\'
#else
    #include <sys/stat.h>
    #define PATH_SEP '/'
#endif

// 递归创建目录
void createDirectories(const std::string& path) {
    if (path.empty()) return;
    
    std::string dir;
    size_t pos = 0;
    while ((pos = path.find_first_of("/\\", pos)) != std::string::npos) {
        dir = path.substr(0, pos++);
        if (!dir.empty()) {
            mkdir(dir.c_str(), 0755);
        }
    }
    // 创建最后一级目录（如果路径以分隔符结尾）
    if (!dir.empty() && (path.back() == '/' || path.back() == '\\')) {
        mkdir(path.c_str(), 0755);
    }
}

// 从路径中提取目录部分
std::string getDirectoryPath(const std::string& filepath) {
    size_t pos = filepath.find_last_of("/\\");
    if (pos != std::string::npos) {
        return filepath.substr(0, pos);
    }
    return "";
}

// 解压 ZIP 文件到指定目录
bool extractZip(const std::string& zipPath, const std::string& destDir) {
    // 1. 打开 ZIP 文件
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) {
        std::cerr << "Failed to open zip file: " << zipPath << std::endl;
        return false;
    }
    
    // 2. 获取 ZIP 中的文件数量
    int fileCount = (int)mz_zip_reader_get_num_files(&zip);
    if (fileCount == 0) {
        std::cerr << "Zip file is empty" << std::endl;
        mz_zip_reader_end(&zip);
        return false;
    }
    
    std::cout << "Found " << fileCount << " files in zip" << std::endl;
    
    // 3. 遍历所有文件
    for (int i = 0; i < fileCount; ++i) {
        // 获取文件名
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            std::cerr << "Failed to get file stat for index " << i << std::endl;
            continue;
        }
        
        std::string filename = fileStat.m_filename;
        
        // 跳过 . 和 .. 目录
        if (filename == "." || filename == "..") {
            continue;
        }
        
        // 构建完整的输出路径
        std::string outputPath = destDir;
        if (!destDir.empty() && destDir.back() != '/' && destDir.back() != '\\') {
            outputPath += PATH_SEP;
        }
        outputPath += filename;
        
        // 处理目录（文件名以 '/' 结尾）
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            std::cout << "Creating directory: " << outputPath << std::endl;
            createDirectories(outputPath);
            continue;
        }
        
        // 确保父目录存在
        std::string parentDir = getDirectoryPath(outputPath);
        if (!parentDir.empty()) {
            createDirectories(parentDir + PATH_SEP);
        }
        
        // 解压文件
        std::cout << "Extracting: " << filename << std::endl;
        
        // 方法1：解压到内存，然后写入文件
        size_t uncompressedSize = fileStat.m_uncomp_size;
        std::vector<unsigned char> buffer(uncompressedSize);
        
        if (mz_zip_reader_extract_to_mem(&zip, i, buffer.data(), uncompressedSize, 0)) {
            FILE* outFile = fopen(outputPath.c_str(), "wb");
            if (outFile) {
                fwrite(buffer.data(), 1, uncompressedSize, outFile);
                fclose(outFile);
            } else {
                std::cerr << "Failed to create file: " << outputPath << std::endl;
            }
        } else {
            std::cerr << "Failed to extract: " << filename << std::endl;
        }
    }
    
    // 4. 清理
    mz_zip_reader_end(&zip);
    
    return true;
}

