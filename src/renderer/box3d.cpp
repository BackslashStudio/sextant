#include "box3d.h"
#include "../axis_placement.h"
#include "../text_metrics.h"
#include "figure_layout.h"   // kTickLabelGap, kTitleGap
#include <algorithm>
#include <cmath>

namespace sextant {

namespace {

struct Px2 { float x = 0.0f, y = 0.0f; };

Px2 unit(Px2 v, Px2 fallback) {
    const float n = std::sqrt(v.x * v.x + v.y * v.y);
    return n > 1e-4f ? Px2{ v.x / n, v.y / n } : fallback;
}

// Half the extent of `text` measured along `dir` -- how far past its anchor
// the string reaches in the direction it is being pushed. Used so a label
// clears the tick mark by the same visual gap whichever way it sits.
float half_extent_along(const Box3DPlan::Label& l, Px2 dir) {
    const float w = text_width(l.font_path, l.fontsize, l.text);
    const float h = font_vmetrics(l.font_path, l.fontsize).line_height;
    return 0.5f * (std::fabs(dir.x) * w + std::fabs(dir.y) * h);
}

void add_poly(std::vector<Box3DPlan::Poly>& out, std::initializer_list<Px3> pts) {
    Box3DPlan::Poly p;
    p.xy.reserve(pts.size() * 2);
    for (const Px3& q : pts) { p.xy.push_back(q.x); p.xy.push_back(q.y); }
    out.push_back(std::move(p));
}

// The two ways box-space geometry becomes a polyline, both of them clipped at
// the near plane. Nothing here projects an endpoint directly: under a
// perspective camera flown into the box, a segment with one end behind the eye
// projects to a line running the wrong way across the whole figure, and a
// pane with one corner behind it to a shape that is not the pane. Under an
// orthographic camera both are pass-throughs, which is why an ortho picture is
// bit-for-bit what it was before perspective existed.
void add_segment(std::vector<Box3DPlan::Poly>& out, const Projector3D& proj,
                 Vec3 a, Vec3 b) {
    Px3 pa, pb;
    if (!proj.project_segment(a, b, pa, pb)) return;
    add_poly(out, { pa, pb });
}

void add_polygon(std::vector<Box3DPlan::Poly>& out, const Projector3D& proj,
                 const std::vector<Vec3>& pts) {
    std::vector<Px3> clipped;
    proj.project_polygon(pts, clipped);
    if (clipped.size() < 3) return;   // nothing left to fill
    Box3DPlan::Poly p;
    p.xy.reserve(clipped.size() * 2);
    for (const Px3& q : clipped) { p.xy.push_back(q.x); p.xy.push_back(q.y); }
    out.push_back(std::move(p));
}

} // namespace

Box3DPlan plan_box3d(const Projector3D& proj, const RenderSnapshot3D& snap,
                     const std::vector<Tick>& xticks,
                     const std::vector<Tick>& yticks,
                     const std::vector<Tick>& zticks)
{
    Box3DPlan plan;

    const Transform3D& tf = proj.transform();
    const Vec3 h = tf.half_extent();
    const Vec3 e = proj.eye_dir();

    // The back wall on each axis is the face whose outward normal points away
    // from the camera -- so if the eye is on the +x side, the drawn YZ pane is
    // the one at -x. Three of the six, always, for any camera outside the box.
    const double bx = (e.x >= 0.0) ? -h.x : h.x;
    const double by = (e.y >= 0.0) ? -h.y : h.y;
    const double bz = (e.z >= 0.0) ? -h.z : h.z;

    const auto P = [&](double x, double y, double z) { return proj.project_box({ x, y, z }); };
    const auto S = [&](std::vector<Box3DPlan::Poly>& out, Vec3 a, Vec3 b) {
        add_segment(out, proj, a, b);
    };

    // ---- Panes and their grid -------------------------------------------
    if (snap.box_style.panes) {
        add_polygon(plan.panes, proj, { { bx, -h.y, -h.z }, { bx,  h.y, -h.z },
                                        { bx,  h.y,  h.z }, { bx, -h.y,  h.z } });
        add_polygon(plan.panes, proj, { { -h.x, by, -h.z }, {  h.x, by, -h.z },
                                        {  h.x, by,  h.z }, { -h.x, by,  h.z } });
        add_polygon(plan.panes, proj, { { -h.x, -h.y, bz }, {  h.x, -h.y, bz },
                                        {  h.x,  h.y, bz }, { -h.x,  h.y, bz } });
        plan.pane_edges = plan.panes;
    }

    if (snap.grid_enabled && snap.grid_opts.linestyle != LineStyle::None) {
        // Each pane carries the ticks of the two axes it spans. A pane is
        // ruled by the axes lying *in* it, never by the one it is normal to.
        for (const auto& t : yticks) {
            const double v = tf.box_y(t.value);
            S(plan.grid, { bx, v, -h.z }, { bx, v, h.z });
        }
        for (const auto& t : zticks) {
            const double v = tf.box_z(t.value);
            S(plan.grid, { bx, -h.y, v }, { bx, h.y, v });
            S(plan.grid, { -h.x, by, v }, { h.x, by, v });
        }
        for (const auto& t : xticks) {
            const double v = tf.box_x(t.value);
            S(plan.grid, { v, by, -h.z }, { v, by, h.z });
            S(plan.grid, { v, -h.y, bz }, { v, h.y, bz });
        }
        for (const auto& t : yticks) {
            const double v = tf.box_y(t.value);
            S(plan.grid, { -h.x, v, bz }, { h.x, v, bz });
        }
    }

    // ---- The three labelled edges ---------------------------------------
    //
    // Each is a silhouette edge -- shared by one drawn (back-facing) pane and
    // one undrawn (front-facing) one -- so it is always on the box's outline
    // and its labels always fall outside the solid. Which one that is turns
    // with the camera, via bx/by/bz above.
    //
    //   x: along the bottom-front, on the drawn XY pane at the near y side
    //   y: along the bottom-side,  on the drawn XY pane at the near x side
    //   z: the vertical edge of the drawn YZ pane at the near y side
    struct AxisEdge {
        Vec3 a, b;                       // endpoints in box space, as placed
        Vec3 auto_a, auto_b;             // the camera-chosen edge, always
        const std::vector<Tick>* ticks;
        double (Transform3D::*to_box)(double) const;
        int    axis;                     // 0=x, 1=y, 2=z -- which coordinate moves
        const std::string* title;
        float  title_fontsize;
        Color  title_color;
    };

    const auto& st = snap.axes_style;

    // Where each axis line sits along the two coordinates that are *not* its
    // own (v1.0 step 20). Auto keeps the camera's silhouette edge -- the
    // bx/by/bz above -- and anything else is a value in data space, which the
    // transform puts back into box space here so there is one copy of that
    // arithmetic. The six are decided independently, which is what lets
    // `origin_z = 0` alone drop all three axes to z = 0 and leave their other
    // coordinate on the box.
    const auto coord = [&](AxisPosition p, const std::optional<double>& pin,
                           double lo, double hi,
                           double (Transform3D::*to_box)(double) const,
                           double auto_box) {
        const AxisCoord3D c = place_axis3d(p, pin, lo, hi);
        return c.camera ? auto_box : (tf.*to_box)(c.at.pos);
    };
    const double xax_y = coord(st.xaxis_y, st.origin_y, tf.ymin, tf.ymax, &Transform3D::box_y, -by);
    const double xax_z = coord(st.xaxis_z, st.origin_z, tf.zmin, tf.zmax, &Transform3D::box_z,  bz);
    const double yax_x = coord(st.yaxis_x, st.origin_x, tf.xmin, tf.xmax, &Transform3D::box_x, -bx);
    const double yax_z = coord(st.yaxis_z, st.origin_z, tf.zmin, tf.zmax, &Transform3D::box_z,  bz);
    const double zax_x = coord(st.zaxis_x, st.origin_x, tf.xmin, tf.xmax, &Transform3D::box_x,  bx);
    const double zax_y = coord(st.zaxis_y, st.origin_y, tf.ymin, tf.ymax, &Transform3D::box_y, -by);

    const AxisEdge edges[3] = {
        { { -h.x, xax_y, xax_z }, { h.x, xax_y, xax_z },
          { -h.x, -by, bz }, { h.x, -by, bz }, &xticks, &Transform3D::box_x, 0,
          &snap.xtitle, st.xtitle_fontsize, st.xtitle_color },
        { { yax_x, -h.y, yax_z }, { yax_x, h.y, yax_z },
          { -bx, -h.y, bz }, { -bx, h.y, bz }, &yticks, &Transform3D::box_y, 1,
          &snap.ytitle, st.ytitle_fontsize, st.ytitle_color },
        { { zax_x, zax_y, -h.z }, { zax_x, zax_y, h.z },
          { bx, -by, -h.z }, { bx, -by, h.z }, &zticks, &Transform3D::box_z, 2,
          &snap.ztitle, st.ztitle_fontsize, st.ztitle_color },
    };

    const Px3 centre = P(0.0, 0.0, 0.0);

    for (const AxisEdge& ax : edges) {
        // An axis entirely behind the eye has no line, no marks and no
        // numbers -- there is no pixel-space fallback that would be better
        // than drawing nothing, since every direction below is derived from
        // where the edge landed.
        Px3 pa, pb;
        if (!proj.project_segment(ax.a, ax.b, pa, pb)) continue;
        add_poly(plan.axis_lines, { pa, pb });

        // One outward direction per axis, not one per tick: taking it from the
        // edge's midpoint keeps a row of labels parallel to its axis instead
        // of fanning out from the box centre, which is what a per-tick
        // direction would do at the ends of a long edge.
        //
        // The midpoint is the *camera-chosen* edge's, not the placed line's
        // (v1.0 step 20). An axis moved inward would otherwise push its labels
        // along a vector from the box centre that shrinks as it approaches the
        // centre and is zero-length at it; borrowing the silhouette edge's
        // direction is already computed, turns smoothly with the camera and
        // never degenerates. It also anchors the title, which stays on that
        // edge for want of anywhere else: a 3D cell reserves nothing outside
        // its frame, so a title following the line inward would land in the
        // data with no collision story.
        Px3 qa, qb;
        const bool auto_seen = proj.project_segment(ax.auto_a, ax.auto_b, qa, qb);
        const Px2 mid = auto_seen ? Px2{ (qa.x + qb.x) * 0.5f, (qa.y + qb.y) * 0.5f }
                                  : Px2{ (pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f };
        const Px2 dir = unit({ mid.x - centre.x, mid.y - centre.y },
                             ax.axis == 2 ? Px2{ -1.0f, 0.0f } : Px2{ 0.0f, 1.0f });

        // Labels run *along* the projected edge, so that is the direction they
        // collide in. A 3D axis is foreshortened by an amount that depends on
        // the camera -- an edge nearly end-on to the viewer is a few pixels
        // long while carrying the same tick list as a 2D axis two hundred
        // pixels wide -- so which numbers there is room for cannot be decided
        // where the ticks are generated. It is decided here, and only the
        // *labels* are thinned: the marks and the grid still show every
        // subdivision, which is what makes a dropped number readable as
        // spacing rather than as a missing tick.
        const Px2 along = unit({ pb.x - pa.x, pb.y - pa.y }, { 1.0f, 0.0f });
        constexpr float kLabelGap = 4.0f;
        bool  have_kept = false;
        float kept_pos = 0.0f, kept_half = 0.0f;

        float max_label_reach = 0.0f;
        for (const Tick& t : *ax.ticks) {
            const double v = (tf.*ax.to_box)(t.value);
            Vec3 p = ax.a;
            if      (ax.axis == 0) p.x = v;
            else if (ax.axis == 1) p.y = v;
            else                   p.z = v;

            // A tick behind the eye is dropped whole -- mark and number
            // together -- rather than drawn at the garbage pixel the divide
            // gives it. Its own position is the test, not the edge's: a
            // dollied-in camera keeps one end of an axis and loses the other.
            if (!proj.in_front(p)) continue;

            const Px3 tp = proj.project_box(p);
            const int  tick_index = static_cast<int>(plan.tick_marks.size());
            add_poly(plan.tick_marks,
                     { tp, Px3{ tp.x + dir.x * st.tick_length,
                                tp.y + dir.y * st.tick_length, tp.depth, tp.w } });

            Box3DPlan::Label lbl;
            lbl.text      = t.label;
            lbl.fontsize  = st.label_fontsize;
            lbl.color     = st.label_color;
            lbl.font_path = st.font_path;
            lbl.tick      = tick_index;
            const float reach = st.tick_length + kTickLabelGap + half_extent_along(lbl, dir);
            lbl.x = tp.x + dir.x * reach;
            lbl.y = tp.y + dir.y * reach;
            // The far edge, not the anchor: a title cleared only to the
            // label centres lands on top of the widest number.
            max_label_reach = std::max(max_label_reach, reach + half_extent_along(lbl, dir));

            const float pos  = lbl.x * along.x + lbl.y * along.y;
            const float half = half_extent_along(lbl, along);
            // Greedy from one end: the first number always survives, so an
            // axis never loses all of them however short it gets.
            if (have_kept && std::fabs(pos - kept_pos) < half + kept_half + kLabelGap)
                continue;
            have_kept = true;
            kept_pos  = pos;
            kept_half = half;
            plan.tick_labels.push_back(std::move(lbl));
        }

        if (!ax.title->empty()) {
            Box3DPlan::Label lbl;
            lbl.text      = *ax.title;
            lbl.fontsize  = ax.title_fontsize;
            lbl.color     = ax.title_color;
            lbl.font_path = st.font_path;
            // Clear of the longest label on *this* axis, so a title never
            // lands on top of a number however wide the numbers get.
            const float reach = (max_label_reach > 0.0f
                                     ? max_label_reach
                                     : st.tick_length + kTickLabelGap)
                                + kTitleGap + half_extent_along(lbl, dir);
            lbl.x = mid.x + dir.x * reach;
            lbl.y = mid.y + dir.y * reach;
            plan.axis_titles.push_back(std::move(lbl));
        }
    }

    return plan;
}

} // namespace sextant
