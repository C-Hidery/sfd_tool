/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 *
 * Tests for the system temp-file helpers used by the *_to_temp dump APIs.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "file_io.h"

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("make_temp_file_path returns a unique path under the system temp dir")
{
    const std::string a = make_temp_file_path("nr_fixnv1");
    const std::string b = make_temp_file_path("nr_fixnv1");

    CHECK(!a.empty());
    CHECK(!b.empty());
    CHECK(a != b); // 唯一性

    const auto tmp = std::filesystem::temp_directory_path();
    CHECK(std::filesystem::u8path(a).parent_path() == tmp);
    CHECK(a.find("nr_fixnv1") != std::string::npos);
}

TEST_CASE("make_temp_file_path sanitizes tags and remove_file deletes")
{
    const std::string a = make_temp_file_path("sfd test/../x:b");
    CHECK(!a.empty());
    // 非法字符应被替换为 '_'，不产生子目录
    CHECK(std::filesystem::u8path(a).parent_path() == std::filesystem::temp_directory_path());

    {
        std::ofstream out(std::filesystem::u8path(a), std::ios::binary);
        out << "payload";
    }
    CHECK(std::filesystem::exists(std::filesystem::u8path(a)));

    remove_file(a);
    CHECK(!std::filesystem::exists(std::filesystem::u8path(a)));

    // 删除不存在的文件不应抛异常
    remove_file(a);
    CHECK(true);
}

TEST_CASE("TempFile falls back to a fresh temp name in the current path when constructed empty")
{
    // 空路径检查：显式构造空路径时在当前目录生成唯一临时文件名（绝对路径）。
    TempFile empty_path{std::string()};
    CHECK(!empty_path.empty());
    CHECK(empty_path.path().find("sfd_tool_") != std::string::npos);
    const std::filesystem::path p = std::filesystem::u8path(empty_path.path());
    CHECK(p.is_absolute());
    CHECK(p.parent_path() == std::filesystem::current_path());

    // 两次空路径构造应得到不同路径
    TempFile other{std::string()};
    CHECK(empty_path.path() != other.path());

    // 默认构造仍为空对象，用作失败哨兵。
    TempFile none;
    CHECK(none.empty());

    // 回退路径可以正常创建/写入，并在作用域结束时被 RAII 删除
    std::string q;
    {
        TempFile tf{std::string()};
        q = tf.path();
        std::ofstream out(std::filesystem::u8path(q), std::ios::binary);
        out << "payload";
        out.close();
        CHECK(std::filesystem::exists(std::filesystem::u8path(q)));
    }
    CHECK(!std::filesystem::exists(std::filesystem::u8path(q)));
}

TEST_CASE("TempFile fallback path stays valid across chdir")
{
    // 回退路径是绝对路径：析构发生在 chdir 之后也应删除原文件。
    const std::filesystem::path orig = std::filesystem::current_path();
    std::string q;
    {
        TempFile tf{std::string()};
        q = tf.path();
        {
            std::ofstream out(std::filesystem::u8path(q), std::ios::binary);
            out << "payload";
        }
        CHECK(std::filesystem::exists(std::filesystem::u8path(q)));

        std::error_code ec;
        std::filesystem::current_path(std::filesystem::temp_directory_path(ec), ec);
    }
    std::error_code ec;
    std::filesystem::current_path(orig, ec);
    CHECK(!std::filesystem::exists(std::filesystem::u8path(q)));
}

TEST_CASE("make_temp_file_path_in_cwd returns a unique absolute path in the current directory")
{
    const std::string a = make_temp_file_path_in_cwd("cwd_tag");
    const std::string b = make_temp_file_path_in_cwd("cwd_tag");
    CHECK(!a.empty());
    CHECK(a != b);
    CHECK(a.find("cwd_tag") != std::string::npos);
    const std::filesystem::path pa = std::filesystem::u8path(a);
    CHECK(pa.is_absolute());
    CHECK(pa.parent_path() == std::filesystem::current_path());
}

