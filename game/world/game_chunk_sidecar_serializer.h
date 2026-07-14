// Game payload serializer boundary.
//
// The implementation owns the current ScienceAndTheology payload schema.
// Engine storage only persists the returned bytes through IVoxelWorldStorage.

#pragma once

#include "game/world/game_chunk.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::game {

class IGameChunkSidecarSerializer {
public:
    virtual ~IGameChunkSidecarSerializer() = default;

    virtual std::vector<uint8_t> serialize(const std::string& dimension_id,
                                           const GameChunk& chunk) const = 0;
    virtual bool deserialize(const std::vector<uint8_t>& payload,
                             std::string& out_dimension_id,
                             GameChunk& out_chunk) const = 0;
};

}  // namespace snt::game
