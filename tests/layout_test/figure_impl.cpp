// Figure::Impl reached from outside Figure, through figure_impl.h and
// detail::FigureAccess -- the path an app built on sextant::internal drives a
// figure by, on one thread and with no window: publish, read the snapshot,
// push edits as a panel would, publish again.
#include "layout_test.h"
#include "figure_impl.h"

#include <stdexcept>
#include <type_traits>

namespace lt {
    using namespace sextant;

    void test_figure_impl_access() {
        std::printf("\n[figure_impl.h: driving a figure without show()]\n");

        auto fig = Figure::create();
        auto ax  = fig->add_subplot(1, 2, 1);
        auto ax3 = fig->add_subplot3d(1, 2, 2);
        const std::vector<double> y{ 1.0, 3.0, 2.0 };
        ax->line(y);
        ax->set_title("caller");
        ax3->set_title("caller 3d");

        auto& fi = detail::FigureAccess::impl(*fig);
        static_assert(std::is_same_v<decltype(fi), detail::FigureAccess::Impl&>);
        const auto& cfi = detail::FigureAccess::impl(static_cast<const Figure&>(*fig));
        check(&cfi == &fi, "FigureAccess: the const and non-const overloads reach the same Impl");
        check(!fi.open.load() && !fi.window_thread, "a figure never shown has no window");
        check(!fi.snapshot_box.load(), "nothing is published before the first apply_edits_and_publish()");

        // Slot lookup through the aliases: each lane finds only its own kind.
        detail::FigureAccess::Impl::AxesImpl*   a2 = fi.find_slot_impl(1);
        detail::FigureAccess::Impl::Axes3DImpl* a3 = fi.find_slot_impl3d(2);
        check(a2 && a2->title == "caller" && a2->lines.size() == 1,
              "find_slot_impl(1) is the 2D axes' live Impl");
        check(a3 && a3->title == "caller 3d", "find_slot_impl3d(2) is the 3D axes' live Impl");
        check(!fi.find_slot_impl(2) && !fi.find_slot_impl3d(1) && !fi.find_slot_impl(9),
              "a slot of the other kind, or no slot at all, is null");

        fi.apply_edits_and_publish();
        auto snap = fi.snapshot_box.load();
        bool shape = snap && snap->axes.size() == 2;
        if (shape) {
            const auto& s1 = snap->axes[0];
            const auto& s2 = snap->axes[1];
            shape = s1.slot.index == 1 && s1.snap2d() && s1.snap2d()->lines.size() == 1
                 && s1.snap2d()->title == "caller"
                 && s2.slot.index == 2 && s2.snap3d() && s2.snap3d()->title == "caller 3d";
        }
        check(shape, "publish: the snapshot holds both slots, each of its own kind, with the caller's content");

        // A panel's edit: pushed over the drawn snapshot, drained by the next publish.
        fi.edit_box.set_drawn(snap);
        fi.edit_box.update(1, [](AxesEdit& e) { e.title = "from panel"; });
        fi.edit_box.update3d(2, [](AxesEdit3D& e) { e.title = "from panel 3d"; });
        fi.apply_edits_and_publish();
        auto snap2 = fi.snapshot_box.load();
        check(snap2 && snap2->generation > snap->generation, "publish: a new snapshot, with a newer generation");
        check(snap2 && snap2->axes.size() == 2 && snap2->axes[0].snap2d()
              && snap2->axes[0].snap2d()->title == "from panel"
              && snap2->axes[1].snap3d() && snap2->axes[1].snap3d()->title == "from panel 3d",
              "edit: both lanes' titles reach the published snapshot");
        check(a2->title == "from panel" && a3->title == "from panel 3d" && ax->title() == "from panel",
              "edit: and the live Impl, so the public read-back sees it");

        // The stamp rule still holds on this path: a set_title() made after the
        // edit was typed wins over it. Titles carry the stamp they were typed
        // over from the push site, as the panel does (set_drawn() stamps
        // styles, not titles).
        fi.edit_box.set_drawn(snap2);
        const unsigned long long typed_over = snap2->axes[0].snap2d()->title_stamps.title;
        fi.edit_box.update(1, [typed_over](AxesEdit& e) {
            e.title = "stale";
            e.title_seen.title = typed_over;
        });
        ax->set_title("newer");
        fi.apply_edits_and_publish();
        auto snap3 = fi.snapshot_box.load();
        check(snap3 && snap3->axes[0].snap2d() && snap3->axes[0].snap2d()->title == "newer"
              && a2->title == "newer",
              "stamps: a setter call made since the edit was typed wins");
        check(!fi.open.load(), "no window was opened along the way");
    }

