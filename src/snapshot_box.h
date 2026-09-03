#pragma once
#include "plot_objects.h"
#include <memory>
#include <mutex>

namespace sextant {

// Thread-safe latest-value-wins holder for a FigureSnapshot.
// Caller thread calls store(); render thread calls load() once per frame.
// A short-held mutex is intentional: this runs once per frame (~60Hz), not
// in a hot inner loop, so a plain mutex is simpler and more portable across
// MSVC/libstdc++/libc++ than std::atomic<std::shared_ptr<T>>.
class SnapshotBox {
public:
    void store(std::shared_ptr<const FigureSnapshot> s) {
        std::scoped_lock lk(mutex_);
        snapshot_ = std::move(s);
    }

    std::shared_ptr<const FigureSnapshot> load() const {
        std::scoped_lock lk(mutex_);
        return snapshot_;
    }

private:
    mutable std::mutex                    mutex_;
    std::shared_ptr<const FigureSnapshot> snapshot_;
};

} // namespace sextant
