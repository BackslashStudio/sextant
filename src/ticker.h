#pragma once
// Tick placement and labels for a 2D axis with a scale (see AxisScale):
// matplotlib's LogLocator, SymmetricalLogLocator and AutoMinorLocator, cut
// down, and the format-string tick formatter. Linear majors are
// generate_ticks() unchanged.
#include "axis_scale.h"
#include "coord_transform.h"
#include "tick.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {
    // ------------------------------------------------------------------------
    // Formatter
    // ------------------------------------------------------------------------
    // A tick format is printf's, for one double: exactly one conversion
    // %[-+ #0]*[0-9]*(.[0-9]*)?[fFeEgG], plus any %% and literal text. Checked
    // before it is stored, so snprintf only ever sees one double.
    inline bool tick_format_ok(std::string_view f) {
        int conversions = 0;
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (f[i] != '%') continue;
            if (++i >= f.size()) return false;
            if (f[i] == '%') continue;
            while (i < f.size() && std::string_view("-+ #0").find(f[i]) != std::string_view::npos) ++i;
            while (i < f.size() && f[i] >= '0' && f[i] <= '9') ++i;
            if (i < f.size() && f[i] == '.') {
                ++i;
                while (i < f.size() && f[i] >= '0' && f[i] <= '9') ++i;
            }
            if (i >= f.size() || std::string_view("fFeEgG").find(f[i]) == std::string_view::npos)
                return false;
            ++conversions;
        }
        return conversions == 1;
    }

    // `fmt` passed tick_format_ok(); empty means "%g".
    inline std::string format_tick(const std::string& fmt, double v) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), fmt.empty() ? "%g" : fmt.c_str(), v);
        return buf;
    }

    namespace ticker_detail {
        // "10", "2", "e" or "%g" of the base: what a power label writes.
        inline std::string base_name(double b) {
            if (b == std::exp(1.0)) return "e";
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", b);
            return buf;
        }

        // b^k as a label: "$10^{3}$" with math on, else the value as "%g".
        inline std::string power_label(const AxisScale& s, int k, double v, bool math) {
            if (!math) return format_tick("", v);
            std::string out = v < 0.0 ? "$-" : "$";
            out += base_name(s.base);
            out += "^{" + std::to_string(k) + "}$";
            return out;
        }

        // Every stride-th integer k in [k0, k1], aligned to multiples of
        // stride so 10^0 stays a tick while the view moves.
        inline int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

        // How many decades apart majors are, for n in view: ~target + 2 at most.
        inline int decade_stride(int n, int target) {
            const int most = std::max(target + 2, 1);
            return std::max(1, (n + most - 1) / most);
        }

        // The decades of a log axis in view: integer exponents k with b^k in
        // [lo, hi], lo < hi, both > 0.
        struct Decades {
            int k0 = 0, k1 = -1;
            int count() const { return k1 - k0 + 1; }
        };

        inline Decades decades_in(const AxisScale& s, double lo, double hi) {
            // Tolerance in scale units, so 10^3 at the edge of [1000, ...] counts.
            const double a = s.fwd(lo), b = s.fwd(hi);
            const double eps = (b - a) * 1e-9;
            return {static_cast<int>(std::ceil(a - eps)), static_cast<int>(std::floor(b + eps))};
        }

        inline bool in_view(double v, double lo, double hi) {
            const double eps = (hi - lo) * 1e-9;
            return v >= lo - eps && v <= hi + eps;
        }

        inline Tick make_tick(double v, std::string label) {
            Tick t;
            t.value = v;
            t.label = std::move(label);
            return t;
        }

        // Linear ticks on [lo, hi], relabelled with `fmt` when given.
        inline std::vector<Tick> linear_majors(double lo, double hi, int target, const std::string& fmt) {
            std::vector<Tick> t = generate_ticks(lo, hi, target);
            if (!fmt.empty())
                for (Tick& k: t) k.label = format_tick(fmt, k.value);
            return t;
        }

        // Base 10 under two decades: 1, 2 and 5 times each power in view.
        inline std::vector<double> log125(const AxisScale& s, double lo, double hi) {
            std::vector<double> out;
            if (s.base != 10.0) return out;
            const int k0 = static_cast<int>(std::floor(s.fwd(lo))) - 1;
            const int k1 = static_cast<int>(std::ceil(s.fwd(hi)));
            for (int k = k0; k <= k1; ++k)
                for (double m: {1.0, 2.0, 5.0}) {
                    const double v = m * s.inv(k);
                    if (in_view(v, lo, hi)) out.push_back(v);
                }
            return out;
        }

        inline std::vector<Tick> log_majors(const AxisScale& s, double lo, double hi, int target,
                                            const std::string& fmt, bool math) {
            const Decades d = decades_in(s, lo, hi);
            std::vector<Tick> out;
            if (d.count() >= 2) {
                const int stride = decade_stride(d.count(), target);
                for (int k = floor_div(d.k0 + stride - 1, stride) * stride; k <= d.k1; k += stride) {
                    const double v = s.inv(k);
                    out.push_back(make_tick(v, fmt.empty() ? power_label(s, k, v, math) : format_tick(fmt, v)));
                }
                return out;
            }
            // Under two decades: 1-2-5 per decade (base 10), else linear ticks.
            const std::vector<double> v125 = log125(s, lo, hi);
            if (v125.size() >= 2) {
                for (double v: v125) out.push_back(make_tick(v, format_tick(fmt, v)));
                return out;
            }
            return linear_majors(lo, hi, target, fmt);
        }

        inline std::vector<Tick> symlog_majors(const AxisScale& s, double lo, double hi, int target,
                                               const std::string& fmt, bool math) {
            // 0 and +-b^k from the first power at or past linthresh.
            const int kt = static_cast<int>(std::ceil(std::log(s.linthresh) / std::log(s.base) - 1e-9));
            const double big = std::max(std::fabs(lo), std::fabs(hi));
            if (!(big > 0.0) || !std::isfinite(big)) return linear_majors(lo, hi, target, fmt);
            const int kmax = static_cast<int>(std::floor(std::log(big) / std::log(s.base) + 1e-9));
            struct Cand { int k; double v; };
            std::vector<Cand> cands;
            for (int k = kmax; k >= kt; --k) {
                const double v = -std::pow(s.base, k);
                if (in_view(v, lo, hi)) cands.push_back({k, v});
            }
            const bool zero = in_view(0.0, lo, hi);
            for (int k = kt; k <= kmax; ++k) {
                const double v = std::pow(s.base, k);
                if (in_view(v, lo, hi)) cands.push_back({k, v});
            }
            const int n = static_cast<int>(cands.size()) + (zero ? 1 : 0);
            if (n < 2) return linear_majors(lo, hi, target, fmt);
            const int stride = decade_stride(n, target);
            std::vector<Tick> out;
            bool zero_done = !zero;
            for (const Cand& c: cands) {
                if (!zero_done && c.v > 0.0) {
                    out.push_back(make_tick(0.0, fmt.empty() ? "0" : format_tick(fmt, 0.0)));
                    zero_done = true;
                }
                if ((c.k - kt) % stride != 0) continue;
                out.push_back(make_tick(c.v, fmt.empty() ? power_label(s, c.k, c.v, math) : format_tick(fmt, c.v)));
            }
            if (!zero_done) out.push_back(make_tick(0.0, fmt.empty() ? "0" : format_tick(fmt, 0.0)));
            return out;
        }

        // AutoMinorLocator: 5 divisions of a 1 or 5 step, 4 of a 2 or 2.5.
        inline std::vector<double> linear_minors(double lo, double hi, int target) {
            if (!(lo < hi)) return {};
            const double step = nice_step((hi - lo) / target);
            if (!(step > 0.0) || !std::isfinite(step)) return {};
            const double m = step / std::pow(10.0, std::floor(std::log10(step)));
            const int ndivs = (std::fabs(m - 2.0) < 1e-6 || std::fabs(m - 2.5) < 1e-6) ? 4 : 5;
            const double ms = step / ndivs;
            std::vector<double> out;
            const double k0 = std::ceil(lo / ms - 1e-9), k1 = std::floor(hi / ms + 1e-9);
            if (k1 - k0 > 10000.0) return {};
            for (double k = k0; k <= k1; k += 1.0) {
                const double r = std::fmod(k, static_cast<double>(ndivs));
                if (r == 0.0) continue; // a major
                out.push_back(k * ms);
            }
            return out;
        }

        // sign * m * b^k for m = 2 .. ceil(b)-1 with m * b^k in [max(lo, floor_abs), hi]
        // (all positive).
        inline void decade_minors(const AxisScale& s, double lo, double hi, double floor_abs,
                                  std::vector<double>& out, double sign) {
            if (!(hi > lo) || !(hi > 0.0)) return;
            const int mmax = static_cast<int>(std::ceil(s.base)) - 1;
            const double a = std::max(lo, floor_abs);
            if (!(a < hi)) return;
            const int k0 = static_cast<int>(std::floor(std::log(a) / std::log(s.base))) - 1;
            const int k1 = static_cast<int>(std::ceil(std::log(hi) / std::log(s.base)));
            if (k1 - k0 > 400) return;
            for (int k = k0; k <= k1; ++k) {
                const double p = std::pow(s.base, k);
                for (int m = 2; m <= mmax; ++m) {
                    const double v = m * p;
                    if (v >= p * s.base) break;
                    if (v >= a && v <= hi) out.push_back(sign * v);
                }
            }
        }
    } // namespace ticker_detail

    // ------------------------------------------------------------------------
    // Majors
    // ------------------------------------------------------------------------
    // The generated ticks of one axis on [lo, hi] (either order, as
    // generate_ticks()). `fmt` empty = the scale's default labels: "%g" on
    // linear, powers ("$10^{3}$", "%g" with math off) on log and symlog.
    inline std::vector<Tick> scale_ticks(double lo, double hi, const AxisScale& s, const std::string& fmt,
                                         bool math, int target = 7) {
        if (s.linear()) {
            if (fmt.empty()) return generate_ticks(lo, hi, target);
            return ticker_detail::linear_majors(lo, hi, target, fmt);
        }
        if (lo > hi) {
            std::vector<Tick> t = scale_ticks(hi, lo, s, fmt, math, target);
            std::reverse(t.begin(), t.end());
            return t;
        }
        if (!(lo < hi)) return {};
        if (s.kind == Scale::Log) {
            if (!(lo > 0.0)) return {};
            return ticker_detail::log_majors(s, lo, hi, target, fmt, math);
        }
        return ticker_detail::symlog_majors(s, lo, hi, target, fmt, math);
    }

    // set_xticks() positions keep their labels; one given without a label is
    // relabelled with `fmt` (when set).
    inline std::vector<Tick> relabel_ticks(std::vector<Tick> ticks, const std::string& fmt) {
        if (!fmt.empty())
            for (Tick& t: ticks)
                if (t.auto_label) t.label = format_tick(fmt, t.value);
        return ticks;
    }

    // ------------------------------------------------------------------------
    // Minors
    // ------------------------------------------------------------------------
    // Unlabelled positions between the generated majors on [lo, hi] (either
    // order), none on a major. `explicit_majors`: set_xticks() positions are
    // drawn, so a linear axis has no step to subdivide and gets none; a log or
    // symlog axis keeps its per-decade minors.
    inline std::vector<double> scale_minor_ticks(double lo, double hi, const AxisScale& s,
                                                 bool explicit_majors, int target = 7) {
        if (lo > hi) std::swap(lo, hi);
        if (!(lo < hi)) return {};
        std::vector<double> out;
        if (s.linear()) {
            if (!explicit_majors) out = ticker_detail::linear_minors(lo, hi, target);
            return out;
        }
        if (s.kind == Scale::Log) {
            if (!(lo > 0.0)) return {};
            const ticker_detail::Decades d = ticker_detail::decades_in(s, lo, hi);
            if (d.count() >= 2) {
                const int stride = ticker_detail::decade_stride(d.count(), target);
                if (stride == 1) ticker_detail::decade_minors(s, lo, hi, 0.0, out, 1.0);
                else
                    // The decades between majors.
                    for (int k = d.k0; k <= d.k1; ++k)
                        if (k % stride != 0) out.push_back(s.inv(k));
                return out;
            }
            if (ticker_detail::log125(s, lo, hi).size() >= 2) {
                // 1-2-5 majors: the other integer multiples are minors.
                std::vector<double> all;
                ticker_detail::decade_minors(s, lo, hi, 0.0, all, 1.0);
                for (double v: all) {
                    const double m = v / std::pow(10.0, std::floor(std::log10(v) + 1e-9));
                    if (std::fabs(m - 2.0) > 1e-6 && std::fabs(m - 5.0) > 1e-6) out.push_back(v);
                }
                return out;
            }
            return ticker_detail::linear_minors(lo, hi, target);
        }
        // Symlog: per-decade minors outside the linear part, none inside.
        const double t = s.linthresh;
        if (hi > t) ticker_detail::decade_minors(s, std::max(lo, t), hi, t, out, 1.0);
        if (lo < -t) ticker_detail::decade_minors(s, std::max(-hi, t), -lo, t, out, -1.0);
        std::sort(out.begin(), out.end());
        return out;
    }
} // namespace sextant
