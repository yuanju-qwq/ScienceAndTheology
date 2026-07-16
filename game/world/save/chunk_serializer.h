#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/world/game_chunk_sidecar_serializer.h"

namespace snt::game {

// Binary serializer / deserializer for one ScienceAndTheology GameChunk.
//
// The reader accepts only kCurrentVersion. Development saves are regenerated
// when the layout changes instead of carrying migration branches in Runtime.
// All multi-byte integers are stored in host byte order (little-endian).
//
// Thread-safe: all methods are stateless and reentrant.
class GameChunkSerializer final : public IGameChunkSidecarSerializer {
public:
    // Current binary format version. v12 adds persistent player bed anchors
    // and indestructible grave storage to game-owned chunk sidecars.
    static constexpr uint8_t kCurrentVersion = 12;

    std::vector<uint8_t> serialize(
        const std::string& dimension_id, const GameChunk& chunk) const override;

    // Rejects every non-current version and trailing bytes.
    bool deserialize(
        const std::vector<uint8_t>& data,
        std::string& dimension_id, GameChunk& chunk) const override;

private:
    // --- Write helpers ---

    static void write_uint8(std::vector<uint8_t>& buf, uint8_t value);
    static void write_int32(std::vector<uint8_t>& buf, int32_t value);
    static void write_uint32(std::vector<uint8_t>& buf, uint32_t value);
    static void write_int64(std::vector<uint8_t>& buf, int64_t value);
    static void write_uint64(std::vector<uint8_t>& buf, uint64_t value);
    static void write_float(std::vector<uint8_t>& buf, float value);
    static void write_string(std::vector<uint8_t>& buf,
                             const std::string& str);
    static void write_bytes(std::vector<uint8_t>& buf,
                            const uint8_t* data, size_t len);

    // --- Read helpers ---

    static bool read_uint8(const std::vector<uint8_t>& data,
                           size_t& offset, uint8_t& out);
    static bool read_int32(const std::vector<uint8_t>& data,
                           size_t& offset, int32_t& out);
    static bool read_uint32(const std::vector<uint8_t>& data,
                            size_t& offset, uint32_t& out);
    static bool read_int64(const std::vector<uint8_t>& data,
                           size_t& offset, int64_t& out);
    static bool read_uint64(const std::vector<uint8_t>& data,
                            size_t& offset, uint64_t& out);
    static bool read_float(const std::vector<uint8_t>& data,
                           size_t& offset, float& out);
    static bool read_string(const std::vector<uint8_t>& data,
                            size_t& offset, std::string& out);
    static bool read_bytes(const std::vector<uint8_t>& data,
                           size_t& offset, uint8_t* out, size_t len);

    // --- Connector serialization ---

    static void write_connector(std::vector<uint8_t>& buf,
                                 const ConnectorPlacement& conn);
    static bool read_connector(const std::vector<uint8_t>& data,
                               size_t& offset, ConnectorPlacement& conn);

    // --- Mechanism serialization ---

    static void write_mechanism(std::vector<uint8_t>& buf,
                                const MechanismPlacement& mechanism);
    static bool read_mechanism(const std::vector<uint8_t>& data,
                               size_t& offset, MechanismPlacement& mechanism);
    static void write_mechanism_effect(
        std::vector<uint8_t>& buf,
        const MechanismEffectPlacement& effect);
    static bool read_mechanism_effect(
        const std::vector<uint8_t>& data,
        size_t& offset,
        MechanismEffectPlacement& effect);

    // --- Block entity serialization ---

    static void write_block_entity(std::vector<uint8_t>& buf,
                                    const BlockEntityPlacement& entity);
    static bool read_block_entity(const std::vector<uint8_t>& data,
                                  size_t& offset,
                                  BlockEntityPlacement& entity);

    // --- Player bed/grave sidecar serialization ---

    static void write_player_bed_record(
        std::vector<uint8_t>& buf,
        const GamePlayerBedRecord& record);
    static bool read_player_bed_record(
        const std::vector<uint8_t>& data,
        size_t& offset,
        GamePlayerBedRecord& record);
    static void write_player_grave_record(
        std::vector<uint8_t>& buf,
        const GamePlayerGraveRecord& record);
    static bool read_player_grave_record(
        const std::vector<uint8_t>& data,
        size_t& offset,
        GamePlayerGraveRecord& record);
    static void write_player_grave_item_stack(
        std::vector<uint8_t>& buf,
        const GamePlayerGraveItemStack& stack);
    static bool read_player_grave_item_stack(
        const std::vector<uint8_t>& data,
        size_t& offset,
        GamePlayerGraveItemStack& stack);

    // --- Machine runtime sidecar serialization ---

    static void write_machine_runtime_record(
        std::vector<uint8_t>& buf,
        const MachineRuntimePersistenceRecord& record);
    static bool read_machine_runtime_record(
        const std::vector<uint8_t>& data,
        size_t& offset,
        MachineRuntimePersistenceRecord& record);
    static void write_machine_runtime_item_stack(
        std::vector<uint8_t>& buf,
        const MachineRuntimeItemStack& stack);
    static bool read_machine_runtime_item_stack(
        const std::vector<uint8_t>& data,
        size_t& offset,
        MachineRuntimeItemStack& stack);
    static void write_machine_runtime_recipe_snapshot(
        std::vector<uint8_t>& buf,
        const MachineRuntimeRecipeSnapshot& recipe);
    static bool read_machine_runtime_recipe_snapshot(
        const std::vector<uint8_t>& data,
        size_t& offset,
        MachineRuntimeRecipeSnapshot& recipe);

    // --- Population cell serialization ---

    static void write_population_cell(std::vector<uint8_t>& buf,
                                       const PopulationCell& cell);
    static bool read_population_cell(const std::vector<uint8_t>& data,
                                     size_t& offset,
                                     PopulationCell& cell);

    // --- Captive creature serialization ---

    static void write_captive_creature(std::vector<uint8_t>& buf,
                                       const CaptiveCreature& cc);
    static bool read_captive_creature(const std::vector<uint8_t>& data,
                                      size_t& offset,
                                      CaptiveCreature& cc);
};

} // namespace snt::game
