// Style files: a figure's look (and, at the State level, its content-like
// settings) as YAML, and the way back. The walk reads the live Impls (Figure is
// the friend that may) and writes a YAML tree; reading builds FigureEdits and
// applies them through the same appliers a panel edit uses, so nothing here
// knows how a field is stored. Option structs go through yaml_describe.h; the
// few types describe() does not cover (ticks, limits, scales, aspect, polar
// view and rings) are written by hand below.
#include "figure_edits.h"
#include "figure_impl.h"
#include "sextant/figure.h"
#include "tick.h"
#include "yaml_describe.h"
#include "yaml_io.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sextant {
namespace {

namespace dt = yaml::detail;
using yaml::Issues;
using yaml::Node;

constexpr int kFormatVersion = 1;

using FigImpl    = detail::FigureAccess::Impl;
using AxesImpl   = FigImpl::AxesImpl;
using Axes3DImpl = FigImpl::Axes3DImpl;
using PolarImpl  = FigImpl::PolarImpl;

std::string join(const std::string& a, std::string_view b) {
    return a.empty() ? std::string(b) : a + "." + std::string(b);
}

std::string at_index(const std::string& a, std::size_t i) {
    return a + "[" + std::to_string(i) + "]";
}

// --- Option structs -----------------------------------------------------------

template <class T>
Node opts_node(const T& o) { return yaml::to_node(o, T{}); }

// A text's look is two structs.
Node opts_node(const TextStyle& o) {
    Node m = Node::make_map();
    m.add("text", yaml::to_node(o.text, TextOptions{}));
    m.add("arrow", yaml::to_node(o.arrow, ArrowOptions{}));
    return m;
}

template <class T>
void read_opts(const Node& n, T& o, Issues& is, const std::string& at) {
    yaml::from_node(n, o, is, at);
}

void read_opts(const Node& n, TextStyle& o, Issues& is, const std::string& at) {
    if (n.kind == Node::Kind::Null) return;
    if (!n.is_map()) {
        is.add(at + ": expected a mapping, got " + dt::got(n));
        return;
    }
    if (const Node* t = n.find("text")) yaml::from_node(*t, o.text, is, join(at, "text"));
    if (const Node* a = n.find("arrow")) yaml::from_node(*a, o.arrow, is, join(at, "arrow"));
    std::string extra;
    for (const auto& kv : n.map)
        if (kv.first != "text" && kv.first != "arrow") extra += (extra.empty() ? "" : ", ") + kv.first;
    if (!extra.empty()) is.add(at + ": unknown keys skipped: " + extra);
}

// What a file never carries (the per-point arrays) stays as the target has it.
template <class T>
void keep_data(T& fresh, const T& cur) {
    if constexpr (described<T>) {
        std::vector<std::pair<std::string_view, const void*>> src;
        auto collect = [&](std::string_view name, const auto& x) {
            using X = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::is_same_v<X, std::vector<double>> || std::is_same_v<X, std::vector<std::string>>)
                if (yaml::is_data_field(name, x)) src.emplace_back(name, &x);
        };
        describe(collect, cur);
        auto assign = [&](std::string_view name, auto& x) {
            using X = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::is_same_v<X, std::vector<double>> || std::is_same_v<X, std::vector<std::string>>)
                if (yaml::is_data_field(name, x))
                    for (const auto& [k, p] : src)
                        if (k == name) x = *static_cast<const X*>(p);
        };
        describe(assign, fresh);
    }
}

// --- Reading one mapping ------------------------------------------------------

struct Obj {
    const Node& n;
    Issues& is;
    std::string path;
    bool ok = true;
    std::vector<std::string> seen;

    Obj(const Node& node, Issues& issues, std::string p) : n(node), is(issues), path(std::move(p)) {
        if (n.kind == Node::Kind::Null) ok = false;   // "key:" with nothing under it
        else if (!n.is_map()) {
            is.add((path.empty() ? std::string("(top level)") : path) + ": expected a mapping, got " + dt::got(n));
            ok = false;
        }
    }

    const Node* at(std::string_view k) {
        seen.emplace_back(k);
        return ok ? n.find(k) : nullptr;
    }

    std::string sub(std::string_view k) const { return join(path, k); }

    template <class X>
    bool leaf(std::string_view k, X& x) {
        const Node* c = at(k);
        if (!c) return false;
        std::string err;
        X v = x;
        if (!dt::decode_leaf(*c, v, err)) { is.add(sub(k) + ": " + err); return false; }
        x = std::move(v);
        return true;
    }

    template <class X>
    void set(std::string_view k, std::optional<X>& dst) {
        X v{};
        if (leaf(k, v)) dst = std::move(v);
    }

