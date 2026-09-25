// Automation lanes (plan §F-E): the breakpoint envelope, recording a pass over
// it, and thinning a dense recording back to the points it needs. Control
// thread only; the engine precomputes what it plays from these.

#include "blokkily/model/song.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {

namespace {

double lerp(const AutomationPoint& a, const AutomationPoint& b, Tick at) {
    if (b.at == a.at) return b.value;
    const double t = static_cast<double>(at - a.at) / static_cast<double>(b.at - a.at);
    return a.value + (b.value - a.value) * t;
}

// The value arriving at `at`: at a jump, the first of the two points.
double arriving_value(const std::vector<AutomationPoint>& points, Tick at) {
    const auto it = std::lower_bound(points.begin(), points.end(), at,
        [](const AutomationPoint& point, Tick tick) { return point.at < tick; });
    if (it == points.end()) return points.back().value;
    if (it->at == at || it == points.begin()) return it->value;
    return lerp(*(it - 1), *it, at);
}

// Keeps only the first and last point of each tick, then turns a jump whose
// two sides agree into a single point, and drops repeats of the same point.
std::vector<AutomationPoint> normalised(const std::vector<AutomationPoint>& points) {
    std::vector<AutomationPoint> out;
    for (std::size_t i = 0; i < points.size();) {
        std::size_t last = i;
        while (last + 1 < points.size() && points[last + 1].at == points[i].at) ++last;
        out.push_back(points[i]);
        if (last != i && points[last].value != points[i].value) out.push_back(points[last]);
        i = last + 1;
    }
    return out;
}

} // namespace

std::optional<double> AutomationLane::value_at(Tick at) const {
    if (points.empty()) return std::nullopt;
    const auto after = std::upper_bound(points.begin(), points.end(), at,
        [](Tick tick, const AutomationPoint& point) { return tick < point.at; });
    if (after == points.begin()) return points.front().value;
    const auto& before = *(after - 1);
    if (before.at == at || after == points.end()) return before.value;
    return lerp(before, *after, at);
}

bool AutomationLane::write_pass(Tick from, Tick to, std::span<const AutomationPoint> pass) {
    if (pass.empty() || to < from) return false;
    for (std::size_t i = 0; i < pass.size(); ++i) {
        if (pass[i].at < from || pass[i].at > to || !std::isfinite(pass[i].value)) return false;
        if (i > 0 && pass[i].at < pass[i - 1].at) return false;
    }

    std::vector<AutomationPoint> written;
    for (const auto& point : points)
        if (point.at < from) written.push_back(point);
    if (!points.empty()) written.push_back({from, arriving_value(points, from)});
    written.insert(written.end(), pass.begin(), pass.end());
    if (!points.empty()) {
        written.push_back({to, pass.back().value});
        written.push_back({to, *value_at(to)});
    }
    for (const auto& point : points)
        if (point.at > to) written.push_back(point);
    points = normalised(written);
    return true;
}

void AutomationLane::thin(double tolerance) {
    if (points.size() < 3) return;
    const auto jump = [this](std::size_t i) {
        return (i > 0 && points[i - 1].at == points[i].at) ||
               (i + 1 < points.size() && points[i + 1].at == points[i].at);
    };
    // Ramps between two kept points must pass within tolerance of everything
    // dropped between them.
    const auto fits = [this, tolerance](std::size_t from, std::size_t to) {
        for (std::size_t k = from + 1; k < to; ++k)
            if (std::abs(lerp(points[from], points[to], points[k].at) - points[k].value) >
                tolerance)
                return false;
        return true;
    };

    std::vector<AutomationPoint> kept{points.front()};
    std::size_t anchor = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const bool last = i + 1 == points.size();
        if (last || jump(i)) {
            // A fixed point: everything before it must reach it from the
            // anchor, or the point before it is kept as well.
            if (i > anchor + 1 && !fits(anchor, i)) kept.push_back(points[i - 1]);
            kept.push_back(points[i]);
            anchor = i;
            continue;
        }
        if (!fits(anchor, i + 1)) {
            kept.push_back(points[i]);
            anchor = i;
        }
    }
    points = std::move(kept);
}

bool AutomationLane::well_formed() const {
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!std::isfinite(points[i].value)) return false;
        if (i > 0 && points[i].at < points[i - 1].at) return false;
        if (i > 1 && points[i].at == points[i - 2].at) return false;
    }
    return true;
}

} // namespace blokkily
