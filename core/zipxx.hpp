#ifndef ZIPXX_HPP
#define ZIPXX_HPP

#include <zip.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace zipxx {

class Exception : public std::runtime_error {
public:
    explicit Exception(const std::string& what) : std::runtime_error(what) {}
};

struct EntryInfo {
    std::string  name;
    std::uint64_t size_uncompressed = 0;
    std::uint64_t size_compressed   = 0;
    std::int64_t  mtime             = 0;
    bool          is_directory      = false;
    bool          is_encrypted      = false;
};

class Archive {
public:
    enum class Mode { Read, Create, Append };

    Archive() noexcept = default;
    Archive(const std::filesystem::path& p, Mode mode) { open(p, mode); }
    ~Archive() { close(); }

    Archive(const Archive&)            = delete;
    Archive& operator=(const Archive&) = delete;

    Archive(Archive&& other) noexcept : zf_(other.zf_), mode_(other.mode_) {
        other.zf_ = nullptr;
    }
    Archive& operator=(Archive&& other) noexcept {
        if (this != &other) {
            close();
            zf_     = other.zf_;
            mode_   = other.mode_;
            other.zf_ = nullptr;
        }
        return *this;
    }

    // ---- 打开/关闭 -------------------------------------------------------

    void open(const std::filesystem::path& p, Mode mode) {
        close();
        int flags = 0;
        switch (mode) {
            case Mode::Read:   flags = ZIP_RDONLY;                    break;
            case Mode::Create: flags = ZIP_CREATE | ZIP_TRUNCATE;     break;
            case Mode::Append: flags = ZIP_CREATE;                    break;
        }
        int err = 0;
        zf_ = zip_open(p.string().c_str(), flags, &err);
        if (!zf_) {
            zip_error_t ze;
            zip_error_init_with_code(&ze, err);
            std::string msg = "zip_open(" + p.string() + "): " + zip_error_strerror(&ze);
            zip_error_fini(&ze);
            throw Exception(msg);
        }
        mode_ = mode;
    }

    void close() noexcept {
        if (!zf_) return;
        if (mode_ == Mode::Read) zip_discard(zf_);
        else                     zip_close(zf_);
        zf_ = nullptr;
    }

    bool is_open() const noexcept { return zf_ != nullptr; }

    // ---- 查询 ------------------------------------------------------------

    std::int64_t num_entries() const {
        check_open();
        return zip_get_num_entries(zf_, 0);
    }

    template <class Fn>
    void for_each(Fn&& fn) const {
        check_open();
        const std::int64_t n = zip_get_num_entries(zf_, 0);
        for (std::int64_t i = 0; i < n; ++i) {
            fn(query_info(static_cast<zip_uint64_t>(i)));
        }
    }

