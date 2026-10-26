#pragma once
// Internal header — defines PolarAxes::Impl. A polar axes holds an Axes::Impl
// for its objects, title, legend and look, so the 2D ingest, Data panel and
// edit journal code is reused; its view and style are its own.
#include "sextant/polar_axes.h"
#include "axes_impl.h"
#include "plot_objects.h"

namespace sextant {
    struct PolarAxes::Impl {
        // Plot objects in (θ, r) data. The sheet's limits, scales, ticks and
        // aspect are never read; view and style are.
        Axes::Impl sheet;

        PolarView view;
        PolarStyle style;
        PolarStamps stamps;
        ObjectId id = next_object_id();   // see ObjectId

        Impl() { sheet.grid_enabled = true; }

        RenderSnapshotPolar build_snapshot() const {
            RenderSnapshotPolar p;
            p.objects = sheet.build_snapshot();
            p.view = view;
            p.style = style;
            p.stamps = stamps;
            p.id = id;
            return p;
        }
    };
} // namespace sextant
