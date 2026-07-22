// Source-law graph source representation shared by content and player state.
//
// A graph only names content-defined nodes and ports. It is not an executable
// skill and remains safe to persist or inspect before a body can compile it.

#pragma once

#include "game/source_law/source_law_types.h"

#include <cstdint>
#include <vector>

namespace snt::game::source_law {

struct SourceLawSpellNode {
    uint32_t stable_node_id = 0;
    SourceLawSpellNodeKind kind = SourceLawSpellNodeKind::kCount;
    SourceLawId definition_id;
    std::vector<SourceLawId> parameter_ids;

    friend bool operator==(const SourceLawSpellNode&, const SourceLawSpellNode&) = default;
};

struct SourceLawSpellLink {
    uint32_t from_node_id = 0;
    SourceLawId from_port_id;
    uint32_t to_node_id = 0;
    SourceLawId to_port_id;

    friend bool operator==(const SourceLawSpellLink&, const SourceLawSpellLink&) = default;
};

struct SourceLawSpellGraph {
    SourceLawSpellGraphKind kind = SourceLawSpellGraphKind::kPlayerAuthored;
    std::vector<SourceLawSpellNode> nodes;
    std::vector<SourceLawSpellLink> links;
    std::vector<SourceLawId> required_path_core_ids;
    std::vector<SourceLawId> requested_hybrid_link_ids;
    std::vector<SourceLawId> declared_primary_system_ids;
    std::vector<SourceLawId> declared_coordinating_system_ids;
    uint16_t declared_max_control_steps = 0;

    friend bool operator==(const SourceLawSpellGraph&, const SourceLawSpellGraph&) = default;
};

}  // namespace snt::game::source_law