    EntryInfo query_info(std::string_view name) const {
        check_open();
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat(zf_, std::string(name).c_str(), 0, &st) != 0) {
            throw_last_error("zip_stat");
        }
        return make_info(st);
    }

    EntryInfo query_info(zip_uint64_t index) const {
        check_open();
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(zf_, index, 0, &st) != 0) {
            throw_last_error("zip_stat_index");
        }
        return make_info(st);
    }

    bool contains(std::string_view name) const {
        check_open();
        return zip_name_locate(zf_, std::string(name).c_str(), 0) >= 0;
    }

    // ---- 读取 ------------------------------------------------------------

    std::vector<std::byte> read(std::string_view name) const {
        check_open();
        const std::string n(name);

        auto info = query_info(name);
        std::vector<std::byte> out(static_cast<std::size_t>(info.size_uncompressed));

        zip_file_t* f = zip_fopen(zf_, n.c_str(), 0);
        if (!f) throw_last_error("zip_fopen");

        std::size_t total = 0;
        while (total < out.size()) {
            zip_int64_t got = zip_fread(f, out.data() + total, out.size() - total);
            if (got < 0) {
                zip_fclose(f);
                throw_last_error("zip_fread");
            }
            if (got == 0) break;
            total += static_cast<std::size_t>(got);
        }
        zip_fclose(f);
        out.resize(total);
        return out;
    }

    std::string read_to_string(std::string_view name) const {
        auto bytes = read(name);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    void extract(std::string_view name, const std::filesystem::path& dest) const {
        check_open();
        const std::string n(name);

        zip_file_t* f = zip_fopen(zf_, n.c_str(), 0);
        if (!f) throw_last_error("zip_fopen");

        auto parent = dest.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);

        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) {
            zip_fclose(f);
            throw Exception("cannot open for write: " + dest.string());
        }

        std::vector<char> buf(64 * 1024);
        for (;;) {
            zip_int64_t got = zip_fread(f, buf.data(), buf.size());
            if (got < 0) {
                zip_fclose(f);
                throw_last_error("zip_fread");
            }
            if (got == 0) break;
            out.write(buf.data(), static_cast<std::streamsize>(got));
        }
        zip_fclose(f);
        if (!out) throw Exception("write failed: " + dest.string());
    }

    void extract_all(const std::filesystem::path& dest_dir) const {
        check_open();
        std::filesystem::create_directories(dest_dir);
        const auto root = std::filesystem::absolute(dest_dir).lexically_normal();
        const std::int64_t n = zip_get_num_entries(zf_, 0);
        for (std::int64_t i = 0; i < n; ++i) {
            auto info = query_info(static_cast<zip_uint64_t>(i));

            // 防止 zip slip：条目必须落在 dest_dir 内。
            auto out = (root / info.name).lexically_normal();
            auto rel = out.lexically_relative(root);
            bool unsafe = rel.empty() || rel.is_absolute() || out == root;
            for (const auto& part : rel) {
                if (part == "..") {
                    unsafe = true;
                    break;
                }
            }
            if (unsafe) {
                throw Exception("unsafe path in zip: " + info.name);
            }

            if (info.is_directory) std::filesystem::create_directories(out);
            else                   extract(info.name, out);
        }
    }

    // ---- 写入 ------------------------------------------------------------

    void add_file(std::string_view name,
                  const void* data, std::size_t size,
                  zip_int32_t compression_method = ZIP_CM_DEFLATE,
                  int compression_level = -1)
    {
        check_writable();
        // 拷贝一份交给 libzip 管理（freep=1），避免调用方生命周期问题
        void* buf = std::malloc(size ? size : 1);
        if (!buf) throw Exception("malloc failed");
        if (size) std::memcpy(buf, data, size);

        zip_source_t* s = zip_source_buffer(zf_, buf, size, 1);
        if (!s) {
            std::free(buf);
            throw_last_error("zip_source_buffer");
        }
        add_via_source(name, s, compression_method, compression_level);
    }

    void add_file(std::string_view name, std::string_view data,
                  zip_int32_t compression_method = ZIP_CM_DEFLATE,
                  int compression_level = -1)
    {
        add_file(name, data.data(), data.size(), compression_method, compression_level);
    }

    void add_file_from_path(const std::filesystem::path& src,
                            std::string_view name_in_zip,
                            zip_int32_t compression_method = ZIP_CM_DEFLATE,
                            int compression_level = -1)
    {
        check_writable();
        zip_source_t* s = zip_source_file(zf_, src.string().c_str(), 0, -1);
        if (!s) throw_last_error("zip_source_file");
        add_via_source(name_in_zip, s, compression_method, compression_level);
    }

    void add_directory(const std::filesystem::path& src_dir,
                       std::string_view root_name = "")
    {
        check_writable();
        if (!std::filesystem::is_directory(src_dir)) {
            throw Exception("not a directory: " + src_dir.string());
        }
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(src_dir))
        {
            auto rel = std::filesystem::relative(entry.path(), src_dir);
            std::string zip_name;
            if (!root_name.empty()) {
                zip_name.append(root_name);
                zip_name.push_back('/');
            }
            zip_name += rel.generic_string();

            if (entry.is_directory()) {
                add_empty_directory(zip_name);
            } else if (entry.is_regular_file()) {
                add_file_from_path(entry.path(), zip_name);
            }
        }
    }

    void add_empty_directory(std::string_view name) {
        check_writable();
        std::string n(name);
        if (n.empty() || n.back() != '/') n.push_back('/');

        zip_source_t* s = zip_source_buffer(zf_, nullptr, 0, 0);
        if (!s) throw_last_error("zip_source_buffer");
        add_via_source(n, s, ZIP_CM_STORE, 0);
    }

private:
    zip_t* zf_   = nullptr;
    Mode   mode_ = Mode::Read;

    void check_open() const {
        if (!zf_) throw Exception("archive is not open");
    }
    void check_writable() const {
        check_open();
        if (mode_ == Mode::Read) throw Exception("archive opened read-only");
    }

    void throw_last_error(const char* action) const {
        std::string msg(action ? action : "libzip error");
        if (zip_error_t* ze = zip_get_error(zf_)) {
            msg += ": ";
            msg += zip_error_strerror(ze);
        }
        throw Exception(msg);
    }

    static EntryInfo make_info(const zip_stat_t& st) {
        EntryInfo info;
        if (st.name) info.name = st.name;
        info.size_uncompressed = st.size;
        info.size_compressed   = st.comp_size;
        info.mtime             = st.mtime;
        info.is_encrypted      = (st.encryption_method != ZIP_EM_NONE);
        if (!info.name.empty() && info.name.back() == '/') info.is_directory = true;
        return info;
    }

    void add_via_source(std::string_view name, zip_source_t* source,
                        zip_int32_t compression_method, int compression_level)
    {
        zip_int64_t idx = zip_file_add(
            zf_, std::string(name).c_str(), source, ZIP_FL_ENC_UTF_8);
        if (idx < 0) {
            zip_source_free(source);
            throw_last_error("zip_file_add");
        }
        // 对目录设置压缩没有意义，但调用也不会出错
        zip_set_file_compression(
            zf_, static_cast<zip_uint64_t>(idx),
            compression_method, compression_level);
    }
};

// ---- 便捷函数 ------------------------------------------------------------

inline void extract_zip(const std::filesystem::path& zip_path,
                        const std::filesystem::path& dest_dir)
{
    Archive a(zip_path, Archive::Mode::Read);
    a.extract_all(dest_dir);
}

inline void create_zip_from_directory(const std::filesystem::path& zip_path,
                                      const std::filesystem::path& source_dir,
                                      bool include_root = false)
{
    Archive a(zip_path, Archive::Mode::Create);
    std::string root;
    if (include_root) {
        root = source_dir.filename().string();
    }
    a.add_directory(source_dir, root);
}

} // namespace zipxx

#endif // ZIPXX_HPP