#pragma once
#include "plot_objects.h"
#include <memory>
#include <mutex>

namespace sextant {

// Thread-safe latest-value-wins holder for a FigureSnapshot: the caller thread
// store()s, the render thread load()s once per frame. A plain mutex is enough
// at that rate.
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
