// Durable machine fluid-tank value contract.
//
// Machine tanks carry only pure content-addressed fluids and their local
// thermodynamic state. Terrain-fluid mixtures remain owned by GameFluidSystem;
// recipes and future process chemistry may layer richer input/output rules on
// top of this value-only storage without changing chunk ownership.

#pragma once

#include "game/resources/resource_key.h"

#include <cstdint>

namespace snt::game {

enum class MachineFluidTransport : uint8_t {
    kLiquid = 0,
    kGas = 1,
};

enum class MachineFluidTankAccess : uint8_t {
    kInput = 0,
    kOutput = 1,
    kBuffer = 2,
};

// A tank is empty only when its resource stack is absent. Empty tanks retain
// their physical values so a machine can precondition its next accepted
// fluid; a transfer copies the source state into an empty destination.
struct MachineFluidTank {
    static constexpr int16_t kDefaultTemperatureKelvin = 300;
    static constexpr int32_t kDefaultPressurePascal = 101'325;

    ResourceContentStack fluid;
    int64_t capacity_millibuckets = 0;
    int16_t temperature_kelvin = kDefaultTemperatureKelvin;
    int32_t pressure_pascal = kDefaultPressurePascal;
    MachineFluidTransport transport = MachineFluidTransport::kLiquid;
    MachineFluidTankAccess access = MachineFluidTankAccess::kBuffer;

    [[nodiscard]] bool is_empty() const noexcept { return fluid.is_absent(); }
    [[nodiscard]] bool accepts_input() const noexcept {
        return access == MachineFluidTankAccess::kInput ||
               access == MachineFluidTankAccess::kBuffer;
    }
    [[nodiscard]] bool allows_output() const noexcept {
        return access == MachineFluidTankAccess::kOutput ||
               access == MachineFluidTankAccess::kBuffer;
    }
    [[nodiscard]] bool is_valid() const noexcept {
        const uint8_t transport_value = static_cast<uint8_t>(transport);
        const uint8_t access_value = static_cast<uint8_t>(access);
        if (capacity_millibuckets <= 0 || temperature_kelvin <= 0 ||
            pressure_pascal < 0 ||
            transport_value > static_cast<uint8_t>(MachineFluidTransport::kGas) ||
            access_value > static_cast<uint8_t>(MachineFluidTankAccess::kBuffer)) {
            return false;
        }
        if (fluid.is_absent()) return true;
        return fluid.is_valid() && fluid.is_fluid() &&
               fluid.amount <= capacity_millibuckets;
    }
};

}  // namespace snt::game
