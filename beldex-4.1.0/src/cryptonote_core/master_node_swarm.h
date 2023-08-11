#pragma once

#include "master_node_rules.h"

#include <map>
#include <vector>
#include <random>

namespace master_nodes {
    inline constexpr uint64_t MAX_ID = UNASSIGNED_SWARM_ID - 1;

    using swarm_mnode_map_t = std::map<swarm_id_t, std::vector<crypto::public_key>>;
    struct swarm_size {
        swarm_id_t swarm_id;
        size_t size;
    };
    struct excess_pool_mnode {
        crypto::public_key public_key;
        swarm_id_t swarm_id;
    };

    uint64_t get_new_swarm_id(const swarm_mnode_map_t& swarm_to_mnodes);

    void calc_swarm_changes(swarm_mnode_map_t& swarm_to_mnodes, uint64_t seed);

#ifdef UNIT_TEST
    size_t calc_excess(const swarm_mnode_map_t &swarm_to_mnodes);
    size_t calc_threshold(const swarm_mnode_map_t &swarm_to_mnodes);
    crypto::public_key steal_from_excess_pool(swarm_mnode_map_t &swarm_to_mnodes, std::mt19937_64 &mt);
    void create_new_swarm_from_excess(swarm_mnode_map_t &swarm_to_mnodes, std::mt19937_64 &mt);
    void calc_swarm_sizes(const swarm_mnode_map_t &swarm_to_mnodes, std::vector<swarm_size> &sorted_swarm_sizes);
    void assign_mnodes(const std::vector<crypto::public_key> &mnode_pubkeys, swarm_mnode_map_t &swarm_to_mnodes, std::mt19937_64 &mt, size_t percentile);
    void get_excess_pool(size_t threshold, const swarm_mnode_map_t& swarm_to_mnodes, std::vector<excess_pool_mnode>& pool_mnodes, size_t& excess);
    const excess_pool_mnode& pick_from_excess_pool(const std::vector<excess_pool_mnode>& excess_pool, std::mt19937_64 &mt);
    void remove_excess_mnode_from_swarm(const excess_pool_mnode& excess_mnode, swarm_mnode_map_t &swarm_to_mnodes);
#endif
}
