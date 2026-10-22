#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "../quiver_geometry.h"
#include <cstddef>
#include <vector>

namespace sextant {
    // Axes3D::quiver3d() in the box, shared by the GL path, the SVG plan, hover
    // and the tests. An arrow is arrow_outline()'s shape laid flat in the plane
    // that holds the arrow and faces the eye at its midpoint (as the error-bar
    // whiskers), its pixel sizes converted to box lengths once at the box
    // centre, so under perspective a far arrow is thinner. Rebuilt per view.

    // One arrow in box space: arrow_outline()'s ring (7 points, or 3 for a head
    // alone), with its colour (alpha resolved, depth shade not yet applied).
    struct Quiver3DArrow {
        Vec3 p[7]{};
        int points = 0;
        Color color{};
        std::size_t plot = 0, arrow = 0;
    };

    // An arrow's outline from tail `t` to tip `h` (box space), facing `facing`
    // (unit, toward the eye), `k` box units per pixel; returns the point count,
    // 0 when the arrow has no length or points along `facing`.
    int quiver3d_outline(Vec3 t, Vec3 h, Vec3 facing, double k, const Quiver3DOptions& o, Vec3 (&out)[7]);

    // Every arrow drawn of one field (for_each_quiver3d_arrow()), in arrow order.
    void quiver3d_arrows(const Projector3D& proj, const Quiver3DPlot& q, std::size_t plot,
                         std::vector<Quiver3DArrow>& out);

    // The field's bounding-box centre distance, comparable with the other kinds'.
    double quiver3d_plot_distance(const Quiver3DPlot& q, const Projector3D& proj);

    // One convex piece of a projected arrow for the SVG path (the painter
    // orders convex polygons): the head, and the shaft unless the arrow is all
    // head. An opaque arrow's shaft reaches half a pixel into its head, so no
    // antialiased seam shows between them (a translucent one's would blend
    // twice there). Each piece's colour is shaded at its arrow's centroid depth.
    struct Quiver3DPolygon {
        std::vector<float> xy;  // pixels
        std::vector<Vec3>  box; // the same ring, in box space
        Color color{};          // finished: alpha resolved, depth shade applied
        float depth = 0.0f;     // Px3::depth at the arrow's centroid
        std::size_t plot = 0, arrow = 0;
        bool head = false;
    };

    // All arrows of all shown fields as pieces, far to near by depth (an
    // arrow's shaft before its head); arrows partly behind a perspective eye
    // are dropped.
    std::vector<Quiver3DPolygon> plan_quivers3d(const Projector3D& proj, const std::vector<Quiver3DPlot>& quivers);
} // namespace sextant