    template <class T>
    void section(std::string_view k, const T& cur, std::optional<T>& dst) {
        const Node* c = at(k);
        if (!c) return;
        T fresh{};
        keep_data(fresh, cur);
        read_opts(*c, fresh, is, sub(k));
        dst = std::move(fresh);
    }

    void finish() {
        if (!ok) return;
        std::string extra;
        for (const auto& kv : n.map) {
            bool known = false;
            for (const auto& s : seen) known = known || s == kv.first;
            if (!known) extra += (extra.empty() ? "" : ", ") + kv.first;
        }
        if (!extra.empty()) is.add((path.empty() ? std::string("(top level)") : path) + ": unknown keys skipped: " + extra);
    }
};

template <class X>
void put(Node& m, const char* k, const X& x) { m.add(k, dt::encode_leaf(x)); }

template <class T>
void put_section(Node& m, const char* k, const T& v) { m.add(k, opts_node(v)); }

// --- Ticks, limits, scales ----------------------------------------------------

Node ticks_node(const std::optional<std::vector<Tick>>& t) {
    if (!t || t->empty()) return Node::scalar("auto");
    Node s = Node::make_seq();
    for (const Tick& k : *t) {
        if (k.auto_label) {
            s.push(dt::encode_leaf(k.value));
        } else {
            Node m = Node::make_map();
            put(m, "value", k.value);
            put(m, "label", k.label);
            s.push(std::move(m));
        }
    }
    return s;
}

Tick auto_tick(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return {v, buf, true};
}

// "auto" gives an empty list (the axes' own ticks); nullopt = bad, already reported.
std::optional<std::vector<Tick>> ticks_read(const Node& n, Issues& is, const std::string& at) {
    if (n.is_scalar() && !n.quoted && n.text == "auto") return std::vector<Tick>{};
    if (!n.is_seq()) {
        is.add(at + ": expected auto or a list of ticks, got " + dt::got(n));
        return std::nullopt;
    }
    std::vector<Tick> out;
    bool bad = false;
    for (std::size_t i = 0; i < n.seq.size(); ++i) {
        const Node& e = n.seq[i];
        const std::string here = at_index(at, i);
        std::string err;
        if (e.is_map()) {
            Obj o(e, is, here);
            double v = 0;
            std::string label;
            if (!o.leaf("value", v)) { if (!e.find("value")) is.add(here + ": a tick needs a value"); bad = true; }
            const bool has_label = o.leaf("label", label);
            o.finish();
            if (!bad) out.push_back(has_label ? Tick{v, label, false} : auto_tick(v));
        } else {
            double v = 0;
            if (dt::decode_leaf(e, v, err)) out.push_back(auto_tick(v));
            else { is.add(here + ": " + err); bad = true; }
        }
    }
    if (bad) return std::nullopt;
    return out;
}

Node lim_node(bool autoscale, double lo, double hi) {
    if (autoscale) return Node::scalar("auto");
    Node s = Node::make_seq();
    s.push(dt::encode_leaf(lo));
    s.push(dt::encode_leaf(hi));
    return s;
}

bool lim_read(const Node& n, std::optional<bool>& autoscale, std::optional<double>& lo,
              std::optional<double>& hi, Issues& is, const std::string& at) {
    if (n.is_scalar() && !n.quoted && n.text == "auto") { autoscale = true; return true; }
    double a = 0, b = 0;
    std::string err;
    if (n.is_seq() && n.seq.size() == 2 && dt::decode_leaf(n.seq[0], a, err) && dt::decode_leaf(n.seq[1], b, err)) {
        autoscale = false; lo = a; hi = b;
        return true;
    }
    is.add(at + ": expected auto or [low, high], got " + dt::got(n));
    return false;
}

Node scale_node(const AxisScale& s) {
    const AxisScale def;
    Node m = Node::make_map();
    put(m, "kind", s.kind);
    if (s.base != def.base) put(m, "base", s.base);
    if (s.linthresh != def.linthresh) put(m, "linthresh", s.linthresh);
    if (s.linscale != def.linscale) put(m, "linscale", s.linscale);
    return m;
}

void scale_read(const Node& n, std::optional<AxisScale>& dst, Issues& is, const std::string& at) {
    Obj o(n, is, at);
    if (!o.ok) return;
    AxisScale s;
    o.leaf("kind", s.kind);
    o.leaf("base", s.base);
    o.leaf("linthresh", s.linthresh);
    o.leaf("linscale", s.linscale);
    o.finish();
    dst = s;
}

