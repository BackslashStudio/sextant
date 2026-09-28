#pragma once
#include <sextant/figure.h>
#include <string_view>

namespace sextant {
    // Hand one diagnostic to the handler set by Figure::set_message_handler(),
    // or to stderr as "sextant: <message>". Callable from any thread; the caller
    // must hold no sextant lock (the handler may block, e.g. on Python's GIL,
    // while another thread needs that lock). Never throws: a throwing handler is
    // ignored.
    void emit_message(std::string_view message) noexcept;

    MessageHandler exchange_message_handler(MessageHandler handler);
} // namespace sextant
