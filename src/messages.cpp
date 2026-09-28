#include "messages.h"
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace sextant {
    namespace {
        struct Slot {
            std::mutex m;
            // Shared, so a call in flight keeps the handler it started with
            // while another thread replaces it.
            std::shared_ptr<const MessageHandler> handler;
        };

        // Leaked on purpose, like the window registry: a message may come from
        // a Figure destroyed during static destruction.
        Slot& slot() {
            static Slot* s = new Slot();
            return *s;
        }

        void to_stderr(std::string_view message) {
            const std::string line = "sextant: " + std::string(message) + "\n";
            std::fputs(line.c_str(), stderr);
        }
    } // namespace

    void emit_message(std::string_view message) noexcept {
        std::shared_ptr<const MessageHandler> h;
        {
            std::lock_guard<std::mutex> lock(slot().m);
            h = slot().handler;
        }
        // Called with the lock released: the handler may set another handler,
        // or block for as long as it likes.
        try {
            if (h) (*h)(message);
            else to_stderr(message);
        } catch (...) {
            // A diagnostic never turns a working call into a failing one.
        }
    }

    MessageHandler exchange_message_handler(MessageHandler handler) {
        auto next = handler ? std::make_shared<const MessageHandler>(std::move(handler)) : nullptr;
        std::shared_ptr<const MessageHandler> prev;
        {
            std::lock_guard<std::mutex> lock(slot().m);
            prev = std::exchange(slot().handler, std::move(next));
        }
        return prev ? *prev : MessageHandler{};
    }
} // namespace sextant