Node aspect_node(const AxesAspect& a) {
    Node m = Node::make_map();
    put(m, "ratio", a.ratio);
    put(m, "adjustable", std::string(a.adjustable == Adjustable::Box ? "box" : "datalim"));
    return m;
}

void aspect_read(const Node& n, std::optional<AxesAspect>& dst, Issues& is, const std::string& at) {
    Obj o(n, is, at);
    if (!o.ok) return;
    AxesAspect a;
    o.leaf("ratio", a.ratio);
    std::string adj;
    if (o.leaf("adjustable", adj)) {
        if (adj == "box") a.adjustable = Adjustable::Box;
        else if (adj == "datalim") a.adjustable = Adjustable::Datalim;
        else is.add(o.sub("adjustable") + ": unknown value '" + adj + "', one of: box datalim");
    }
    o.finish();
    dst = a;
}

// --- Plot objects -------------------------------------------------------------

// f(name, objects) for every 2D kind, in PlotStyleEdit's variant order.
template <class D, class F>
void for_each_kind(D& d, F&& f) {
    f("line", d.lines);
    f("scatter", d.scatters);
    f("bar", d.bars);
    f("heatmap", d.heatmaps);
    f("scatter_z", d.scatter_z);
    f("text", d.texts);
    f("axline", d.axlines);
    f("axspan", d.axspans);
    f("fill", d.fills);
    f("quiver", d.quivers);
    f("poly", d.polys);
    f("circle", d.circles);
    f("wedge", d.wedges);
    f("pie", d.pies);
    f("heatmap_tri", d.heatmap_tris);
    f("contour", d.contours);
    f("stack", d.stacks);
    f("bar_stack", d.bar_stacks);
}

Node plots_node(const AxesImpl& d) {
    Node m = Node::make_map();
    for_each_kind(d, [&](const char* name, const auto& v) {
        if (v.empty()) return;
        Node s = Node::make_seq();
        for (const auto& p : v) s.push(opts_node(p.opts));
        m.add(name, std::move(s));
    });
    return m;
}

// `plane` = -1: the axes itself, else the plane of a 3D axes.
void plots_read(const Node& n, const AxesImpl& cur, int plane, std::vector<PlotStyleEdit>& out,
                Issues& is, const std::string& at) {
    Obj o(n, is, at);
    if (!o.ok) return;
    for_each_kind(cur, [&](const char* name, const auto& v) {
        const Node* list = o.at(name);
        if (!list) return;
        const std::string here = o.sub(name);
        if (!list->is_seq()) {
            is.add(here + ": expected a list, got " + dt::got(*list));
            return;
        }
        for (std::size_t i = 0; i < list->seq.size(); ++i) {
            if (i >= v.size()) {
                is.add(here + ": the file has " + std::to_string(list->seq.size()) + " but this figure has "
                       + std::to_string(v.size()) + "; the rest skipped");
                break;
            }
            using O = std::remove_cvref_t<decltype(v[i].opts)>;
            O fresh{};
            keep_data(fresh, v[i].opts);
            read_opts(list->seq[i], fresh, is, at_index(here, i));
            PlotStyleEdit pe;
            pe.plot_index = static_cast<int>(i);
            pe.plane_index = plane;
            pe.opts = fresh;
            out.push_back(std::move(pe));
        }
    });
    o.finish();
}

template <class Vec>
Node objects_node(const Vec& v) {
    Node s = Node::make_seq();
    for (const auto& p : v) s.push(opts_node(p.opts));
    return s;
}

template <class Vec, class Edit>
void objects_read(const Node& n, const Vec& cur, std::vector<Edit>& out, Issues& is, const std::string& at) {
    if (!n.is_seq()) {
        is.add(at + ": expected a list, got " + dt::got(n));
        return;
    }
    for (std::size_t i = 0; i < n.seq.size(); ++i) {
        if (i >= cur.size()) {
            is.add(at + ": the file has " + std::to_string(n.seq.size()) + " but this figure has "
                   + std::to_string(cur.size()) + "; the rest skipped");
            break;
        }
        using O = std::remove_cvref_t<decltype(cur[i].opts)>;
        O fresh{};
        keep_data(fresh, cur[i].opts);
        read_opts(n.seq[i], fresh, is, at_index(at, i));
        Edit e;
        e.plot_index = static_cast<int>(i);
        e.opts = fresh;
        out.push_back(std::move(e));
    }
}

// --- Axes ---------------------------------------------------------------------