TEST_CASE("TempFile is RAII and write_buffer_to_temp writes the payload")
{
    std::string p;
    {
        TempFile tf = write_buffer_to_temp("hello", 5, "unit");
        CHECK(!tf.empty());
        p = tf.path();
        CHECK(std::filesystem::exists(std::filesystem::u8path(p)));
        CHECK(std::filesystem::file_size(std::filesystem::u8path(p)) == 5);
    }
    // 离开作用域后自动删除
    CHECK(!std::filesystem::exists(std::filesystem::u8path(p)));

    // release() 放弃所有权，析构不再删除
    std::string q;
    {
        TempFile tf = write_buffer_to_temp("x", 1, "release");
        CHECK(!tf.empty());
        q = tf.release();
        CHECK(tf.empty());
    }
    CHECK(std::filesystem::exists(std::filesystem::u8path(q)));
    remove_file(q);
    CHECK(!std::filesystem::exists(std::filesystem::u8path(q)));
}

namespace {
// 与 core/file_io.cpp 的严格校验保持一致：拒绝 overlong / 代理区 / >U+10FFFF。
bool is_valid_utf8(const std::string& s)
{
    size_t i = 0;
    while (i < s.size())
    {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i += 1; continue; }
        size_t extra = 0;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c < 0xC2) return false;
        else if (c <= 0xDF) extra = 1;
        else if (c <= 0xEF) {
            extra = 2;
            if (c == 0xE0) lo = 0xA0;
            else if (c == 0xED) hi = 0x9F;
        } else if (c <= 0xF4) {
            extra = 3;
            if (c == 0xF0) lo = 0x90;
            else if (c == 0xF4) hi = 0x8F;
        } else return false;
        if (i + extra >= s.size()) return false;
        unsigned char c1 = (unsigned char)s[i + 1];
        if (c1 < lo || c1 > hi) return false;
        for (size_t k = 2; k <= extra; ++k)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}
} // namespace

TEST_CASE("strict UTF-8 validation rejects overlong, surrogates and >U+10FFFF")
{
    CHECK(is_valid_utf8("ascii"));
    CHECK(is_valid_utf8("\xE5\x88\x86\xE5\x8C\xBA"));       // 分区
    CHECK(is_valid_utf8("\xF0\x9F\x98\x80"));               // U+1F600

    CHECK_FALSE(is_valid_utf8("\xC0\x80"));                 // overlong NUL
    CHECK_FALSE(is_valid_utf8("\xE0\x80\x80"));             // overlong 3 字节
    CHECK_FALSE(is_valid_utf8("\xF0\x80\x80\x80"));         // overlong 4 字节
    CHECK_FALSE(is_valid_utf8("\xED\xA0\x80"));             // UTF-16 代理区
    CHECK_FALSE(is_valid_utf8("\xF4\x90\x80\x80"));         // > U+10FFFF
    CHECK_FALSE(is_valid_utf8("\xF5\x80\x80\x80"));         // 非法首字节
    CHECK_FALSE(is_valid_utf8("\xE5\x88"));                 // 截断
}

TEST_CASE("utf8_to_path/path_to_utf8 round-trip non-ASCII UTF-8 paths")
{
    const std::string utf8 = "分区备份/测试 文件.img";
    const std::filesystem::path p = utf8_to_path(utf8);
    CHECK(!p.empty());
    CHECK(path_to_utf8(p) == utf8);
}

TEST_CASE("make_temp_file_path always returns valid UTF-8")
{
    // tag 里混入非 ASCII / 非法字符，名称部分会被清洗为 ASCII
    const std::string a = make_temp_file_path("\xE5\x88\x86\xE5\x8C\xBA x:y");
    CHECK(!a.empty());
    CHECK(is_valid_utf8(a));

    const std::string b = make_temp_file_path(nullptr);
    CHECK(!b.empty());
    CHECK(is_valid_utf8(b));
}