    // The frame a host runs instead of show(): host_frame() patches and draws,
    // the host publishes when it chooses.
    void test_host_frame() {
        std::printf("\n[figure_impl.h: host_frame(), publishing on the host's call]\n");

        auto fig = Figure::create();
        auto ax  = fig->add_subplot(1, 1, 1);
        ax->line(std::vector<double>{ 1.0, 3.0, 2.0 });
        ax->set_title("caller");
        auto& fi = detail::FigureAccess::impl(*fig);
        detail::FigureAccess::Impl::AxesImpl* a2 = fi.find_slot_impl(1);

        // has_journal() follows the journal alone, not what is pending.
        fi.edit_box.update(1, [](AxesEdit& e) { e.title = "pending"; });
        check(!fi.edit_box.has_journal(), "has_journal(): a pending edit is not journaled yet");
        fi.edit_box.load_and_clear_journaled();
        check(fi.edit_box.has_journal(), "has_journal(): true after a journaled drain");
        fi.edit_box.take_journal();
        check(!fi.edit_box.has_journal(), "has_journal(): false once the journal is taken");

        auto s1 = fi.host_frame();
        check(s1 && s1->axes.size() == 1 && s1->axes[0].snap2d()
              && s1->axes[0].snap2d()->title == "caller",
              "host_frame(): the first call publishes the initial snapshot");
        check(fi.host_frame() == s1, "host_frame(): an idle frame returns the same snapshot");

        // A typed title, stamped at the push site as the panel does.
        const auto typed_over = s1->axes[0].snap2d()->title_stamps.title;
        fi.edit_box.update(1, [typed_over](AxesEdit& e) {
            e.title = "typed";
            e.title_seen.title = typed_over;
        });
        auto s2 = fi.host_frame();
        check(s2 != s1 && s2->axes[0].snap2d()->title == "typed",
              "host_frame(): the next frame patches the edit onto the snapshot");
        check(s2->data_generation == s1->data_generation && s2->layout_generation != s1->layout_generation,
              "host_frame(): a title patch keeps the data caches");
        check(a2->title == "caller" && fi.edit_box.has_journal(),
              "host_frame(): never publishes on its own -- the live axes wait, journaled");
        auto s3 = fi.host_frame();
        check(s3 == s2 && fi.edit_box.has_journal() && a2->title == "caller",
              "host_frame(): a quiet frame does not publish either");

        // The host's publish.
        fi.apply_edits_and_publish();
        check(a2->title == "typed" && ax->title() == "typed" && !fi.edit_box.has_journal(),
              "publish: the journal reaches the live axes and is cleared");
        auto s4 = fi.host_frame();
        check(s4 != s3 && s4->generation > s3->generation && s4->axes[0].snap2d()->title == "typed",
              "publish: the next frame draws the rebuilt snapshot");

        // A data op: one new data generation from the patch, replayed on publish.
        fi.edit_box.update(1, [](AxesEdit& e) {
            e.plot_ops.push_back(PlotCellEdit{PlotKind::Line, 0, 1, 2, 9.0});
        });
        auto s5 = fi.host_frame();
        check(s5->data_generation != s4->data_generation && s5->axes[0].snap2d()->lines[0].y[2] == 9.0
              && a2->lines[0].y[2] == 2.0,
              "data op: patched onto the snapshot with a new data generation, live data untouched");
        fi.apply_edits_and_publish();
        check(a2->lines[0].y[2] == 9.0, "data op: the publish replays it into the live axes");

        // Public-API changes reach the snapshot through the host's publish too.
        ax->set_title("setter");
        check(fi.host_frame()->axes[0].snap2d()->title == "typed",
              "setter: not drawn before the host publishes");
        fi.apply_edits_and_publish();
        check(fi.host_frame()->axes[0].snap2d()->title == "setter",
              "setter: drawn after it");

        // One driver at a time.
        bool threw = false;
        fi.open.store(true);
        try { fi.host_frame(); } catch (const std::logic_error&) { threw = true; }
        fi.open.store(false);
        check(threw, "host_frame(): throws while the figure is shown");
    }
} // namespace lt
