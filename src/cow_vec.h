#pragma once
// CowVec<T> -- a copy-on-write std::vector for plot-object bulk data, so
// snapshots share buffers instead of deep-copying.
//
// THREADING: the render thread's isolation from Axes::Impl mutation depends on
// mut() being the only way to get a mutable reference (it clones if shared).
// Do not add non-const operator[], begin(), end() or data().
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace sextant {
    template<class T>
    class CowVec {
    public:
        using value_type = T;
        using const_iterator = typename std::vector<T>::const_iterator;

        CowVec() : p_(std::make_shared<std::vector<T>>()) {
        }

        // Implicit so plot objects can be aggregate-initialized from plain vectors.
        CowVec(std::vector<T> v) : p_(std::make_shared<std::vector<T>>(std::move(v))) {
        }

        CowVec& operator=(std::vector<T> v) {
            p_ = std::make_shared<std::vector<T>>(std::move(v));
            return *this;
        }

        // Copy/move share the buffer.
        CowVec(const CowVec&) = default;

        CowVec& operator=(const CowVec&) = default;

        CowVec(CowVec&&) noexcept = default;

        CowVec& operator=(CowVec&&) noexcept = default;

        // ---- Read-only surface (the std::vector members consumers use) --------
        std::size_t size() const noexcept { return p_->size(); }
        bool empty() const noexcept { return p_->empty(); }
        const T& operator[](std::size_t i) const { return (*p_)[i]; }
        const T* data() const noexcept { return p_->data(); }
        const_iterator begin() const noexcept { return p_->begin(); }
        const_iterator end() const noexcept { return p_->end(); }

        const std::vector<T>& get() const noexcept { return *p_; }
        operator const std::vector<T> &() const noexcept { return *p_; }

        // ---- The one mutable door: clones first if the buffer is shared ------
        std::vector<T>& mut() {
            if (p_.use_count() != 1)
                p_ = std::make_shared<std::vector<T>>(*p_);
            return *p_;
        }

    private:
        // Never null.
        std::shared_ptr<std::vector<T>> p_;
    };
} // namespace sextant
