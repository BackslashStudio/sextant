#pragma once
// Style files from a published snapshot: what the window's File menu uses, on
// the render thread, where the live figure is off limits. Figure::style_yaml()
// and apply_style_yaml() (the caller thread's) share the walk; see
// style_file.cpp. Internal, not installed.
#include "figure_edits.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {
    struct FigureSnapshot;

    // The figure's style (`state` false) or style and state as YAML. `cell`: just
    // that cell's axes (no grid or figure section), else the whole figure.
    std::string snapshot_style_text(const FigureSnapshot& snap, bool state,
                                    std::optional<int> cell = std::nullopt);

    struct StyleRead {
        FigureEdits edits;                // to push through FigureEditBox
        std::vector<std::string> issues;  // what was skipped, one line each
    };

    // The edits a style file makes over `snap`. `target`: apply the file's entry
    // for that cell (or its only entry) to that cell, and ignore the rest.
    // Throws std::invalid_argument for bad YAML, a file that is no style file,
    // or a newer format; everything else is an issue.
    StyleRead snapshot_style_edits(std::string_view text, const FigureSnapshot& snap,
                                   std::optional<int> target = std::nullopt);

    // The cells a dialog can offer, with the kind of axes in each.
    struct StyleCell {
        int index = 0;
        const char* kind = "2D";
    };
    std::vector<StyleCell> snapshot_style_cells(const FigureSnapshot& snap);
}
