#include "event_channel.h"
#include "messages.h"
#include "window_registry.h"
#include <algorithm>
#include <exception>
#include <string>

namespace sextant {
namespace {
    // Every channel's queued events, so any_events_pending() is one read.
    std::atomic<std::size_t> g_total_pending{0};

    struct Hub {
        std::mutex m;
        std::vector<std::weak_ptr<EventChannel>> channels;
    };

    // Never destroyed, for the reason the window registry is not: a Figure held
    // in a static outlives ordinary static destruction.
    Hub& hub() {
        static Hub* h = new Hub();
        return *h;
    }

    // Merges rather than queues.
    bool coalescible(EventKind k) {
        return k == EventKind::MouseMove || k == EventKind::Scroll || k == EventKind::Resize;
    }

    // Rounds of "take the queue, deliver it" a single dispatch() makes, so a
    // callback that provokes events (or a window producing them faster than a
    // slow callback) cannot hold the caller in a drain for ever.
    constexpr int kMaxRounds = 4;

    struct DispatchGuard {
        std::atomic<bool>& flag;
        bool owned;

        explicit DispatchGuard(std::atomic<bool>& f) : flag(f), owned(!f.exchange(true)) {}

        ~DispatchGuard() { if (owned) flag.store(false); }
    };
} // namespace

std::shared_ptr<EventChannel> EventChannel::create() {
    std::shared_ptr<EventChannel> ch(new EventChannel());
    Hub& h = hub();
    std::lock_guard<std::mutex> lock(h.m);
    h.channels.erase(std::remove_if(h.channels.begin(), h.channels.end(),
                                    [](const std::weak_ptr<EventChannel>& w) { return w.expired(); }),
                     h.channels.end());
    h.channels.push_back(ch);
    return ch;
}

void EventChannel::recompute_mask_locked() {
    std::uint32_t mask = 0;
    for (const auto& h : handlers_)
        if (h->active.load()) mask |= 1u << static_cast<int>(h->kind);
    mask_.store(mask, std::memory_order_relaxed);
}

int EventChannel::connect(EventKind kind, EventCallback fn) {
    auto h = std::make_shared<Handler>();
    h->kind = kind;
    h->fn = std::move(fn);
    std::lock_guard<std::mutex> lock(m_);
    h->id = next_id_++;
    handlers_.push_back(h);
    recompute_mask_locked();
    return h->id;
}

void EventChannel::disconnect(int id) {
    std::lock_guard<std::mutex> lock(m_);
    const auto it = std::find_if(handlers_.begin(), handlers_.end(),
                                 [id](const std::shared_ptr<Handler>& h) { return h->id == id; });
    if (it == handlers_.end()) return;
    (*it)->active.store(false);
    handlers_.erase(it);
    recompute_mask_locked();
}

void EventChannel::push(Event e) {
    if (!wants(e.kind)) return;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (coalescible(e.kind) && !queue_.empty() && queue_.back().kind == e.kind) {
            Event& last = queue_.back();
            if (e.kind == EventKind::Scroll) {
                e.scroll_x += last.scroll_x;
                e.scroll_y += last.scroll_y;
            }
            last = std::move(e);
            return; // merged: nothing new to wake anybody for
        }

        if (queue_.size() >= kSoftCapacity) {
            // Make room by discarding the oldest thing that can be re-derived.
            const auto it = std::find_if(queue_.begin(), queue_.end(),
                                         [](const Event& q) { return coalescible(q.kind); });
            if (it != queue_.end()) {
                queue_.erase(it);
                dropped_.fetch_add(1, std::memory_order_relaxed);
                pending_.fetch_sub(1, std::memory_order_relaxed);
                g_total_pending.fetch_sub(1, std::memory_order_relaxed);
            } else if (coalescible(e.kind) || queue_.size() >= kHardCapacity) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        queue_.push_back(std::move(e));
        pending_.fetch_add(1, std::memory_order_relaxed);
        g_total_pending.fetch_add(1, std::memory_order_relaxed);
    }
    wake_waiters();
}

int EventChannel::dispatch() {
    DispatchGuard guard(dispatching_);
    if (!guard.owned) return 0;

    int taken = 0;
    for (int round = 0; round < kMaxRounds; ++round) {
        std::deque<Event> batch;
        {
            std::lock_guard<std::mutex> lock(m_);
            batch.swap(queue_);
            pending_.fetch_sub(batch.size(), std::memory_order_relaxed);
            g_total_pending.fetch_sub(batch.size(), std::memory_order_relaxed);
        }
        if (batch.empty()) break;
        taken += static_cast<int>(batch.size());

        for (const Event& e : batch) {
            // The handlers for this kind, copied so that connect()/disconnect()
            // from a callback do not disturb the walk. A handler disconnected
            // meanwhile is skipped by its own flag.
            std::vector<std::shared_ptr<Handler>> hs;
            {
                std::lock_guard<std::mutex> lock(m_);
                for (const auto& h : handlers_)
                    if (h->kind == e.kind) hs.push_back(h);
            }
            for (const auto& h : hs) {
                if (!h->active.load()) continue;
                try {
                    h->fn(e);
                } catch (const std::exception& ex) {
                    emit_message(std::string("an event callback threw: ") + ex.what());
                } catch (...) {
                    emit_message("an event callback threw an unknown exception");
                }
            }
        }
    }
    return taken;
}

int dispatch_all_events() {
    std::vector<std::shared_ptr<EventChannel>> live;
    {
        Hub& h = hub();
        std::lock_guard<std::mutex> lock(h.m);
        for (auto it = h.channels.begin(); it != h.channels.end();) {
            if (auto sp = it->lock()) { live.push_back(std::move(sp)); ++it; }
            else it = h.channels.erase(it);
        }
    }
    int total = 0;
    for (const auto& ch : live) total += ch->dispatch();
    return total;
}

bool any_events_pending() {
    return g_total_pending.load(std::memory_order_relaxed) > 0;
}
} // namespace sextant