// The part shared by 2D, twin and polar axes: grid, legend, colorbar, frame style,
// plots. `cell` false = a twin (title, legend and colorbar are the host's).
void look_node(Node& m, const AxesImpl& d, bool host) {
    put(m, "grid", d.grid_enabled);
    put_section(m, "grid_options", d.grid_opts);
    if (host) {
        put(m, "legend", d.legend_enabled);
        put_section(m, "legend_options", d.legend_opts);
        put_section(m, "colorbar_options", d.colorbar_opts);
    }
    put_section(m, "axes_style", d.axes_style);
}

void look_read(Obj& o, const AxesImpl& cur, AxesEdit& e) {
    o.set("grid", e.grid_enabled);
    o.section("grid_options", cur.grid_opts, e.grid_opts);
    o.set("legend", e.legend_enabled);
    o.section("legend_options", cur.legend_opts, e.legend_opts);
    o.section("colorbar_options", cur.colorbar_opts, e.colorbar_opts);
    o.section("axes_style", cur.axes_style, e.axes_style);
    if (const Node* p = o.at("plots")) plots_read(*p, cur, -1, e.plot_styles, o.is, o.sub("plots"));
}

// `layer`: 0 an axes, kLayerTwinX / kLayerTwinY a twin (its shared axis is the host's).
Node axes2d_node(const AxesImpl& d, bool state, int layer) {
    const bool host = layer == 0;
    const bool x = layer != kLayerTwinX, y = layer != kLayerTwinY;
    Node m = Node::make_map();
    if (state) {
        if (host) put(m, "title", d.title);
        if (x) put(m, "xtitle", d.xtitle);
        if (y) put(m, "ytitle", d.ytitle);
        if (x) m.add("xlim", lim_node(d.xlim_auto, d.xmin, d.xmax));
        if (y) m.add("ylim", lim_node(d.ylim_auto, d.ymin, d.ymax));
        if (x) m.add("xticks", ticks_node(d.xticks_override));
        if (y) m.add("yticks", ticks_node(d.yticks_override));
    }
    look_node(m, d, host);
    if (x) m.add("xscale", scale_node(d.xscale));
    if (y) m.add("yscale", scale_node(d.yscale));
    if (x) put(m, "xtick_format", d.xtick_format);
    if (y) put(m, "ytick_format", d.ytick_format);
    if (host) {
        m.add("aspect", aspect_node(d.aspect));
        put(m, "axis_on", d.axis_on);
    }
    Node plots = plots_node(d);
    if (!plots.map.empty()) m.add("plots", std::move(plots));
    return m;
}

void axes2d_read(Obj& o, const AxesImpl& cur, AxesEdit& e) {
    o.set("title", e.title);
    o.set("xtitle", e.xtitle);
    o.set("ytitle", e.ytitle);
    if (const Node* n = o.at("xlim")) lim_read(*n, e.xlim_auto, e.xmin, e.xmax, o.is, o.sub("xlim"));
    if (const Node* n = o.at("ylim")) lim_read(*n, e.ylim_auto, e.ymin, e.ymax, o.is, o.sub("ylim"));
    if (const Node* n = o.at("xticks")) e.xticks_override = ticks_read(*n, o.is, o.sub("xticks"));
    if (const Node* n = o.at("yticks")) e.yticks_override = ticks_read(*n, o.is, o.sub("yticks"));
    look_read(o, cur, e);
    if (const Node* n = o.at("xscale")) scale_read(*n, e.xscale, o.is, o.sub("xscale"));
    if (const Node* n = o.at("yscale")) scale_read(*n, e.yscale, o.is, o.sub("yscale"));
    o.set("xtick_format", e.xtick_format);
    o.set("ytick_format", e.ytick_format);
    if (const Node* n = o.at("aspect")) aspect_read(*n, e.aspect, o.is, o.sub("aspect"));
    o.set("axis_on", e.axis_on);
}

Node polar_node(const PolarImpl& p, bool state) {
    const AxesImpl& d = p.sheet;
    Node m = Node::make_map();
    if (state) {
        put(m, "title", d.title);
        m.add("rlim", lim_node(p.view.rauto, p.view.rmin, p.view.rmax));
        m.add("rticks", ticks_node(p.style.rticks_override));
        m.add("thetagrids", ticks_node(p.style.thetagrids_override));
    }
    look_node(m, d, true);
    Node v = Node::make_map();
    put(v, "theta_offset", p.view.theta_offset);
    put(v, "direction", p.view.direction);
    put(v, "thetamin", p.view.thetamin);
    put(v, "thetamax", p.view.thetamax);
    m.add("view", std::move(v));
    put(m, "rlabel_position", p.style.rlabel_position);
    put(m, "rtick_format", p.style.rtick_format);
    Node plots = plots_node(d);
    if (!plots.map.empty()) m.add("plots", std::move(plots));
    return m;
}

