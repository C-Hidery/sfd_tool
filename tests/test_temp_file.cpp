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
        std::ofstream out(a, std::ios::binary);
        out << "payload";
    }
    CHECK(std::filesystem::exists(std::filesystem::u8path(a)));

    remove_file(a);
    CHECK(!std::filesystem::exists(std::filesystem::u8path(a)));

    // 删除不存在的文件不应抛异常
    remove_file(a);
    CHECK(true);
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
bool is_valid_utf8(const std::string& s)
{
    size_t i = 0;
    while (i < s.size())
    {
        unsigned char c = (unsigned char)s[i];
        size_t extra = 0;
        if (c < 0x80) { i += 1; continue; }
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= s.size()) return false;
        for (size_t k = 1; k <= extra; ++k)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}
} // namespace

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
