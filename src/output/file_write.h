#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace sextant {
    // Write `bytes` to `path` verbatim (binary mode: no newline translation), so a
    // file is byte-identical to the in-memory output it was made from. `who`
    // prefixes the error message. Throws std::system_error on failure.
    void write_file(std::string_view path, std::span<const std::uint8_t> bytes,
                    std::string_view who);

    void write_file(std::string_view path, std::string_view text, std::string_view who);
} // namespace sextant
