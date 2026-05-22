#pragma once

#include <cstdint>

namespace science_and_theology::gt {

// Type of pipe network. Determines what can be transported.
// Used by both FluidNetwork (LIQUID/GAS) and ItemPipeNetwork (ITEM).
enum class PipeType : uint8_t {
    LIQUID = 0,
    GAS = 1,
    ITEM = 2,
};

// Default throughput values per pipe type (items or mB per tick).
inline constexpr int64_t kDefaultThroughput(PipeType type) {
    switch (type) {
        case PipeType::LIQUID: return 100;   // 100 mB/tick
        case PipeType::GAS:    return 200;   // 200 mB/tick (gases flow faster)
        case PipeType::ITEM:   return 1;     // 1 stack per tick
    }
    return 100;
}

} // namespace science_and_theology::gt