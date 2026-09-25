#pragma once
// Internal header — defines Plane2D::Impl. A plane holds an Axes::Impl, so the
// 2D ingest, snapshot, Data panel and edit journal code is reused unchanged.
#include "sextant/axes3d.h"
#include "axes_impl.h"
#include "plot_objects.h"

namespace sextant {
    struct Plane2D::Impl {
        // Plot objects in the parent's data coordinates. The sheet's own limits
        // are never read; the parent box's are.
        Axes::Impl sheet;

        PlaneOrientation orient = PlaneOrientation::XY;
        double offset = 0.0;
        Plane2DOptions opts;
        unsigned long long placement_stamp = 0;   // see PlaneSnapshot

        PlaneSnapshot build_snapshot() const {
            PlaneSnapshot p;
            p.orient = orient;
            p.offset = offset;
            p.opts = opts;
            p.placement_stamp = placement_stamp;
            p.sheet = sheet.build_snapshot();
            return p;
        }
    };
} // namespace sextant
