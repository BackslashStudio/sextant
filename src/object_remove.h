#pragma once
// The public removal calls (Axes::remove, Axes3D::remove, Plane2D::remove,
// Axes3D::remove_plane) share one body: take the object out through the
// holder's internal remove, drop what it returns (nothing undoes a public
// removal), and warn instead of throwing for an id that is not there.
#include "messages.h"
#include "plot_objects.h"
#include <string>

namespace sextant {
    namespace object_remove {
        inline void warn_missing(const char* who, const char* what, ObjectId id) {
            emit_message(std::string(who) + ": no " + what + " with id " + std::to_string(id));
        }

        // `H` is Axes::Impl, Axes3D::Impl or a plane's sheet.
        template <class H>
        bool remove(H& holder, ObjectId id, const char* who) {
            if (holder.remove_object(id)) return true;
            warn_missing(who, "object", id);
            return false;
        }
    } // namespace object_remove
} // namespace sextant
