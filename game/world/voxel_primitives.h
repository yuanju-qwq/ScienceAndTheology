// Game-facing imports of engine-owned generic voxel primitives.
//
// This is the only bridge from ScienceAndTheology world rules to the engine
// voxel data layer. It intentionally imports storage primitives, not gameplay
// definitions or serializers.

#pragma once

#include "voxel/data/chunk_registry.h"
#include "voxel/data/terrain_data.h"
#include "voxel/data/voxel_chunk.h"

namespace snt::game {

using snt::voxel::CellFluidId;
using snt::voxel::ChunkKey;
using snt::voxel::ChunkRegistry;
using snt::voxel::ChunkState;
using snt::voxel::TerrainCell;
using snt::voxel::TerrainData;
using snt::voxel::TerrainFlags;
using snt::voxel::TerrainMaterial;
using snt::voxel::TerrainMaterialId;
using snt::voxel::VoxelChunk;

using snt::voxel::TF_CLIMBABLE;
using snt::voxel::TF_COLLAPSE_RISK;
using snt::voxel::TF_GRAVITY_FALL;
using snt::voxel::TF_INDESTRUCTIBLE;
using snt::voxel::TF_LIQUID;
using snt::voxel::TF_MINEABLE;
using snt::voxel::TF_SOLID;
using snt::voxel::TF_SUPPORT_BEAM;
using snt::voxel::TF_WALKABLE;

}  // namespace snt::game
