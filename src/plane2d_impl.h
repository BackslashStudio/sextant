#pragma once
// Internal header — defines Plane2D::Impl.
//
// The whole of the arrangement spec_3d.md §6 decided is the first member: a
// plane *holds* an Axes::Impl rather than being one. That is what makes the
// 2D kinds on a plane affordable -- ingest, validation, CowVec sharing and
// build_snapshot() are the 2D code unchanged, and so are the Data panel's
// table enumeration and the edit journal when they reach a plane (step 6).
// What the separate public class buys is the API staying honest: an Axes
// exposes set_xlim, set_xticks, legend() and set_title(), none of which mean
// anything on an object whose coordinates and annotation belong to its parent.
#include "sextant/axes3d.h"
#include "axes_impl.h"
#include "plot_objects.h"

namespace sextant {

struct Plane2D::Impl {
    // Everything drawn on the plane, in the *parent's* data coordinates along
    // the two axes `orient` spans. There are no limits here to resolve: the
    // sheet's own xmin/xmax/ymin/ymax are never read, because the parent box's
    // are what the plane is measured against.
    Axes::Impl sheet;

    PlaneOrientation orient = PlaneOrientation::XY;
    double           offset = 0.0;
    Plane2DOptions   opts;

    PlaneSnapshot build_snapshot() const {
        PlaneSnapshot p;
        p.orient = orient;
        p.offset = offset;
        p.opts   = opts;
        p.sheet  = sheet.build_snapshot();
        return p;
    }
};

} // namespace sextant
