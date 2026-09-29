#pragma once
#include "sextant/events.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace sextant {
// One figure's events: the callbacks connected to it and the queue between the
// window thread, which only pushes, and whichever thread drains. User code never
// runs on the window thread, so a callback may block, take Python's GIL or call
// Figure::close() without deadlocking against the thread that close() joins.
//
// Owned by Figure::Impl through a shared_ptr and registered with a process-wide
// list (dispatch_all_events()), so the static Figure::poll_events()/run() reach
// every figure's queue. The queue outlives the window: a Close event is queued
// as the window goes and is delivered at the next drain.
class EventChannel {
public:
    static std::shared_ptr<EventChannel> create();

    EventChannel(const EventChannel&) = delete;
    EventChannel& operator=(const EventChannel&) = delete;

    // Any thread. The id is never reused.
    int connect(EventKind kind, EventCallback fn);

    // Any thread, including from inside a callback: the callback is not called
    // again, even for events of the batch being delivered. Unknown id: no-op.
    void disconnect(int id);

    // Whether any callback wants this kind. The window thread asks before it
    // builds an event, so an unconnected kind costs one atomic read.
    bool wants(EventKind kind) const {
        return (mask_.load(std::memory_order_relaxed) >> static_cast<int>(kind)) & 1u;
    }

    std::uint32_t wanted_mask() const { return mask_.load(std::memory_order_relaxed); }

    // Any thread. Dropped when no callback wants the kind. MouseMove, Scroll
    // and Resize merge into a queued neighbour of the same kind (latest wins;
    // scroll offsets add); when the queue is full one of those is dropped
    // before anything else is, and Close, MouseDown/Up and KeyDown/Up only past
    // a hard bound.
    void push(Event e);

    // Delivers what is queued, on the calling thread, in order, and returns how
    // many events it took. One drain at a time per channel: a call made from
    // inside a callback, or racing another thread's drain, returns 0. No lock is
    // held while a callback runs. A callback's exception goes to the message
    // handler and delivery goes on. Events queued meanwhile follow, up to a few
    // rounds; the rest wait for the next drain.
    int dispatch();

    bool has_pending() const { return pending_.load(std::memory_order_relaxed) > 0; }

    // Events discarded to the bounds since creation.
    std::size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

    static constexpr std::size_t kSoftCapacity = 4096;
    static constexpr std::size_t kHardCapacity = 16384;

private:
    EventChannel() = default;

    struct Handler {
        int id = 0;
        EventKind kind = EventKind::Close;
        EventCallback fn;
        std::atomic<bool> active{true};
    };

    void recompute_mask_locked();

    std::mutex m_;
    std::deque<Event> queue_;
    std::vector<std::shared_ptr<Handler>> handlers_;
    int next_id_ = 1;

    std::atomic<std::uint32_t> mask_{0};
    std::atomic<std::size_t> pending_{0};
    std::atomic<std::size_t> dropped_{0};
    std::atomic<bool> dispatching_{false};
};

// Drains every figure's channel on the calling thread; the total taken.
int dispatch_all_events();

// Whether any channel has something queued.
bool any_events_pending();
} // namespace sextant
