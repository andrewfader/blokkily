#include "blokkily/audio/sidechain.hpp"

#include <algorithm>
#include <queue>

namespace blokkily {

std::vector<std::size_t> compute_track_render_order(
    std::size_t track_count,
    std::span<const SidechainRoute> sidechains,
    std::span<const MultiOutputRoute> multi_outs) {
    if (track_count == 0) return {};

    std::vector<std::vector<std::size_t>> adj(track_count);
    std::vector<int> in_degree(track_count, 0);

    const auto add_edge = [&](std::size_t from, std::size_t to) {
        if (from == to || from >= track_count || to >= track_count) return;
        // Avoid duplicate edges
        if (std::find(adj[from].begin(), adj[from].end(), to) == adj[from].end()) {
            adj[from].push_back(to);
            in_degree[to]++;
        }
    };

    for (const auto& sc : sidechains) {
        if (sc.target.kind == BusKind::track) {
            const auto dest = static_cast<std::size_t>(sc.target.bus);
            add_edge(sc.source_track, dest);
        }
    }

    for (const auto& mo : multi_outs) {
        add_edge(mo.source_track, mo.dest_track);
    }

    // Kahn's algorithm
    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < track_count; ++i) {
        if (in_degree[i] == 0) {
            ready.push(i);
        }
    }

    std::vector<std::size_t> order;
    order.reserve(track_count);

    while (!ready.empty()) {
        const std::size_t u = ready.front();
        ready.pop();
        order.push_back(u);

        for (const std::size_t v : adj[u]) {
            in_degree[v]--;
            if (in_degree[v] == 0) {
                ready.push(v);
            }
        }
    }

    // In case of a cyclic dependency, break the cycle and add remaining tracks
    if (order.size() < track_count) {
        for (std::size_t i = 0; i < track_count; ++i) {
            if (std::find(order.begin(), order.end(), i) == order.end()) {
                order.push_back(i);
            }
        }
    }

    return order;
}

} // namespace blokkily