void polar_read(Obj& o, const PolarImpl& cur, AxesEdit& e) {
    o.set("title", e.title);
    look_read(o, cur.sheet, e);
    std::optional<PolarView> view;
    std::optional<PolarStyle> style;
    const auto v = [&]() -> PolarView& { if (!view) view = cur.view; return *view; };
    const auto s = [&]() -> PolarStyle& { if (!style) style = cur.style; return *style; };
    if (const Node* n = o.at("view")) {
        Obj vo(*n, o.is, o.sub("view"));
        if (vo.ok) {
            PolarView& pv = v();
            vo.leaf("theta_offset", pv.theta_offset);
            vo.leaf("direction", pv.direction);
            vo.leaf("thetamin", pv.thetamin);
            vo.leaf("thetamax", pv.thetamax);
            vo.finish();
        }
    }
    if (const Node* n = o.at("rlim")) {
        std::optional<bool> a;
        std::optional<double> lo, hi;
        if (lim_read(*n, a, lo, hi, o.is, o.sub("rlim"))) {
            PolarView& pv = v();
            pv.rauto = *a;
            if (lo) pv.rmin = *lo;
            if (hi) pv.rmax = *hi;
        }
    }
    double rl = cur.style.rlabel_position;
    if (o.leaf("rlabel_position", rl)) s().rlabel_position = rl;
    std::string fmt = cur.style.rtick_format;
    if (o.leaf("rtick_format", fmt)) s().rtick_format = fmt;
    if (const Node* n = o.at("rticks"))
        if (auto t = ticks_read(*n, o.is, o.sub("rticks")))
            s().rticks_override = t->empty() ? std::nullopt : std::optional(std::move(*t));
    if (const Node* n = o.at("thetagrids"))
        if (auto t = ticks_read(*n, o.is, o.sub("thetagrids")))
            s().thetagrids_override = t->empty() ? std::nullopt : std::optional(std::move(*t));
    e.polar_view = view;
    e.polar_style = style;
}

Node axes3d_node(const Axes3DImpl& d, bool state) {
    Node m = Node::make_map();
    if (state) {
        put(m, "title", d.title);
        put(m, "xtitle", d.xtitle);
        put(m, "ytitle", d.ytitle);
        put(m, "ztitle", d.ztitle);
        m.add("xlim", lim_node(d.xlim_auto, d.xmin, d.xmax));
        m.add("ylim", lim_node(d.ylim_auto, d.ymin, d.ymax));
        m.add("zlim", lim_node(d.zlim_auto, d.zmin, d.zmax));
        m.add("xticks", ticks_node(d.xticks_override));
        m.add("yticks", ticks_node(d.yticks_override));
        m.add("zticks", ticks_node(d.zticks_override));
    }
    put(m, "grid", d.grid_enabled);
    put_section(m, "grid_options", d.grid_opts);
    put(m, "legend", d.legend_enabled);
    put_section(m, "legend_options", d.legend_opts);
    put_section(m, "colorbar_options", d.colorbar_opts);
    put_section(m, "axes_style", d.axes_style);
    put_section(m, "box_style", d.box_style);
    put_section(m, "aspect", d.aspect);
    put_section(m, "camera", d.camera);

    if (d.plane_count() > 0) {
        Node planes = Node::make_seq();
        for (std::size_t i = 0; i < d.plane_count(); ++i) {
            const auto& pl = d.plane_at(i);
            Node pm = Node::make_map();
            put(pm, "orient", pl.orient);
            put(pm, "offset", pl.offset);
            put_section(pm, "options", pl.opts);
            Node plots = plots_node(pl.sheet);
            if (!plots.map.empty()) pm.add("plots", std::move(plots));
            planes.push(std::move(pm));
        }
        m.add("planes", std::move(planes));
    }
    const auto list = [&](const char* name, const auto& v) { if (!v.empty()) m.add(name, objects_node(v)); };
    list("bar3d", d.bars3d);
    list("surface", d.surfaces);
    list("scatter3d", d.scatter3d);
    list("line3d", d.lines3d);
    list("surface_tri", d.surface_tri);
    list("quiver3d", d.quivers3d);
    list("text", d.texts);
    return m;
}

