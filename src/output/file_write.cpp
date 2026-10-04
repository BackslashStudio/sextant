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

        // Windows reads "name:stream" as an NTFS alternate data stream: fopen()
        // succeeds, leaving an empty "name" and the bytes where nobody looks.
        // Only a drive letter's colon (after any \\?\ or \\.\ prefix) is a path.
        bool names_a_stream(std::string_view p) {
#if defined(_WIN32)
            if (p.size() >= 4 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/') &&
                (p[2] == '?' || p[2] == '.') && (p[3] == '\\' || p[3] == '/'))
                p.remove_prefix(4);
            if (p.size() >= 2 && p[1] == ':' &&
                ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')))
                p.remove_prefix(2);
            return p.find(':') != std::string_view::npos;
#else
            (void)p;
            return false;
#endif
        }
    } // namespace

    void write_file(std::string_view path, std::span<const std::uint8_t> bytes,
                    std::string_view who) {
        const std::string p(path);
        if (names_a_stream(p)) fail(EINVAL, who, p, "':' is not allowed in a Windows file name:");
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
