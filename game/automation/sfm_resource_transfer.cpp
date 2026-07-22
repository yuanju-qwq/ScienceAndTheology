// SFM resource-transfer primitive implementation.

#define SNT_LOG_CHANNEL "game.automation.sfm"
#include "game/automation/sfm_resource_transfer.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] ResourceStack stack_with_amount(const ResourceKey& key, int64_t amount) {
    if (amount <= 0) return {};
    return {.key = key, .amount = amount};
}

}  // namespace

snt::core::Expected<SfmResourceTransferResult> SfmResourceTransfer::transfer(
    IResourceStorage& source,
    IResourceStorage& destination,
    const ResourceKeyContext& context,
    const ResourceStack& requested,
    ResourceTransferMode mode) {
    if (!context.is_valid() || !requested.is_valid()) {
        return invalid_argument("SFM resource transfer requires a valid context and stack");
    }
    if (!source.key_context().matches(context) || !destination.key_context().matches(context)) {
        return invalid_state("SFM resource transfer endpoints do not share the requested snapshot");
    }

    SfmResourceTransferResult result{.requested = requested};
    if (&source == &destination) return result;

    const int64_t source_available = std::clamp(
        source.extract(context, requested, ResourceTransferMode::kSimulate),
        int64_t{0}, requested.amount);
    const ResourceStack source_stack = stack_with_amount(requested.key, source_available);
    if (source_stack.is_absent()) return result;

    const int64_t destination_accepted = std::clamp(
        destination.insert(context, source_stack, ResourceTransferMode::kSimulate),
        int64_t{0}, source_stack.amount);
    result.transferable = stack_with_amount(requested.key, destination_accepted);
    if (result.transferable.is_absent() || mode == ResourceTransferMode::kSimulate) {
        return result;
    }

    const int64_t extracted = source.extract(
        context, result.transferable, ResourceTransferMode::kExecute);
    if (extracted != result.transferable.amount) {
        const ResourceStack compensation = stack_with_amount(requested.key, extracted);
        const int64_t restored = source.insert(
            context, compensation, ResourceTransferMode::kExecute);
        if (restored != compensation.amount) {
            SNT_LOG_ERROR(
                "SFM source transfer divergence could not be compensated: "
                "kind=%u runtime_id=%u variant=%u expected_restore=%lld restored=%lld",
                static_cast<unsigned int>(requested.key.kind),
                static_cast<unsigned int>(requested.key.runtime_id),
                static_cast<unsigned int>(requested.key.variant),
                static_cast<long long>(compensation.amount),
                static_cast<long long>(restored));
            return invalid_state("SFM source diverged and source compensation failed");
        }
        SNT_LOG_WARN(
            "SFM source transfer diverged after simulation; restored=%lld",
            static_cast<long long>(compensation.amount));
        return result;
    }

    const int64_t inserted = destination.insert(
        context, result.transferable, ResourceTransferMode::kExecute);
    if (inserted == result.transferable.amount) {
        result.transferred = result.transferable;
        return result;
    }

    const int64_t clamped_inserted = std::clamp(inserted, int64_t{0}, result.transferable.amount);
    const ResourceStack compensation = stack_with_amount(
        requested.key, result.transferable.amount - clamped_inserted);
    const int64_t restored = source.insert(
        context, compensation, ResourceTransferMode::kExecute);
    if (restored != compensation.amount) {
        SNT_LOG_ERROR(
            "SFM resource transfer compensation failed: kind=%u runtime_id=%u variant=%u "
            "expected_restore=%lld restored=%lld",
            static_cast<unsigned int>(requested.key.kind),
            static_cast<unsigned int>(requested.key.runtime_id),
            static_cast<unsigned int>(requested.key.variant),
            static_cast<long long>(compensation.amount),
            static_cast<long long>(restored));
        return invalid_state("SFM destination diverged and source compensation failed");
    }
    result.transferred = stack_with_amount(requested.key, clamped_inserted);
    SNT_LOG_WARN(
        "SFM destination transfer diverged after simulation; transferred=%lld restored=%lld",
        static_cast<long long>(result.transferred.amount),
        static_cast<long long>(compensation.amount));
    return result;
}

}  // namespace snt::game
