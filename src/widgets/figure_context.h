#pragma once
// What a GUI-kit component gets to draw one figure (GUI-kit R3): the snapshot,
// the edit channel, and the state shared by every component bound to that
// figure. A component's own state (CosmeticState, DataPanelState, ...) is
// passed beside it, owned by whoever hosts the component.
#include "panel_state.h"
#include <cstdint>

namespace sextant {
    struct FigureSnapshot;
    struct FigureAxesSnapshot;
    class FigureEditBox;

    struct FigureContext {
        const FigureSnapshot& snap;
        FigureEditBox&        edits;
        Selection&            selection;
        // Shared per figure; reach it through slot_view(), never directly.
        SlotViewState&        slot_view_state;
        // What the plot view showing this figure returned from its last frame
        // (resolved auto limits, the live plot size, its measurements); null
        // when none is on screen.
        const PlotViewInfo*   view;
        // For ImGui::PushID around each window's contents, so two figures'
        // widgets never share ids. Pushed as an int: a process-wide counter.
        std::uint64_t         figure_id;
    };

    // The selected slot's axes, after normalizing the selection to an existing
    // slot (the first one if it names none; a change bumps the generation).
    // Null when the figure has no axes.
    const FigureAxesSnapshot* normalize_selection(Selection& sel, const FigureSnapshot& fsnap);

    // The shared slot view, re-seeded from the selected slot if the selection has
    // moved since, and otherwise following the limits and camera the program set
    // since. Every reader and writer goes through here, so a slot change re-seeds
    // before anything writes, whichever panels are drawn.
    SlotViewState& slot_view(FigureContext& ctx);

    // The Data inspector's pull: re-seeds its per-object scratch when the
    // selection has moved, or when the slot's object counts changed.
    void pull_data_panel(DataPanelState& st, const Selection& sel, const FigureAxesSnapshot& fa);

    // ImGui::PushID for a figure's widgets (see FigureContext::figure_id).
    void push_figure_id(std::uint64_t figure_id);
} // namespace sextant
