// sextant_layout_test -- internal checks not reachable through
// <sextant/sextant.h>. Links sextant_static and includes src/ headers. One
// translation unit per subject; main.cpp calls them in order. This header
// holds the check counter and builders shared by more than one subject.
#pragma once

// Declarations only: nanovg.c already compiles stb_image's implementation.
#include "stb_image.h"

#include "edit_box.h"
#include "figure_edits.h"
#include "figure_export.h"
#include "renderer/data_renderer.h"
#include "renderer/gl_context.h"
#include "renderer/nvg_renderer.h"
#include "font_discovery.h"
#include "renderer/figure_layout.h"
#include "renderer/box3d.h"
#include "renderer/bar3d.h"
#include "renderer/plane2d.h"
#include "renderer/surface.h"
#include "renderer/surface_tri.h"
#include "renderer/painter3d.h"
#include "colormaps.h"
#include "coord_transform3d.h"
#include "text_metrics.h"
#include "contour.h"
#include "hint.h"
#include "widgets/cell_shading.h"
#include "widgets/data_panel.h"
#include "widgets/panel.h"
#include "widgets/imgui_context.h"
#include <imgui_internal.h>   // ImGuiWindow, TreeNodeSetOpen, the id-conflict detector
#include "widgets/panel_state.h"

#include <sextant/sextant.h>