void axes3d_read(Obj& o, const Axes3DImpl& cur, AxesEdit3D& e) {
    o.set("title", e.title);
    o.set("xtitle", e.xtitle);
    o.set("ytitle", e.ytitle);
    o.set("ztitle", e.ztitle);
    if (const Node* n = o.at("xlim")) lim_read(*n, e.xlim_auto, e.xmin, e.xmax, o.is, o.sub("xlim"));
    if (const Node* n = o.at("ylim")) lim_read(*n, e.ylim_auto, e.ymin, e.ymax, o.is, o.sub("ylim"));
    if (const Node* n = o.at("zlim")) lim_read(*n, e.zlim_auto, e.zmin, e.zmax, o.is, o.sub("zlim"));
    if (const Node* n = o.at("xticks")) e.xticks_override = ticks_read(*n, o.is, o.sub("xticks"));
    if (const Node* n = o.at("yticks")) e.yticks_override = ticks_read(*n, o.is, o.sub("yticks"));
    if (const Node* n = o.at("zticks")) e.zticks_override = ticks_read(*n, o.is, o.sub("zticks"));
    o.set("grid", e.grid_enabled);
    o.section("grid_options", cur.grid_opts, e.grid_opts);
    o.set("legend", e.legend_enabled);
    o.section("legend_options", cur.legend_opts, e.legend_opts);
    o.section("colorbar_options", cur.colorbar_opts, e.colorbar_opts);
    o.section("axes_style", cur.axes_style, e.axes_style);
    o.section("box_style", cur.box_style, e.box_style);
    o.section("aspect", cur.aspect, e.aspect);
    o.section("camera", cur.camera, e.camera);

    if (const Node* pn = o.at("planes")) {
        const std::string here = o.sub("planes");
        if (!pn->is_seq()) {
            o.is.add(here + ": expected a list, got " + dt::got(*pn));
        } else {
            for (std::size_t i = 0; i < pn->seq.size(); ++i) {
                if (i >= cur.plane_count()) {
                    o.is.add(here + ": the file has " + std::to_string(pn->seq.size()) + " but this figure has "
                             + std::to_string(cur.plane_count()) + "; the rest skipped");
                    break;
                }
                const auto& pl = cur.plane_at(i);
                Obj po(pn->seq[i], o.is, at_index(here, i));
                if (!po.ok) continue;
                AxesEdit3D::PlaneEdit pe;
                pe.plane_index = static_cast<int>(i);
                po.set("orient", pe.orient);
                po.set("offset", pe.offset);
                po.section("options", pl.opts, pe.opts);
                if (const Node* p = po.at("plots")) plots_read(*p, pl.sheet, static_cast<int>(i), e.plot_styles, o.is, po.sub("plots"));
                po.finish();
                e.planes.push_back(std::move(pe));
            }
        }
    }
    const auto list = [&](const char* name, const auto& cur_v, auto& out) {
        if (const Node* n = o.at(name)) objects_read(*n, cur_v, out, o.is, o.sub(name));
    };
    list("bar3d", cur.bars3d, e.bars3d);
    list("surface", cur.surfaces, e.surfaces);
    list("scatter3d", cur.scatter3d, e.scatter3d);
    list("line3d", cur.lines3d, e.lines3d);
    list("surface_tri", cur.surface_tri, e.surface_tri);
    list("quiver3d", cur.quivers3d, e.quivers3d);
    list("text", cur.texts, e.texts);
}

} // namespace

// --- The figure ---------------------------------------------------------------

struct Figure::Impl::StyleFile {
    static Node write(Figure::Impl& d, Figure::StyleDetail detail) {
        const bool state = detail == Figure::StyleDetail::State;
        Node root = Node::make_map();
        put(root, "sextant_style", kFormatVersion);
        put(root, "detail", std::string(state ? "state" : "style"));

        const Shape g = d.grid_shape();
        {
            Node s = Node::make_seq();
            s.push(dt::encode_leaf(g.rows));
            s.push(dt::encode_leaf(g.cols));
            root.add("grid", std::move(s));
        }

        Node f = Node::make_map();
        if (state) {
            Node size = Node::make_seq();
            size.push(dt::encode_leaf(d.opts.width));
            size.push(dt::encode_leaf(d.opts.height));
            f.add("size", std::move(size));
            put(f, "suptitle", d.suptitle_text);
        }
        put(f, "background", d.opts.background);
        put_section(f, "margins", d.opts.margins);
        put(f, "col_gap", d.opts.subplot_col_gap);
        put(f, "row_gap", d.opts.subplot_row_gap);
        put(f, "pick_radius", d.opts.pick_radius);
        put(f, "col_ratios", d.col_ratios);
        put(f, "row_ratios", d.row_ratios);
        put_section(f, "suptitle_style", d.suptitle_opts);
        root.add("figure", std::move(f));

        Node axes = Node::make_seq();
        for (auto& s : d.slots) {
            Node a;
            if (PolarImpl* p = polar_impl(s)) {
                a = polar_node(*p, state);
                a.map.insert(a.map.begin(), {"kind", Node::scalar("polar")});
            } else if (s.as3d()) {
                a = axes3d_node(*s.as3d()->d, state);
                a.map.insert(a.map.begin(), {"kind", Node::scalar("axes3d")});
            } else if (s.as2d()) {
                AxesImpl& x = *s.as2d()->d;
                a = axes2d_node(x, state, 0);
                if (x.twin_x) a.add("twin_x", axes2d_node(*x.twin_x->d, state, kLayerTwinX));
                if (x.twin_y) a.add("twin_y", axes2d_node(*x.twin_y->d, state, kLayerTwinY));
                a.map.insert(a.map.begin(), {"kind", Node::scalar("axes")});
            } else {
                continue;
            }
            a.map.insert(a.map.begin(), {"cell", dt::encode_leaf(s.index)});
            axes.push(std::move(a));
        }
        root.add("axes", std::move(axes));
        return root;
    }

