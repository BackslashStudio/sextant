#include "file_write.h"
#include <cerrno>
#include <cstdio>
#include <string>
#include <system_error>

namespace sextant {
    namespace {
        [[noreturn]] void fail(int err, std::string_view who, const std::string& path,
                               const char* what) {
            // generic_category: the code is an errno. system_category would read
            // it as a Win32 error on Windows and give the wrong message.
            throw std::system_error(err ? err : EIO, std::generic_category(),
                                    std::string(who) + ": " + what + " '" + path + "'");
        }
    } // namespace

    void write_file(std::string_view path, std::span<const std::uint8_t> bytes,
                    std::string_view who) {
        const std::string p(path);
        errno = 0;
        std::FILE* f = std::fopen(p.c_str(), "wb");
        if (!f) fail(errno, who, p, "cannot open");
        const std::size_t n = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), f);
        const int write_err = errno;
        // fclose() flushes; a full disk may only show up here.
        const bool closed = std::fclose(f) == 0;
        if (n != bytes.size()) fail(write_err, who, p, "cannot write");
        if (!closed) fail(errno, who, p, "cannot write");
    }

    void write_file(std::string_view path, std::string_view text, std::string_view who) {
        write_file(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
                   who);
    }
} // namespace sextant
