// AE pattern-provider dispatch implementation.

#define SNT_LOG_CHANNEL "game.ae_pattern_provider"
#include "game/automation/ae_pattern_provider.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

namespace snt::game {
namespace {

constexpr size_t kMaxProviderKeyBytes = 256;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool valid_identifier(std::string_view value, size_t maximum) noexcept {
    return !value.empty() && value.size() <= maximum && value.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool resource_key_less(const ResourceKey& left, const ResourceKey& right) noexcept {
    if (left.kind != right.kind) return left.kind < right.kind;
    if (left.runtime_id != right.runtime_id) return left.runtime_id < right.runtime_id;
    return left.variant < right.variant;
}

[[nodiscard]] bool same_stacks(std::vector<ResourceStack> left,
                               std::vector<ResourceStack> right) {
    const auto less = [](const ResourceStack& first, const ResourceStack& second) {
        return resource_key_less(first.key, second.key);
    };
    std::sort(left.begin(), left.end(), less);
    std::sort(right.begin(), right.end(), less);
    return left == right;
}

[[nodiscard]] snt::core::Expected<void> restore_extracted_inputs(
    IAeAutocraftingResourceAccess& resources,
    const ResourceKeyContext& context,
    const std::vector<ResourceStack>& extracted) {
    for (auto found = extracted.rbegin(); found != extracted.rend(); ++found) {
        if (resources.insert(context, *found, ResourceTransferMode::kExecute) != found->amount) {
            return invalid_state("AE pattern provider could not restore extracted input");
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> rollback_inserted_outputs(
    IAeAutocraftingResourceAccess& resources,
    const ResourceKeyContext& context,
    const std::vector<ResourceStack>& inserted) {
    for (auto found = inserted.rbegin(); found != inserted.rend(); ++found) {
        if (resources.extract(context, *found, ResourceTransferMode::kExecute) != found->amount) {
            return invalid_state("AE pattern provider could not roll back delivered output");
        }
    }
    return {};
}

[[nodiscard]] int32_t combined_priority(int32_t provider_priority,
                                        int32_t pattern_priority) noexcept {
    const int64_t value = static_cast<int64_t>(provider_priority) +
        static_cast<int64_t>(pattern_priority);
    return static_cast<int32_t>(std::clamp<int64_t>(
        value, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

}  // namespace

struct AePatternProviderService::ProviderSlot {
    bool occupied = false;
    uint32_t generation = 1;
    AePatternProviderRegistration registration;
};

struct AePatternProviderService::ScopeCatalog {
    struct Route {
        AePatternProviderHandle provider;
        std::string provider_key;
        std::string pattern_id;
        std::vector<ResourceStack> inputs;
        std::vector<ResourceStack> outputs;
    };

    std::unique_ptr<AeAutocraftingService> planner;
    std::unordered_map<std::string, Route> routes;
};

struct AePatternProviderService::Job {
    struct PendingOperation {
        AePatternProviderHandle provider;
        AePatternProviderOperationHandle operation;
        std::string provider_key;
        std::string pattern_id;
        std::vector<ResourceStack> expected_outputs;
    };

    ResourceContentStack requested;
    uint64_t scope_id = 0;
    std::vector<AeAutocraftingPlanStep> steps;
    AePatternProviderJobState state = AePatternProviderJobState::kQueued;
    size_t next_step_index = 0;
    uint64_t remaining_operations_in_step = 0;
    uint64_t completed_operations = 0;
    std::optional<PendingOperation> pending_operation;
    bool cancel_after_pending_operation = false;
};

struct AePatternProviderService::JobSlot {
    bool occupied = false;
    uint32_t generation = 1;
    Job job;
};

struct AePatternProviderService::PendingResourceRuntimeSnapshot {
    ResourceRuntimeIndex::Snapshot resource_runtime_index;
    std::unordered_map<uint64_t, std::unique_ptr<ScopeCatalog>> scope_catalogs;
};

AePatternProviderService::AePatternProviderService(
    ResourceRuntimeIndex::Snapshot resource_runtime_index)
    : resource_runtime_index_(std::move(resource_runtime_index)) {
    providers_.emplace_back(std::make_unique<ProviderSlot>());
    jobs_.emplace_back(std::make_unique<JobSlot>());
}

AePatternProviderService::~AePatternProviderService() = default;

snt::core::Expected<void> AePatternProviderService::validate_registration(
    const AePatternProviderRegistration& registration) const {
    if (!resource_runtime_index_.key_context().is_valid()) {
        return invalid_state("AE pattern provider service has no valid resource runtime snapshot");
    }
    if (!valid_identifier(registration.provider_key, kMaxProviderKeyBytes) ||
        registration.scope_id == 0 || registration.endpoint == nullptr ||
        registration.patterns.empty()) {
        return invalid_argument("AE pattern provider registration is incomplete");
    }
    std::unordered_set<std::string> ids;
    ids.reserve(registration.patterns.size());
    for (const AeAutocraftingPatternDefinition& pattern : registration.patterns) {
        if (!valid_identifier(pattern.id, kMaxProviderKeyBytes) || pattern.inputs.empty() ||
            pattern.outputs.empty() || pattern.ticks_per_operation == 0 ||
            !ids.insert(pattern.id).second) {
            return invalid_argument("AE pattern provider registration contains an invalid pattern");
        }
    }
    return {};
}

snt::core::Expected<AePatternProviderHandle> AePatternProviderService::register_provider(
    AePatternProviderRegistration registration) {
    if (auto valid = validate_registration(registration); !valid) return valid.error();
    for (size_t index = 1; index < providers_.size(); ++index) {
        const ProviderSlot& existing = *providers_[index];
        if (existing.occupied && existing.registration.provider_key == registration.provider_key) {
            return invalid_argument("AE pattern provider key is already registered: " +
                                    registration.provider_key);
        }
    }

    uint32_t slot_index = 0;
    if (!reusable_provider_slots_.empty()) {
        slot_index = reusable_provider_slots_.back();
        reusable_provider_slots_.pop_back();
    } else {
        if (providers_.size() > std::numeric_limits<uint32_t>::max()) {
            return invalid_state("AE pattern provider handle slots are exhausted");
        }
        slot_index = static_cast<uint32_t>(providers_.size());
        providers_.emplace_back(std::make_unique<ProviderSlot>());
    }
    ProviderSlot& slot = *providers_[slot_index];
    if (slot.occupied || slot.generation == 0) {
        return invalid_state("AE pattern provider encountered an unavailable handle slot");
    }
    const uint64_t scope_id = registration.scope_id;
    slot.occupied = true;
    slot.registration = std::move(registration);
    if (auto rebuilt = rebuild_scope(scope_id); !rebuilt) {
        slot.occupied = false;
        slot.registration = {};
        reusable_provider_slots_.push_back(slot_index);
        return rebuilt.error();
    }
    const AePatternProviderHandle handle{.slot = slot_index, .generation = slot.generation};
    SNT_LOG_INFO("Registered AE pattern provider slot=%u scope=%llu patterns=%zu",
                 static_cast<unsigned int>(handle.slot),
                 static_cast<unsigned long long>(scope_id),
                 slot.registration.patterns.size());
    return handle;
}

bool AePatternProviderService::unregister_provider(AePatternProviderHandle handle) noexcept {
    ProviderSlot* const slot = find_provider(handle);
    if (slot == nullptr) return false;
    for (size_t index = 1; index < jobs_.size(); ++index) {
        const JobSlot& job_slot = *jobs_[index];
        if (job_slot.occupied && job_slot.job.pending_operation &&
            job_slot.job.pending_operation->provider == handle) {
            return false;
        }
    }
    const uint64_t scope_id = slot->registration.scope_id;
    slot->occupied = false;
    slot->registration = {};
    if (slot->generation != std::numeric_limits<uint32_t>::max()) {
        ++slot->generation;
        reusable_provider_slots_.push_back(handle.slot);
    }
    const auto rebuilt = rebuild_scope(scope_id);
    if (!rebuilt) {
        SNT_LOG_ERROR("Could not rebuild AE pattern provider scope after unregister: %s",
                      rebuilt.error().format().c_str());
    }
    cancel_jobs_for_scope_rebuild(scope_id);
    return true;
}

snt::core::Expected<void> AePatternProviderService::update_provider_patterns(
    AePatternProviderHandle handle,
    std::vector<AeAutocraftingPatternDefinition> patterns) {
    ProviderSlot* const slot = find_provider(handle);
    if (slot == nullptr) return invalid_state("AE pattern provider handle is stale");
    AePatternProviderRegistration candidate = slot->registration;
    candidate.patterns = std::move(patterns);
    if (auto valid = validate_registration(candidate); !valid) return valid.error();

    const uint64_t scope_id = slot->registration.scope_id;
    std::vector<AeAutocraftingPatternDefinition> previous =
        std::move(slot->registration.patterns);
    slot->registration.patterns = std::move(candidate.patterns);
    if (auto rebuilt = rebuild_scope(scope_id); !rebuilt) {
        slot->registration.patterns = std::move(previous);
        static_cast<void>(rebuild_scope(scope_id));
        return rebuilt.error();
    }
    cancel_jobs_for_scope_rebuild(scope_id);
    SNT_LOG_INFO("Updated AE pattern provider slot=%u patterns=%zu scope=%llu",
                 static_cast<unsigned int>(handle.slot), slot->registration.patterns.size(),
                 static_cast<unsigned long long>(scope_id));
    return {};
}

snt::core::Expected<void> AePatternProviderService::update_provider_scope(
    AePatternProviderHandle handle, uint64_t scope_id) {
    if (scope_id == 0) return invalid_argument("AE pattern provider scope must be non-zero");
    ProviderSlot* const slot = find_provider(handle);
    if (slot == nullptr) return invalid_state("AE pattern provider handle is stale");
    if (slot->registration.scope_id == scope_id) return {};
    for (size_t index = 1; index < jobs_.size(); ++index) {
        const JobSlot& job_slot = *jobs_[index];
        if (job_slot.occupied && job_slot.job.pending_operation &&
            job_slot.job.pending_operation->provider == handle) {
            return invalid_state("AE pattern provider cannot move while it owns an operation");
        }
    }
    const uint64_t previous_scope = slot->registration.scope_id;
    slot->registration.scope_id = scope_id;
    if (auto rebuilt = rebuild_scope(previous_scope); !rebuilt) {
        slot->registration.scope_id = previous_scope;
        return rebuilt.error();
    }
    if (auto rebuilt = rebuild_scope(scope_id); !rebuilt) {
        slot->registration.scope_id = previous_scope;
        static_cast<void>(rebuild_scope(previous_scope));
        return rebuilt.error();
    }
    cancel_jobs_for_scope_rebuild(previous_scope);
    SNT_LOG_INFO("Moved AE pattern provider slot=%u scope=%llu -> %llu",
                 static_cast<unsigned int>(handle.slot),
                 static_cast<unsigned long long>(previous_scope),
                 static_cast<unsigned long long>(scope_id));
    return {};
}

snt::core::Expected<void> AePatternProviderService::rebuild_scope(uint64_t scope_id) {
    if (scope_id == 0) return invalid_argument("AE pattern provider scope must be non-zero");
    std::vector<AeAutocraftingPatternDefinition> definitions;
    auto next = std::make_unique<ScopeCatalog>();
    next->planner = std::make_unique<AeAutocraftingService>(resource_runtime_index_);

    const ResourceRuntimeIndex::Snapshot snapshot = resource_runtime_index_;
    for (size_t slot_index = 1; slot_index < providers_.size(); ++slot_index) {
        const ProviderSlot& provider = *providers_[slot_index];
        if (!provider.occupied || provider.registration.scope_id != scope_id) continue;
        for (size_t pattern_index = 0; pattern_index < provider.registration.patterns.size();
             ++pattern_index) {
            const AeAutocraftingPatternDefinition& source =
                provider.registration.patterns[pattern_index];
            const std::string route_id = "_ae_provider/" + std::to_string(slot_index) + "/" +
                std::to_string(provider.generation) + "/" + std::to_string(pattern_index);
            AeAutocraftingPatternDefinition definition = source;
            definition.id = route_id;
            definition.provider_priority = combined_priority(
                provider.registration.priority, source.provider_priority);
            definitions.push_back(std::move(definition));

            ScopeCatalog::Route route{
                .provider = {.slot = static_cast<uint32_t>(slot_index),
                             .generation = provider.generation},
                .provider_key = provider.registration.provider_key,
                .pattern_id = source.id,
            };
            const auto compile_stacks = [&snapshot](
                                            const std::vector<ResourceContentStack>& source_stacks,
                                            std::vector<ResourceStack>& destination,
                                            std::string_view label) -> snt::core::Expected<void> {
                destination.reserve(source_stacks.size());
                std::unordered_set<ResourceKey, ResourceKey::Hash> seen;
                seen.reserve(source_stacks.size());
                for (const ResourceContentStack& content_stack : source_stacks) {
                    const auto compact = resolve_resource_stack(content_stack, snapshot);
                    if (!compact || !compact->is_valid()) {
                        return invalid_state("AE pattern provider cannot resolve " +
                                             std::string(label) + " resource");
                    }
                    if (!seen.insert(compact->key).second) {
                        return invalid_argument("AE pattern provider pattern has duplicate " +
                                                std::string(label) + " resources");
                    }
                    destination.push_back(*compact);
                }
                std::sort(destination.begin(), destination.end(),
                          [](const ResourceStack& left, const ResourceStack& right) {
                              return resource_key_less(left.key, right.key);
                          });
                return {};
            };
            if (auto inputs = compile_stacks(source.inputs, route.inputs, "input"); !inputs) {
                return inputs.error();
            }
            if (auto outputs = compile_stacks(source.outputs, route.outputs, "output"); !outputs) {
                return outputs.error();
            }
            if (!next->routes.emplace(route_id, std::move(route)).second) {
                return invalid_state("AE pattern provider generated a duplicate internal route");
            }
        }
    }
    if (definitions.empty()) {
        scope_catalogs_.erase(scope_id);
        return {};
    }
    if (auto published = next->planner->replace_patterns(std::move(definitions)); !published) {
        return published.error();
    }
    scope_catalogs_[scope_id] = std::move(next);
    return {};
}

snt::core::Expected<void> AePatternProviderService::rebuild_all_scopes() {
    std::set<uint64_t> scopes;
    for (size_t index = 1; index < providers_.size(); ++index) {
        const ProviderSlot& slot = *providers_[index];
        if (slot.occupied) scopes.insert(slot.registration.scope_id);
    }
    scope_catalogs_.clear();
    for (const uint64_t scope_id : scopes) {
        if (auto rebuilt = rebuild_scope(scope_id); !rebuilt) return rebuilt.error();
    }
    return {};
}

snt::core::Expected<AePatternProviderPlan> AePatternProviderService::plan(
    IAeAutocraftingResourceAccess& resources,
    uint64_t scope_id,
    ResourceContentStack requested) const {
    if (!resources.key_context().matches(resource_runtime_index_.key_context())) {
        return invalid_state("AE pattern provider resource owner belongs to a different snapshot");
    }
    const auto found = scope_catalogs_.find(scope_id);
    if (scope_id == 0 || found == scope_catalogs_.end() || !found->second->planner) {
        return invalid_state("AE component has no active pattern provider catalog");
    }
    auto planned = found->second->planner->plan(resources, std::move(requested));
    if (!planned) return planned.error();
    AePatternProviderPlan result{.requested = std::move(planned->requested)};
    result.steps.reserve(planned->steps.size());
    for (const AeAutocraftingPlanStep& step : planned->steps) {
        const auto route = found->second->routes.find(step.pattern_id);
        if (route == found->second->routes.end()) {
            return invalid_state("AE pattern planner selected a missing provider route");
        }
        result.steps.push_back({
            .provider_key = route->second.provider_key,
            .pattern_id = route->second.pattern_id,
            .operations = step.operations,
        });
    }
    return result;
}

snt::core::Expected<AePatternProviderJobHandle> AePatternProviderService::submit_job(
    IAeAutocraftingResourceAccess& resources,
    uint64_t scope_id,
    ResourceContentStack requested) {
    if (!resources.key_context().matches(resource_runtime_index_.key_context())) {
        return invalid_state("AE pattern provider resource owner belongs to a different snapshot");
    }
    const auto catalog = scope_catalogs_.find(scope_id);
    if (scope_id == 0 || catalog == scope_catalogs_.end() || !catalog->second->planner) {
        return invalid_state("AE component has no active pattern provider catalog");
    }
    auto planned = catalog->second->planner->plan(resources, requested);
    if (!planned) return planned.error();

    uint32_t slot_index = 0;
    if (!reusable_job_slots_.empty()) {
        slot_index = reusable_job_slots_.back();
        reusable_job_slots_.pop_back();
    } else {
        if (jobs_.size() > std::numeric_limits<uint32_t>::max()) {
            return invalid_state("AE pattern provider job handle slots are exhausted");
        }
        slot_index = static_cast<uint32_t>(jobs_.size());
        jobs_.emplace_back(std::make_unique<JobSlot>());
    }
    JobSlot& slot = *jobs_[slot_index];
    if (slot.occupied || slot.generation == 0) {
        return invalid_state("AE pattern provider encountered an unavailable job slot");
    }
    slot.occupied = true;
    slot.job = {
        .requested = std::move(planned->requested),
        .scope_id = scope_id,
        .steps = std::move(planned->steps),
    };
    if (slot.job.steps.empty()) {
        slot.job.state = AePatternProviderJobState::kCompleted;
    } else {
        slot.job.remaining_operations_in_step = slot.job.steps.front().operations;
    }
    const AePatternProviderJobHandle handle{.slot = slot_index, .generation = slot.generation};
    SNT_LOG_INFO("Submitted AE provider job slot=%u scope=%llu steps=%zu",
                 static_cast<unsigned int>(handle.slot),
                 static_cast<unsigned long long>(scope_id), slot.job.steps.size());
    return handle;
}

snt::core::Expected<AePatternProviderJobHandle> AePatternProviderService::adopt_operation(
    AePatternProviderHandle provider_handle,
    AePatternProviderOperationHandle operation,
    ResourceContentStack requested,
    std::string pattern_id,
    std::vector<ResourceStack> expected_outputs) {
    ProviderSlot* const provider = find_provider(provider_handle);
    if (provider == nullptr || !operation.is_valid() || !requested.is_valid() ||
        !valid_identifier(pattern_id, kMaxProviderKeyBytes) || expected_outputs.empty()) {
        return invalid_argument("AE pattern provider recovery operation is invalid");
    }
    std::unordered_set<ResourceKey, ResourceKey::Hash> output_keys;
    output_keys.reserve(expected_outputs.size());
    for (const ResourceStack& output : expected_outputs) {
        if (!output.is_valid() || !output_keys.insert(output.key).second) {
            return invalid_argument(
                "AE pattern provider recovery operation has invalid expected outputs");
        }
    }
    for (size_t index = 1; index < jobs_.size(); ++index) {
        const JobSlot& existing = *jobs_[index];
        if (existing.occupied && existing.job.pending_operation &&
            existing.job.pending_operation->provider == provider_handle &&
            existing.job.pending_operation->operation == operation) {
            return invalid_state("AE pattern provider operation is already adopted");
        }
    }

    uint32_t slot_index = 0;
    if (!reusable_job_slots_.empty()) {
        slot_index = reusable_job_slots_.back();
        reusable_job_slots_.pop_back();
    } else {
        if (jobs_.size() > std::numeric_limits<uint32_t>::max()) {
            return invalid_state("AE pattern provider job handle slots are exhausted");
        }
        slot_index = static_cast<uint32_t>(jobs_.size());
        jobs_.emplace_back(std::make_unique<JobSlot>());
    }
    JobSlot& slot = *jobs_[slot_index];
    if (slot.occupied || slot.generation == 0) {
        return invalid_state("AE pattern provider encountered an unavailable job slot");
    }
    slot.occupied = true;
    slot.job = {};
    slot.job.requested = std::move(requested);
    slot.job.scope_id = provider->registration.scope_id;
    slot.job.steps.push_back({
        .pattern_id = pattern_id,
        .operations = 1,
    });
    slot.job.state = AePatternProviderJobState::kWaitingForProvider;
    slot.job.remaining_operations_in_step = 1;
    slot.job.pending_operation = Job::PendingOperation{
        .provider = provider_handle,
        .operation = operation,
        .provider_key = provider->registration.provider_key,
        .pattern_id = std::move(pattern_id),
        .expected_outputs = std::move(expected_outputs),
    };
    const AePatternProviderJobHandle handle{.slot = slot_index, .generation = slot.generation};
    SNT_LOG_INFO("Adopted durable AE provider operation slot=%u provider=%s scope=%llu",
                 static_cast<unsigned int>(handle.slot),
                 provider->registration.provider_key.c_str(),
                 static_cast<unsigned long long>(provider->registration.scope_id));
    return handle;
}

snt::core::Expected<AePatternProviderJobSnapshot> AePatternProviderService::tick(
    AePatternProviderJobHandle handle,
    IAeAutocraftingResourceAccess& resources,
    uint32_t max_operations) {
    if (max_operations == 0) {
        return invalid_argument("AE pattern provider tick requires a positive operation budget");
    }
    JobSlot* const slot = find_job_slot(handle);
    if (slot == nullptr) return invalid_state("AE pattern provider job handle is stale");
    Job& job = slot->job;
    if (job.state == AePatternProviderJobState::kCompleted ||
        job.state == AePatternProviderJobState::kCancelledByContentReload ||
        job.state == AePatternProviderJobState::kFailed) {
        return snapshot_of(handle, job);
    }
    const ResourceKeyContext context = resource_runtime_index_.key_context();
    if (!resources.key_context().matches(context)) {
        return invalid_state("AE pattern provider resource owner belongs to a different snapshot");
    }

    uint32_t completed_this_tick = 0;
    while (completed_this_tick < max_operations) {
        if (job.pending_operation) {
            const Job::PendingOperation& pending = *job.pending_operation;
            ProviderSlot* const provider = find_provider(pending.provider);
            if (provider == nullptr || provider->registration.endpoint == nullptr) {
                job.state = AePatternProviderJobState::kFailed;
                return invalid_state("AE pattern provider disappeared during an active operation");
            }
            const AePatternProviderOperationState provider_state =
                provider->registration.endpoint->operation_state(pending.operation);
            if (provider_state == AePatternProviderOperationState::kRunning) {
                job.state = AePatternProviderJobState::kWaitingForProvider;
                break;
            }
            if (provider_state == AePatternProviderOperationState::kBlocked) {
                job.state = AePatternProviderJobState::kBlockedProvider;
                break;
            }
            if (provider_state == AePatternProviderOperationState::kFailed ||
                provider_state == AePatternProviderOperationState::kMissing) {
                job.state = AePatternProviderJobState::kFailed;
                return invalid_state("AE pattern provider could not complete an operation");
            }

            // A topology/content boundary must never move completed output
            // into a possibly different AE component. Keep it with the
            // provider machine and release only this dispatch job.
            if (job.cancel_after_pending_operation) {
                job.pending_operation.reset();
                job.state = AePatternProviderJobState::kCancelledByContentReload;
                break;
            }

            auto actual_outputs = provider->registration.endpoint->completed_outputs(
                pending.operation, context);
            if (!actual_outputs) {
                job.state = AePatternProviderJobState::kFailed;
                return actual_outputs.error();
            }
            if (!same_stacks(*actual_outputs, pending.expected_outputs)) {
                job.state = AePatternProviderJobState::kFailed;
                return invalid_state("AE pattern provider completed unexpected outputs");
            }
            for (const ResourceStack& output : *actual_outputs) {
                if (resources.insert(context, output, ResourceTransferMode::kSimulate) !=
                    output.amount) {
                    job.state = AePatternProviderJobState::kBlockedOutputCapacity;
                    return snapshot_of(handle, job);
                }
            }
            std::vector<ResourceStack> inserted;
            inserted.reserve(actual_outputs->size());
            for (const ResourceStack& output : *actual_outputs) {
                const int64_t accepted = resources.insert(context, output, ResourceTransferMode::kExecute);
                if (accepted != output.amount) {
                    if (accepted > 0) inserted.push_back({.key = output.key, .amount = accepted});
                    if (auto rollback = rollback_inserted_outputs(resources, context, inserted); !rollback) {
                        SNT_LOG_ERROR("AE provider output rollback failed: %s",
                                      rollback.error().format().c_str());
                    }
                    job.state = AePatternProviderJobState::kBlockedOutputCapacity;
                    return snapshot_of(handle, job);
                }
                inserted.push_back(output);
            }
            if (auto acknowledged = provider->registration.endpoint->acknowledge_completion(
                    pending.operation, context);
                !acknowledged) {
                if (auto rollback = rollback_inserted_outputs(resources, context, inserted); !rollback) {
                    SNT_LOG_ERROR("AE provider acknowledgement rollback failed: %s",
                                  rollback.error().format().c_str());
                }
                job.state = AePatternProviderJobState::kFailed;
                return acknowledged.error();
            }

            job.pending_operation.reset();
            ++job.completed_operations;
            ++completed_this_tick;
            if (job.remaining_operations_in_step == 0) {
                job.state = AePatternProviderJobState::kFailed;
                return invalid_state("AE provider job operation counter underflowed");
            }
            --job.remaining_operations_in_step;
            if (job.remaining_operations_in_step == 0) ++job.next_step_index;
            if (job.cancel_after_pending_operation) {
                job.state = AePatternProviderJobState::kCancelledByContentReload;
                break;
            }
            continue;
        }

        if (job.next_step_index >= job.steps.size()) {
            job.state = AePatternProviderJobState::kCompleted;
            break;
        }
        const auto catalog = scope_catalogs_.find(job.scope_id);
        if (catalog == scope_catalogs_.end()) {
            job.state = AePatternProviderJobState::kCancelledByContentReload;
            break;
        }
        AeAutocraftingPlanStep& step = job.steps[job.next_step_index];
        const auto route = catalog->second->routes.find(step.pattern_id);
        if (route == catalog->second->routes.end()) {
            job.state = AePatternProviderJobState::kFailed;
            return invalid_state("AE provider job route is no longer registered");
        }
        if (job.remaining_operations_in_step == 0) {
            job.remaining_operations_in_step = step.operations;
        }
        ProviderSlot* const provider = find_provider(route->second.provider);
        if (provider == nullptr || provider->registration.endpoint == nullptr) {
            job.state = AePatternProviderJobState::kBlockedProvider;
            break;
        }
        AePatternProviderOperationRequest request{
            .pattern_id = route->second.pattern_id,
            .inputs = route->second.inputs,
            .expected_outputs = route->second.outputs,
        };
        const AePatternProviderStartState start_state =
            provider->registration.endpoint->can_start_operation(request);
        if (start_state != AePatternProviderStartState::kReady) {
            job.state = AePatternProviderJobState::kBlockedProvider;
            break;
        }
        for (const ResourceStack& input : request.inputs) {
            if (resources.extract(context, input, ResourceTransferMode::kSimulate) != input.amount) {
                job.state = AePatternProviderJobState::kBlockedMissingInput;
                return snapshot_of(handle, job);
            }
        }
        std::vector<ResourceStack> extracted;
        extracted.reserve(request.inputs.size());
        for (const ResourceStack& input : request.inputs) {
            if (resources.extract(context, input, ResourceTransferMode::kExecute) != input.amount) {
                if (auto rollback = restore_extracted_inputs(resources, context, extracted); !rollback) {
                    SNT_LOG_ERROR("AE provider input rollback failed: %s",
                                  rollback.error().format().c_str());
                }
                job.state = AePatternProviderJobState::kBlockedMissingInput;
                return snapshot_of(handle, job);
            }
            extracted.push_back(input);
        }
        request.inputs = std::move(extracted);
        auto operation = provider->registration.endpoint->start_operation(std::move(request));
        if (!operation || !operation->is_valid()) {
            if (auto rollback = restore_extracted_inputs(
                    resources, context, route->second.inputs); !rollback) {
                SNT_LOG_ERROR("AE provider start rollback failed: %s",
                              rollback.error().format().c_str());
            }
            job.state = AePatternProviderJobState::kBlockedProvider;
            if (!operation) return operation.error();
            return snapshot_of(handle, job);
        }
        job.pending_operation = Job::PendingOperation{
            .provider = route->second.provider,
            .operation = *operation,
            .provider_key = route->second.provider_key,
            .pattern_id = route->second.pattern_id,
            .expected_outputs = route->second.outputs,
        };
        job.state = AePatternProviderJobState::kWaitingForProvider;
    }
    if (!job.pending_operation && job.next_step_index >= job.steps.size() &&
        job.state != AePatternProviderJobState::kCancelledByContentReload) {
        job.state = AePatternProviderJobState::kCompleted;
    }
    return snapshot_of(handle, job);
}

std::optional<AePatternProviderJobSnapshot> AePatternProviderService::find_job(
    AePatternProviderJobHandle handle) const noexcept {
    const JobSlot* const slot = find_job_slot(handle);
    return slot == nullptr ? std::nullopt : std::optional{snapshot_of(handle, slot->job)};
}

bool AePatternProviderService::remove_job(AePatternProviderJobHandle handle) noexcept {
    JobSlot* const slot = find_job_slot(handle);
    if (slot == nullptr || slot->job.pending_operation) return false;
    slot->occupied = false;
    slot->job = {};
    if (slot->generation != std::numeric_limits<uint32_t>::max()) {
        ++slot->generation;
        reusable_job_slots_.push_back(handle.slot);
    }
    return true;
}

void AePatternProviderService::cancel_jobs_for_scope(uint64_t scope_id) noexcept {
    if (scope_id == 0) return;
    cancel_jobs_for_scope_rebuild(scope_id);
}

snt::core::Expected<void> AePatternProviderService::prepare_resource_runtime_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    if (pending_resource_runtime_snapshot_) {
        return invalid_state("AE pattern provider resource snapshot preparation is already pending");
    }
    if (!next_snapshot.key_context().is_valid()) {
        return invalid_argument("AE pattern provider resource snapshot is invalid");
    }
    // Build all routes against the candidate snapshot without touching the
    // live catalogs.  Each planner remains isolated by component scope.
    PendingResourceRuntimeSnapshot pending{.resource_runtime_index = next_snapshot};
    std::set<uint64_t> scopes;
    for (size_t index = 1; index < providers_.size(); ++index) {
        const ProviderSlot& provider = *providers_[index];
        if (provider.occupied) scopes.insert(provider.registration.scope_id);
    }
    const ResourceRuntimeIndex::Snapshot previous_snapshot = resource_runtime_index_;
    resource_runtime_index_ = next_snapshot;
    const auto restore_snapshot = [this, &previous_snapshot]() {
        resource_runtime_index_ = previous_snapshot;
    };
    std::unordered_map<uint64_t, std::unique_ptr<ScopeCatalog>> previous_catalogs =
        std::move(scope_catalogs_);
    for (const uint64_t scope_id : scopes) {
        if (auto rebuilt = rebuild_scope(scope_id); !rebuilt) {
            scope_catalogs_ = std::move(previous_catalogs);
            restore_snapshot();
            return rebuilt.error();
        }
    }
    pending.scope_catalogs = std::move(scope_catalogs_);
    scope_catalogs_ = std::move(previous_catalogs);
    restore_snapshot();
    pending_resource_runtime_snapshot_ = std::make_unique<PendingResourceRuntimeSnapshot>(
        std::move(pending));
    return {};
}

void AePatternProviderService::commit_resource_runtime_snapshot() noexcept {
    if (!pending_resource_runtime_snapshot_) return;
    resource_runtime_index_ = std::move(pending_resource_runtime_snapshot_->resource_runtime_index);
    scope_catalogs_ = std::move(pending_resource_runtime_snapshot_->scope_catalogs);
    pending_resource_runtime_snapshot_.reset();
    cancel_jobs_for_resource_reload();
    SNT_LOG_INFO("Recompiled AE provider routes for resource snapshot generation=%llu",
                 static_cast<unsigned long long>(resource_runtime_index_.generation()));
}

void AePatternProviderService::cancel_resource_runtime_snapshot() noexcept {
    pending_resource_runtime_snapshot_.reset();
}

AePatternProviderService::ProviderSlot* AePatternProviderService::find_provider(
    AePatternProviderHandle handle) noexcept {
    if (!handle.is_valid() || handle.slot >= providers_.size()) return nullptr;
    ProviderSlot& slot = *providers_[handle.slot];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

const AePatternProviderService::ProviderSlot* AePatternProviderService::find_provider(
    AePatternProviderHandle handle) const noexcept {
    if (!handle.is_valid() || handle.slot >= providers_.size()) return nullptr;
    const ProviderSlot& slot = *providers_[handle.slot];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

AePatternProviderService::JobSlot* AePatternProviderService::find_job_slot(
    AePatternProviderJobHandle handle) noexcept {
    if (!handle.is_valid() || handle.slot >= jobs_.size()) return nullptr;
    JobSlot& slot = *jobs_[handle.slot];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

const AePatternProviderService::JobSlot* AePatternProviderService::find_job_slot(
    AePatternProviderJobHandle handle) const noexcept {
    if (!handle.is_valid() || handle.slot >= jobs_.size()) return nullptr;
    const JobSlot& slot = *jobs_[handle.slot];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

AePatternProviderJobSnapshot AePatternProviderService::snapshot_of(
    AePatternProviderJobHandle handle, const Job& job) const {
    AePatternProviderJobSnapshot snapshot{
        .handle = handle,
        .requested = job.requested,
        .state = job.state,
        .next_step_index = job.next_step_index,
        .remaining_operations_in_step = job.remaining_operations_in_step,
        .completed_operations = job.completed_operations,
    };
    if (job.pending_operation) {
        snapshot.active_provider_key = job.pending_operation->provider_key;
        snapshot.active_pattern_id = job.pending_operation->pattern_id;
    }
    return snapshot;
}

void AePatternProviderService::cancel_jobs_for_scope_rebuild(uint64_t scope_id) noexcept {
    size_t cancelled = 0;
    for (size_t index = 1; index < jobs_.size(); ++index) {
        JobSlot& slot = *jobs_[index];
        if (!slot.occupied || slot.job.scope_id != scope_id ||
            slot.job.state == AePatternProviderJobState::kCompleted ||
            slot.job.state == AePatternProviderJobState::kCancelledByContentReload ||
            slot.job.state == AePatternProviderJobState::kFailed) {
            continue;
        }
        if (slot.job.pending_operation) {
            slot.job.cancel_after_pending_operation = true;
        } else {
            slot.job.state = AePatternProviderJobState::kCancelledByContentReload;
        }
        ++cancelled;
    }
    if (cancelled != 0) {
        SNT_LOG_INFO("Cancelled %zu AE provider job(s) after a route boundary", cancelled);
    }
}

void AePatternProviderService::cancel_jobs_for_resource_reload() noexcept {
    size_t cancelled = 0;
    for (size_t index = 1; index < jobs_.size(); ++index) {
        JobSlot& slot = *jobs_[index];
        if (!slot.occupied || slot.job.state == AePatternProviderJobState::kCompleted ||
            slot.job.state == AePatternProviderJobState::kCancelledByContentReload ||
            slot.job.state == AePatternProviderJobState::kFailed) {
            continue;
        }
        if (slot.job.pending_operation) {
            slot.job.cancel_after_pending_operation = true;
        } else {
            slot.job.state = AePatternProviderJobState::kCancelledByContentReload;
        }
        ++cancelled;
    }
    if (cancelled != 0) {
        SNT_LOG_INFO("Cancelled %zu queued AE provider job(s at resource reload boundary)",
                     cancelled);
    }
}

}  // namespace snt::game