    // Reads the document into edits; what cannot be applied is reported.
    static void read(Figure::Impl& d, const Node& root, FigureEdits& fe, Issues& is,
                     std::optional<std::pair<int, int>>& size, std::optional<float>& pick) {
        Obj top(root, is, "");
        top.at("sextant_style");
        top.at("detail");
        if (const Node* g = top.at("grid")) {
            int r = 0, c = 0;
            std::string err;
            const Shape have = d.grid_shape();
            if (g->is_seq() && g->seq.size() == 2 && dt::decode_leaf(g->seq[0], r, err) && dt::decode_leaf(g->seq[1], c, err)) {
                if (r != have.rows || c != have.cols)
                    is.add("grid: the file is for " + std::to_string(r) + "x" + std::to_string(c)
                           + " cells, this figure has " + std::to_string(have.rows) + "x" + std::to_string(have.cols));
            }
        }

        if (const Node* fn = top.at("figure")) {
            Obj f(*fn, is, "figure");
            if (f.ok) {
                const Shape g = d.grid_shape();
                if (const Node* sz = f.at("size")) {
                    int w = 0, h = 0;
                    std::string err;
                    if (sz->is_seq() && sz->seq.size() == 2 && dt::decode_leaf(sz->seq[0], w, err) && dt::decode_leaf(sz->seq[1], h, err)
                        && w > 0 && h > 0)
                        size = std::pair{w, h};
                    else
                        is.add("figure.size: expected [width, height] of positive integers");
                }
                f.set("suptitle", fe.suptitle);
                f.set("background", fe.background);
                f.section("margins", d.opts.margins, fe.margins);
                f.set("col_gap", fe.col_gap);
                f.set("row_gap", fe.row_gap);
                float pr = 0;
                if (f.leaf("pick_radius", pr)) {
                    if (std::isfinite(pr) && pr >= 0.0f) pick = pr;
                    else is.add("figure.pick_radius: must be finite and >= 0");
                }
                const auto ratios = [&](const char* key, int n, std::optional<std::vector<float>>& dst) {
                    std::vector<float> r;
                    if (!f.leaf(key, r)) return;
                    bool good = r.empty() || static_cast<int>(r.size()) == n;
                    for (float x : r) good = good && std::isfinite(x) && x > 0.0f;
                    if (good) dst = std::move(r);
                    else is.add(f.sub(key) + ": needs " + std::to_string(n) + " positive numbers (or none for equal)");
                };
                ratios("col_ratios", g.cols, fe.col_ratios);
                ratios("row_ratios", g.rows, fe.row_ratios);
                f.section("suptitle_style", d.suptitle_opts, fe.suptitle_opts);
                f.finish();
            }
        }

        if (const Node* an = top.at("axes")) {
            if (!an->is_seq()) {
                is.add("axes: expected a list, got " + dt::got(*an));
            } else {
                for (std::size_t i = 0; i < an->seq.size(); ++i) read_slot(d, an->seq[i], at_index("axes", i), fe, is);
            }
        }
        top.finish();
    }

