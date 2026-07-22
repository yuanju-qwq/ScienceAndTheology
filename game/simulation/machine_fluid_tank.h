// Machine fluid-tank persistence and runtime contracts.
//
// MachineFluidTankRecord crosses save/load and therefore stores a stable
// ResourceContentStack. MachineFluidTank is the live value type: it stores a
// compact ResourceStack bound to one immutable ResourceRuntimeIndex snapshot.
// Both retain only pure fluids and local thermodynamic state. Terrain-fluid
// mixtures remain owned by GameFluidSystem.

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

// Durable tank state owned by a MachineRuntimePersistenceRecord.
struct MachineFluidTankRecord {
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

// Live tank state held by a MachineRuntimeComponent and offline runtime work
// set. Empty tanks retain their physical values so a transfer can copy the
// source state into a preconditioned destination without inventing a mixed
// resource identity.
struct MachineFluidTank {
    static constexpr int16_t kDefaultTemperatureKelvin =
        MachineFluidTankRecord::kDefaultTemperatureKelvin;
    static constexpr int32_t kDefaultPressurePascal =
        MachineFluidTankRecord::kDefaultPressurePascal;

    ResourceStack fluid;
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
        return fluid.is_valid() && fluid.amount <= capacity_millibuckets;
    }
};

}  // namespace snt::game
