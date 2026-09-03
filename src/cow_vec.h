#pragma once
// CowVec<T> -- a copy-on-write std::vector, holding the bulk data inside every
// plot object (see plot_objects.h). Sharing the buffers turns a snapshot
// build and a panel-edit snapshot copy into a refcount bump instead of a deep
// copy of every point.
//
// THREADING -- the load-bearing invariant. The render thread's isolation from
// concurrent Axes::Impl mutation rests entirely on this class: the ONLY way to
// obtain a mutable reference is mut(), which clones first if the buffer is
// shared. So there is deliberately no non-const operator[], begin(), end() or
// data(). Adding one would let a caller write through a buffer the render
// thread is reading, and the race would be invisible in every test that does
// not happen to drag a window at the right moment.
//
// mut()'s `use_count() != 1` test is sound because every reference to a buffer
// derives from an already-existing shared_ptr: if this thread holds the only
// one, no other thread can manufacture a second.
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace sextant {

template <class T>
class CowVec {
public:
    using value_type     = T;
    using const_iterator = typename std::vector<T>::const_iterator;

    CowVec() : p_(std::make_shared<std::vector<T>>()) {}

    // Implicit on purpose: the plot objects are aggregate-initialized from
    // plain vectors all over Axes (see axes.cpp), and those call sites should
    // not have to know this type exists.
    CowVec(std::vector<T> v) : p_(std::make_shared<std::vector<T>>(std::move(v))) {}

    CowVec& operator=(std::vector<T> v) {
        p_ = std::make_shared<std::vector<T>>(std::move(v));
        return *this;
    }

    // Copy/move share the buffer — that is the entire point.
    CowVec(const CowVec&)            = default;
    CowVec& operator=(const CowVec&) = default;
    CowVec(CowVec&&) noexcept        = default;
    CowVec& operator=(CowVec&&) noexcept = default;

    // ---- Read-only surface (mirrors the std::vector members the plot
    // consumers actually use, so none of them needed changing) -------------
    std::size_t size()  const noexcept { return p_->size(); }
    bool        empty() const noexcept { return p_->empty(); }
    const T&    operator[](std::size_t i) const { return (*p_)[i]; }
    const T*    data()  const noexcept { return p_->data(); }
    const_iterator begin() const noexcept { return p_->begin(); }
    const_iterator end()   const noexcept { return p_->end(); }

    const std::vector<T>& get() const noexcept { return *p_; }
    operator const std::vector<T>&() const noexcept { return *p_; }

    // ---- The one mutable door --------------------------------------------
    // Clones first if anyone else shares this buffer, so writing through the
    // returned reference can never be observed by another holder.
    std::vector<T>& mut() {
        if (p_.use_count() != 1)
            p_ = std::make_shared<std::vector<T>>(*p_);
        return *p_;
    }

private:
    // Never null. Held non-const only so mut() can hand out a mutable ref;
    // every other accessor above is const.
    std::shared_ptr<std::vector<T>> p_;
};

} // namespace sextant