    static void read_slot(Figure::Impl& d, const Node& entry, const std::string& at, FigureEdits& fe, Issues& is) {
        Obj o(entry, is, at);
        if (!o.ok) return;
        int cell = 0;
        std::string kind;
        if (!o.leaf("cell", cell) || !o.leaf("kind", kind)) {
            if (!o.n.find("cell") || !o.n.find("kind")) is.add(at + ": needs cell and kind");
            return;
        }
        Slot* slot = nullptr;
        for (auto& s : d.slots) if (s.index == cell) slot = &s;
        if (!slot) {
            is.add(at + ": this figure has no axes at cell " + std::to_string(cell) + "; skipped");
            return;
        }
        if (PolarImpl* p = polar_impl(*slot)) {
            if (kind != "polar") return kind_mismatch(at, kind, "polar", is);
            AxesEdit e;
            polar_read(o, *p, e);
            o.finish();
            fe.per_axes.emplace_back(cell, std::move(e));
        } else if (slot->as3d()) {
            if (kind != "axes3d") return kind_mismatch(at, kind, "axes3d", is);
            AxesEdit3D e;
            axes3d_read(o, *slot->as3d()->d, e);
            o.finish();
            fe.per_axes3d.emplace_back(cell, std::move(e));
        } else if (slot->as2d()) {
            if (kind != "axes") return kind_mismatch(at, kind, "axes", is);
            AxesImpl& x = *slot->as2d()->d;
            AxesEdit e;
            axes2d_read(o, x, e);
            const auto twin = [&](const char* name, const std::shared_ptr<Axes>& t, int layer) {
                const Node* n = o.at(name);
                if (!n) return;
                if (!t) { is.add(o.sub(name) + ": this axes has no such twin; skipped"); return; }
                Obj to(*n, is, o.sub(name));
                if (!to.ok) return;
                AxesEdit te;
                axes2d_read(to, *t->d, te);
                to.finish();
                fe.per_axes.emplace_back(axes_key(cell, layer), std::move(te));
            };
            twin("twin_x", x.twin_x, kLayerTwinX);
            twin("twin_y", x.twin_y, kLayerTwinY);
            o.finish();
            fe.per_axes.emplace_back(cell, std::move(e));
        }
    }

    static void kind_mismatch(const std::string& at, const std::string& file, const std::string& have, Issues& is) {
        is.add(at + ": the file has " + file + " axes here, this figure has " + have + "; skipped");
    }

    static void apply(Figure::Impl& d, const FigureEdits& fe) {
        for (const auto& [key, e] : fe.per_axes) {
            if (AxesImpl* a = d.find_slot_impl(key)) a->apply_shared_edit(e);
            if (PolarImpl* p = d.find_slot_polar(key)) apply_polar_edit(*p, e);
        }
        for (const auto& [idx, e] : fe.per_axes3d)
            if (Axes3DImpl* a = d.find_slot_impl3d(idx)) apply_axes3d_edit(*a, e);
        d.apply_figure_level(fe);
        d.fold_ratios(fe.col_ratios, fe.row_ratios);
    }
};

// --- Public ---------------------------------------------------------------------

std::string Figure::style_yaml(StyleDetail detail) const {
    if (d->open.load()) d->apply_edits_and_publish();   // the window's pending edits first
    return yaml::emit(Impl::StyleFile::write(*d, detail));
}

std::vector<std::string> Figure::apply_style_yaml(std::string_view text) {
    const Node root = yaml::parse(text);
    if (!root.is_map() || !root.find("sextant_style"))
        throw std::invalid_argument("Figure::apply_style_yaml: not a sextant style file (no sextant_style key)");
    int version = 0;
    std::string err;
    if (!dt::decode_leaf(*root.find("sextant_style"), version, err))
        throw std::invalid_argument("Figure::apply_style_yaml: sextant_style must be an integer format version");
    if (version > kFormatVersion)
        throw std::invalid_argument("Figure::apply_style_yaml: this file is style format " + std::to_string(version)
                                    + ", this library reads up to " + std::to_string(kFormatVersion));
    if (version < 1)
        throw std::invalid_argument("Figure::apply_style_yaml: sextant_style must be 1 or more");

    const bool open = d->open.load();
    if (open) d->apply_edits_and_publish();   // so what is read is what is shown

    Issues is;
    FigureEdits fe;
    std::optional<std::pair<int, int>> size;
    std::optional<float> pick;
    Impl::StyleFile::read(*d, root, fe, is, size, pick);
    Impl::StyleFile::apply(*d, fe);
    if (pick) set_pick_radius(*pick);
    if (size) resize(size->first, size->second);
    if (open) d->apply_edits_and_publish();
    return std::move(is.list);
}

void Figure::save_style(std::string_view path, StyleDetail detail) const {
    const std::string text = style_yaml(detail);
    std::ofstream out{std::string(path), std::ios::binary};
    if (!out) throw std::runtime_error("Figure::save_style: cannot open '" + std::string(path) + "' for writing");
    out << text;
    out.flush();
    if (!out) throw std::runtime_error("Figure::save_style: writing '" + std::string(path) + "' failed");
}

std::vector<std::string> Figure::load_style(std::string_view path) {
    std::ifstream in{std::string(path), std::ios::binary};
    if (!in) throw std::runtime_error("Figure::load_style: cannot open '" + std::string(path) + "'");
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return apply_style_yaml(text);
}

} // namespace sextant