#include "nanovg.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lt {
    // ---------------------------------------------------------------------------
    // The tally, shared by every .cpp and reported by main.cpp.
    // ---------------------------------------------------------------------------
    extern int g_checks;
    extern int g_failures;

    void check(bool ok, const std::string& what);

    // ---------------------------------------------------------------------------
    // Builders shared by more than one subject file.
    // ---------------------------------------------------------------------------

    // A one-line figure built directly (not via the public API), so tests can set
    // what layout keys on: font sizes, titles, tick overrides.
    sextant::FigureSnapshot make_snapshot(int rows, int cols, int n_cells,
                                          double x_scale = 1.0, double y_scale = 1.0);

    // One 2D axes holding one line, in the public snapshot shape (for panel tests).
    sextant::FigureSnapshot one_line_snapshot(const std::vector<double>& x,
                                              const std::vector<double>& y);

    // Reads the first axes-background rect (the frame) out of an SVG.
    bool read_first_svg_frame(const std::string& path, sextant::PlotRect& out);

    // Index extent by default ([0,cols] x [0,rows], as imshow()); `xr`/`yr`
    // override it.
    sextant::HeatmapPlot make_heatmap(int rows, int cols,
                                      const std::function<float(int, int)>& f,
                                      sextant::HeatmapOptions opts,
                                      std::optional<sextant::Range> xr = std::nullopt,
                                      std::optional<sextant::Range> yr = std::nullopt);

    // One empty 3D axes in a grid, with the camera the caller wants.
    sextant::FigureSnapshot make_snapshot3d(int rows, int cols, int index,
                                            sextant::Camera3D cam = {});

    bool near_px(float a, float b, float tol = 1e-3f);

    // ---------------------------------------------------------------------------
    // Comparing rendered pictures.
    // ---------------------------------------------------------------------------

    // GL_RENDERER of a hidden context, read once.
    const std::string& gl_renderer();

    // False on Apple's software renderer (the macOS CI runner), which does not
    // repeat an export exactly (v1.0 step 21.1): a simple scene had 2 pixels off
    // by one level between identical exports, a depth-peeled one hundreds.
    bool renderer_repeats_exactly();

    // Pixels differing between two PNG files and the largest channel delta
    // among them; px is -1 if either is unreadable or the sizes differ.
    struct PixelDiff {
        int px = -1, worst = 0, w = 0, h = 0;
    };
    PixelDiff png_pixel_diff(const std::string& a_png, const std::string& b_png);

    // Two PNG files show the same picture: byte-identical, or, where the
    // renderer does not repeat itself, at most 0.1% of pixels off by at most 8
    // levels -- a real change moves far more, or by far more.
    bool same_picture(const std::string& a_png, const std::string& b_png);

    // A plane carrying one heatmap, built directly.
    sextant::PlaneSnapshot make_plane(sextant::PlaneOrientation o, double offset,
                                      std::vector<float> data, int rows, int cols,
                                      sextant::Range xr, sextant::Range yr,
                                      sextant::HeatmapOptions ho = {});

    // Two planes, each with one line and one heatmap.
    sextant::RenderSnapshot3D two_plane_snapshot();

    // A grid of bars with distinct heights, u and v (bar3d hit-test and edit
    // checks).
    sextant::Bar3DPlot bar3d_grid();

    // A 3x3 sheet with a label per sample -- the surface counterpart of bar3d_grid().
    sextant::SurfacePlot ripple_surface();

    // ---------------------------------------------------------------------------
    // The suite, in main.cpp's order; one group per source file.
    // ---------------------------------------------------------------------------

    // text_metrics.cpp
    void test_text_metrics();

    void test_missing_font_fallback();

    void test_concurrent_measurement();

    // layout.cpp
    void test_font_size_drives_layout();

    void test_absent_decorations_cost_nothing();

    void test_wide_tick_labels();

    void test_zero_tick();

    void test_grid_alignment();

    void test_margins();

    void test_degenerate_sizes();

    void test_legend_and_colorbar_carve();

    void test_multiple_colorbars();

    void test_colorbar_labels();

    void test_show_legend_and_new_keys();

    void test_png_svg_frame_agreement();

    void test_layout_cost();

    void test_figure_edit_lane();

    void test_public_margins_api();

    // frame_size.cpp
    void test_frame_size_round_trip();

    void test_frame_size_responds_to_layout();

    void test_public_frame_resize();

    void test_subplot_spans();

    // placement.cpp
    void test_extended_frame();

    void test_legend_anchors();

    void test_colorbar_anchors();

    void test_placement_grid();

    void test_frame_size_round_trip_anchors();

    void test_placement_rendered();

    // stored_layout.cpp
    void test_stored_measure();

    void test_stored_inverse();

    void test_layout_store();

    void test_navigation_edits();

    void test_stored_export();

    // grid_ratios.cpp
    void test_grid_ratio_layout();

    void test_grid_ratio_inverse();

    void test_grid_ratio_api();

    void test_grid_ratio_journal();

    void test_grid_boundary_drag();

    // errorbar2d.cpp
    void test_errorbar_api();

    void test_errorbar_data();

    void test_errorbar_whisker_shape();

    void test_errorbar_rendered();

    void test_errorbar_hint_and_rows();

    // line2d.cpp
    void test_line_loop_segments();

    void test_line_loop_rendered();

    // axis_position.cpp
    void test_axis_placement();

    void test_axis_position_layout();

    void test_axis_origin_and_limits();

    void test_axis_position_rendered();

    void test_axis_placement3d();

    void test_axis_position_3d_plan();

    void test_axis_origin_3d_limits();

    // errorbar3d.cpp
    void test_errorbar3d_api();

    void test_errorbar3d_data();

    void test_errorbar3d_geometry();

    void test_errorbar3d_rendered();

    // contour.cpp
    void test_contour_tracing();

    void test_contour_planning();

    void test_contour_cache();

    // heatmap.cpp
    void test_heatmap_extent();

    void test_heatmap_extent_render();

    // axes3d.cpp
    void test_axes3d_projection();

    void test_axes3d_box_plan();

    void test_axes3d_limits_rescale_not_resize();

    void test_axes3d_coexistence();

    void test_axes3d_public_api();

    void test_annotation_invariance();

    void test_axes3d_render();

    // camera3d.cpp
    void test_camera_navigation();

    void test_camera_fit_and_pan();

    void test_perspective_projection();

    void test_perspective_near_clipping();

    void test_camera_edit_lane();

    // bar3d.cpp
    void test_bar3d_clip_matrix();

    void test_bar3d_ingest();

    void test_bar3d_faces();

    void test_bar3d_painter_order();

    void test_bar3d_hints();

    void test_bar3d_data_panel();

    // surface3d.cpp
    void test_surface_ingest();

    void test_surface_draw_order();

    void test_surface_colorbar();

    void test_surface_legend_key();

    void test_surface_render();

    void test_surface_data_panel();

    void test_surface_hints();

    // line3d.cpp
    void test_line3d_ingest();

    void test_line3d_render();

    void test_line3d_svg_order();

    void test_line3d_legend_and_colorbar();

    void test_line3d_hints_and_panel();

    // surface_tri.cpp
    void test_surface_tri_ingest();

    void test_surface_tri_render();

    void test_surface_tri_svg();

    void test_surface_tri_legend_and_colorbar();

    void test_surface_tri_hints_and_panel();

    void test_delaunay();

    // scatter3d.cpp
    void test_scatter3d_ingest();

    void test_scatter3d_render();

    void test_scatter3d_svg_order();

    void test_scatter3d_legend_and_colorbar();

    void test_scatter3d_hints();

    void test_scatter3d_data_panel();

    // plane2d.cpp
    void test_plane2d_ingest();

    void test_plane_raster();

    void test_plane2d_geometry();

    void test_plane2d_decoration_hoist();

    void test_axes3d_colorbar_style();

    // plane2d_render.cpp
    void test_plane2d_render();

    void test_plane2d_composite();

    void test_plane2d_kinds();

    void test_plane2d_legend_and_contours();

    // plane2d_panel.cpp
    void test_plane2d_edit_journal();

    void test_plane2d_data_tables();

    void test_plane2d_hints();

    void test_plane2d_visibility();

    // scene3d.cpp
    void test_scene3d_order();

    void test_depth_peel_order();

    void test_png_peel_option();

    // scene3d_svg.cpp
    void test_scene3d_svg_order();

    void test_scene3d_svg_wireframe();

    void test_export_budget();

    void test_painter3d();

    // cell_shading.cpp
    void test_cell_shading_ramp();

    void test_cell_shading_range();

    void test_cell_shading_cache();

    void test_data_panel_shading();

    // panel.cpp
    void test_cosmetic_panel_3d();

    void test_cosmetic_groups_3d();

    void test_cosmetic_groups_2d();

    void test_panel_id_conflicts();

    void test_subplot_selection();

    void test_data_panel_object_tabs();

    void test_plot_style_lane();

    void test_panel_axis_position();

    void test_panel_axis_position_3d();

    void test_panel_dpi_scale();

    // headless_export.cpp
    void test_headless_context();

    void test_headless_export();

    // PNG dpi: output pixels scale, the layout does not.
    void test_png_dpi();

    // window_broker.cpp
    void test_window_broker();

    void test_window_broker_pump();

    // window_input.cpp
    void test_window_input_queue();

    void test_window_input_requests();

    // read_back.cpp
    void test_read_back_2d();

    void test_read_back_3d();

    void test_title_journal();

    void test_camera_journal();

    void test_set_data_2d();

    void test_set_data_3d();

    void test_set_data_keep_aligned();

    void test_data_op_stamps();

    void test_limit_journal();

    void test_style_journal();

    // window_wait.cpp -- last: it opens real windows
    void test_wait_closed();

    void test_run_until_closed();
} // namespace lt
