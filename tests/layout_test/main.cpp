// sextant_layout_test entry point; the checks live in the subject .cpp files.
#include "layout_test.h"
#include "gl_poison.h"

#include <cstdio>
#include <cstdlib>

int main() {
    using namespace lt;

    // Unbuffered, so output up to a crash isn't lost.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== sextant_layout_test ===\n\n");

    // SEXTANT_POISON_GL=<seed>: every GL allocation made without data starts as
    // random bytes, so output that changes with the seed read memory never written.
    if (const char* seed = std::getenv("SEXTANT_POISON_GL")) {
        gl_renderer();   // loads GLAD, whose pointers the wrappers replace
        install_gl_poison(static_cast<unsigned>(std::strtoul(seed, nullptr, 10)));
        std::printf("GL allocations poisoned, seed %s\n\n", seed);
    }

    // text_metrics.cpp
    test_text_metrics();
    test_missing_font_fallback();
    test_concurrent_measurement();

    // layout.cpp
    test_font_size_drives_layout();
    test_absent_decorations_cost_nothing();
    test_wide_tick_labels();
    test_zero_tick();
    test_grid_alignment();
    test_margins();
    test_degenerate_sizes();
    test_legend_and_colorbar_carve();
    test_multiple_colorbars();
    test_colorbar_labels();
    test_show_legend_and_new_keys();
    test_png_svg_frame_agreement();
    test_layout_cost();
    test_figure_edit_lane();
    test_public_margins_api();

    // frame_size.cpp
    test_frame_size_round_trip();
    test_frame_size_responds_to_layout();
    test_public_frame_resize();
    test_subplot_spans();

    // placement.cpp
    test_extended_frame();
    test_legend_anchors();
    test_colorbar_anchors();
    test_placement_grid();
    test_frame_size_round_trip_anchors();
    test_placement_rendered();

    // stored_layout.cpp
    test_stored_measure();
    test_stored_inverse();
    test_layout_store();
    test_navigation_edits();
    test_stored_export();

    // grid_ratios.cpp
    test_grid_ratio_layout();
    test_grid_ratio_inverse();
    test_grid_ratio_api();
    test_grid_ratio_journal();
    test_grid_boundary_drag();

    // errorbar2d.cpp
    test_errorbar_api();
    test_errorbar_data();
    test_errorbar_whisker_shape();
    test_errorbar_rendered();
    test_errorbar_hint_and_rows();

    // line2d.cpp
    test_line_loop_segments();
    test_line_loop_rendered();

    // axis_position.cpp
    test_axis_placement();
    test_axis_position_layout();
    test_axis_origin_and_limits();
    test_axis_position_rendered();
    test_axis_placement3d();
    test_axis_position_3d_plan();
    test_axis_origin_3d_limits();

    // contour.cpp / heatmap.cpp
    test_contour_tracing();
    test_contour_planning();
    test_contour_cache();
    test_heatmap_extent();
    test_heatmap_extent_render();
    test_heatmap_limits();

    // color.cpp
    test_color_from_hex();

    // axes3d.cpp / camera3d.cpp
    test_axes3d_projection();
    test_axes3d_box_plan();
    test_axes3d_limits_rescale_not_resize();
    test_axes3d_coexistence();
    test_axes3d_public_api();
    test_axes3d_render();
    test_camera_navigation();
    test_camera_fit_and_pan();
    test_perspective_projection();
    test_perspective_near_clipping();
    test_annotation_invariance();

    // bar3d.cpp / surface3d.cpp / plane2d.cpp -- geometry before rendering
    test_bar3d_clip_matrix();
    test_bar3d_ingest();
    test_bar3d_faces();
    test_bar3d_painter_order();
    test_surface_ingest();
    test_surface_draw_order();
    test_surface_colorbar();
    test_surface_legend_key();
    test_plane2d_ingest();
    test_plane_raster();
    test_plane2d_geometry();
    test_plane2d_decoration_hoist();
    test_axes3d_colorbar_style();

    // scene3d.cpp / scene3d_svg.cpp
    test_scene3d_order();
    test_depth_peel_order();
    test_scene3d_svg_order();
    test_scene3d_svg_wireframe();
    test_export_budget();
    test_png_peel_option();
    test_painter3d();

    // plane2d_render.cpp / surface3d.cpp
    test_surface_render();
    test_plane2d_render();
    test_plane2d_composite();
    test_plane2d_kinds();
    test_plane2d_legend_and_contours();

    // plane2d_panel.cpp / bar3d.cpp / surface3d.cpp -- object addressing
    test_plane2d_edit_journal();
    test_plane2d_data_tables();
    test_plane2d_hints();
    test_bar3d_hints();
    test_bar3d_data_panel();
    test_surface_data_panel();
    test_surface_hints();
    test_scatter3d_ingest();
    test_scatter3d_render();
    test_scatter3d_svg_order();
    test_scatter3d_legend_and_colorbar();
    test_scatter3d_hints();
    test_scatter3d_data_panel();
    test_line3d_ingest();
    test_line3d_render();
    test_line3d_svg_order();
    test_line3d_legend_and_colorbar();
    test_line3d_hints_and_panel();

    // errorbar3d.cpp -- after both kinds that carry one
    test_errorbar3d_api();
    test_errorbar3d_data();
    test_errorbar3d_geometry();
    test_errorbar3d_rendered();
    test_surface_tri_ingest();
    test_surface_tri_render();
    test_surface_tri_svg();
    test_surface_tri_legend_and_colorbar();
    test_surface_tri_hints_and_panel();
    test_delaunay();
    test_plane2d_visibility();
    test_camera_edit_lane();

    // panel.cpp
    test_cosmetic_panel_3d();
    test_cosmetic_groups_3d();
    test_cosmetic_groups_2d();
    test_panel_id_conflicts();
    test_subplot_selection();
    test_data_panel_object_tabs();
    test_plot_style_lane();
    test_panel_axis_position();
    test_panel_axis_position_3d();
    test_panel_dpi_scale();

    // cell_shading.cpp
    test_cell_shading_ramp();
    test_cell_shading_range();
    test_cell_shading_cache();
    test_data_panel_shading();

    // headless_export.cpp
    test_headless_context();
    test_headless_export();
    test_png_dpi();

    // window_broker.cpp
    test_window_broker();
    test_window_broker_pump();

    // window_input.cpp
    test_window_input_queue();
    test_window_input_requests();

    // read_back.cpp
    test_read_back_2d();
    test_read_back_3d();
    test_title_journal();
    test_camera_journal();
    test_set_data_2d();
    test_set_data_3d();
    test_set_data_keep_aligned();
    test_data_op_stamps();
    test_limit_journal();
    test_style_journal();

    // window_wait.cpp -- last, because it opens real windows
    test_wait_closed();
    test_run_until_closed();

    if (std::getenv("SEXTANT_POISON_GL")) {
        const PoisonCounts n = gl_poison_counts();
        std::printf("\npoisoned: %d textures, %d renderbuffers, %d buffers\n",
                    n.textures, n.renderbuffers, n.buffers);
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
