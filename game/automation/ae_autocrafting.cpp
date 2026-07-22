// AE autocrafting planning and execution implementation.

#define SNT_LOG_CHANNEL "game.ae_autocrafting"
#include "game/automation/ae_autocrafting.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace snt::game {
namespace {

constexpr size_t kMaxPatternIdentifierBytes = 256;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool valid_identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= kMaxPatternIdentifierBytes &&
        value.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool checked_add(int64_t left, int64_t right, int64_t& result) noexcept {
    if (right > 0 && left > std::numeric_limits<int64_t>::max() - right) return false;
    if (right < 0 && left < std::numeric_limits<int64_t>::min() - right) return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(int64_t left, uint64_t right, int64_t& result) noexcept {
    if (left < 0 || right > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        left != 0 && right > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() / left)) {
        return false;
    }
    result = left * static_cast<int64_t>(right);
    return true;
}

[[nodiscard]] std::optional<uint64_t> ceil_divide_positive(
    int64_t numerator, int64_t denominator) noexcept {
    if (numerator <= 0 || denominator <= 0) return std::nullopt;
    const uint64_t unsigned_numerator = static_cast<uint64_t>(numerator);
    const uint64_t unsigned_denominator = static_cast<uint64_t>(denominator);
    return 1u + ((unsigned_numerator - 1u) / unsigned_denominator);
}

[[nodiscard]] snt::core::Expected<void> restore_inputs(
    IAeAutocraftingResourceAccess& resources,
    const ResourceKeyContext& context,
    const std::vector<ResourceStack>& extracted) {
    for (auto it = extracted.rbegin(); it != extracted.rend(); ++it) {
        if (resources.insert(context, *it, ResourceTransferMode::kExecute) != it->amount) {
            return invalid_state("AE autocrafting could not restore an extracted input");
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> restore_outputs_and_inputs(
    IAeAutocraftingResourceAccess& resources,
    const ResourceKeyContext& context,
    const std::vector<ResourceStack>& inserted,
    const std::vector<ResourceStack>& extracted) {
    for (auto it = inserted.rbegin(); it != inserted.rend(); ++it) {
        if (resources.extract(context, *it, ResourceTransferMode::kExecute) != it->amount) {
            return invalid_state("AE autocrafting could not remove a partially inserted output");
        }
    }
    return restore_inputs(resources, context, extracted);
}

}  // namespace

struct AeAutocraftingService::CompiledPattern {
    std::string id;
    std::vector<ResourceStack> inputs;
    std::vector<ResourceStack> outputs;
    uint32_t ticks_per_operation = 1;
};

struct AeAutocraftingService::CompiledCatalog {
    std::map<std::string, CompiledPattern, std::less<>> patterns;
    std::unordered_map<ResourceKey, std::vector<std::string>, ResourceKey::Hash> producers;
};

struct AeAutocraftingService::Job {
    ResourceContentStack requested;
    std::vector<AeAutocraftingPlanStep> steps;
    AeAutocraftingJobState state = AeAutocraftingJobState::kRunning;
    size_t next_step_index = 0;
    uint64_t remaining_operations_in_step = 0;
    uint64_t completed_operations = 0;
};

struct AeAutocraftingService::JobSlot {
    bool occupied = false;
    uint32_t generation = 1;
    Job job;
};

struct AeAutocraftingService::PendingResourceRuntimeSnapshot {
    ResourceRuntimeIndex::Snapshot resource_runtime_index;
    CompiledCatalog compiled_catalog;
};

class AeAutocraftingPlanner final {
public:
    AeAutocraftingPlanner(const AeAutocraftingService::CompiledCatalog& catalog,
                          const ResourceRuntimeIndex::Snapshot& resource_runtime_index,
                          IAeAutocraftingResourceAccess& resources)
        : catalog_(catalog), resources_(resources),
          context_(resource_runtime_index.key_context()) {}

    [[nodiscard]] snt::core::Expected<std::vector<AeAutocraftingPlanStep>> build(
        const ResourceStack& requested) {
        if (auto result = satisfy(requested.key, requested.amount); !result) return result.error();
        return std::move(steps_);
    }

private:
    [[nodiscard]] snt::core::Expected<int64_t> available_for(const ResourceKey& key) {
        const auto found = available_.find(key);
        if (found != available_.end()) return found->second;
        const int64_t observed = resources_.amount_of(context_, key);
        const int64_t normalized = std::max<int64_t>(0, observed);
        available_.emplace(key, normalized);
        return normalized;
    }

    [[nodiscard]] snt::core::Expected<void> add_available(
        const ResourceKey& key, int64_t amount) {
        auto current = available_for(key);
        if (!current) return current.error();
        int64_t next = 0;
        if (!checked_add(*current, amount, next)) {
            return invalid_state("AE autocrafting plan would overflow an available resource amount");
        }
        available_[key] = next;
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> satisfy(const ResourceKey& key, int64_t amount) {
        if (!key.is_valid() || amount <= 0) {
            return invalid_argument("AE autocrafting plan received an invalid compact resource request");
        }
        auto available = available_for(key);
        if (!available) return available.error();
        if (*available >= amount) {
            available_[key] = *available - amount;
            return {};
        }

        const int64_t deficit = amount - *available;
        available_[key] = 0;
        if (!planning_keys_.insert(key).second) {
            return invalid_state("AE autocrafting pattern graph contains a recursive resource cycle");
        }
        const auto clear_key = [this, &key]() { planning_keys_.erase(key); };

        const auto producers = catalog_.producers.find(key);
        if (producers == catalog_.producers.end() || producers->second.empty()) {
            clear_key();
            return invalid_state("AE autocrafting plan is missing a producer for a requested resource");
        }
        const auto pattern_found = catalog_.patterns.find(producers->second.front());
        if (pattern_found == catalog_.patterns.end()) {
            clear_key();
            return invalid_state("AE autocrafting producer index refers to a missing pattern");
        }
        const AeAutocraftingService::CompiledPattern& pattern = pattern_found->second;
        const auto output = std::find_if(
            pattern.outputs.begin(), pattern.outputs.end(),
            [&key](const ResourceStack& stack) { return stack.key == key; });
        if (output == pattern.outputs.end() || output->amount <= 0) {
            clear_key();
            return invalid_state("AE autocrafting producer has no positive requested output");
        }
        const auto operations = ceil_divide_positive(deficit, output->amount);
        if (!operations) {
            clear_key();
            return invalid_state("AE autocrafting could not calculate a positive operation count");
        }

        for (const ResourceStack& input : pattern.inputs) {
            int64_t total_input = 0;
            if (!checked_multiply(input.amount, *operations, total_input)) {
                clear_key();
                return invalid_state("AE autocrafting plan input amount overflowed");
            }
            if (auto result = satisfy(input.key, total_input); !result) {
                clear_key();
                return result.error();
            }
        }

        if (!steps_.empty() && steps_.back().pattern_id == pattern.id) {
            uint64_t total_operations = 0;
            if (*operations > std::numeric_limits<uint64_t>::max() - steps_.back().operations) {
                clear_key();
                return invalid_state("AE autocrafting plan operation count overflowed");
            }
            total_operations = steps_.back().operations + *operations;
            steps_.back().operations = total_operations;
        } else {
            steps_.push_back({.pattern_id = pattern.id, .operations = *operations});
        }
        for (const ResourceStack& output_stack : pattern.outputs) {
            int64_t total_output = 0;
            if (!checked_multiply(output_stack.amount, *operations, total_output)) {
                clear_key();
                return invalid_state("AE autocrafting plan output amount overflowed");
            }
            if (auto result = add_available(output_stack.key, total_output); !result) {
                clear_key();
                return result.error();
            }
        }
        const auto produced = available_.find(key);
        if (produced == available_.end() || produced->second < deficit) {
            clear_key();
            return invalid_state("AE autocrafting plan produced less than its calculated deficit");
        }
        produced->second -= deficit;
        clear_key();
        return {};
    }

    const AeAutocraftingService::CompiledCatalog& catalog_;
    IAeAutocraftingResourceAccess& resources_;
    ResourceKeyContext context_;
    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> available_;
    std::unordered_set<ResourceKey, ResourceKey::Hash> planning_keys_;
    std::vector<AeAutocraftingPlanStep> steps_;
};

ResourceKeyContext AeAutocraftingStorageAccess::key_context() const noexcept {
    return storage_ == nullptr ? ResourceKeyContext{} : storage_->key_context();
}

int64_t AeAutocraftingStorageAccess::amount_of(
    const ResourceKeyContext& context, const ResourceKey& key) const {
    return storage_ == nullptr ? 0 : storage_->amount_of(context, key);
}

int64_t AeAutocraftingStorageAccess::insert(
    const ResourceKeyContext& context, const ResourceStack& stack,
    ResourceTransferMode mode) {
    return storage_ == nullptr ? 0 : storage_->insert(context, stack, mode);
}

int64_t AeAutocraftingStorageAccess::extract(
    const ResourceKeyContext& context, const ResourceStack& requested,
    ResourceTransferMode mode) {
    return storage_ == nullptr ? 0 : storage_->extract(context, requested, mode);
}

AeAutocraftingService::AeAutocraftingService(
    ResourceRuntimeIndex::Snapshot resource_runtime_index)
    : resource_runtime_index_(std::move(resource_runtime_index)) {
    jobs_.emplace_back(std::make_unique<JobSlot>());
    compiled_catalog_ = std::make_unique<CompiledCatalog>();
}

AeAutocraftingService::~AeAutocraftingService() = default;

snt::core::Expected<void> AeAutocraftingService::replace_patterns(
    std::vector<AeAutocraftingPatternDefinition> definitions) {
    if (!resource_runtime_index_.key_context().is_valid()) {
        return invalid_state("AE autocrafting service has no valid resource runtime snapshot");
    }
    std::map<std::string, AeAutocraftingPatternDefinition, std::less<>> next_definitions;
    for (AeAutocraftingPatternDefinition& definition : definitions) {
        if (!valid_identifier(definition.id)) {
            return invalid_argument("AE autocrafting pattern id is invalid");
        }
        if (!next_definitions.emplace(definition.id, std::move(definition)).second) {
            return invalid_argument("AE autocrafting pattern ids must be unique");
        }
    }
    auto compiled = compile_catalog(next_definitions, resource_runtime_index_);
    if (!compiled) return compiled.error();
    definitions_ = std::move(next_definitions);
    compiled_catalog_ = std::make_unique<CompiledCatalog>(std::move(*compiled));
    cancel_active_jobs_for_catalog_boundary();
    SNT_LOG_INFO("Published %zu AE autocrafting pattern(s)", definitions_.size());
    return {};
}

std::vector<AeAutocraftingPatternDefinition>
AeAutocraftingService::pattern_definitions() const {
    std::vector<AeAutocraftingPatternDefinition> result;
    result.reserve(definitions_.size());
    for (const auto& [id, definition] : definitions_) {
        static_cast<void>(id);
        result.push_back(definition);
    }
    return result;
}

snt::core::Expected<AeAutocraftingPlan> AeAutocraftingService::plan(
    IAeAutocraftingResourceAccess& resources,
    ResourceContentStack requested) const {
    if (!resource_runtime_index_.key_context().is_valid() || !compiled_catalog_) {
        return invalid_state("AE autocrafting service has no compiled resource snapshot");
    }
    if (!requested.is_valid()) {
        return invalid_argument("AE autocrafting request requires a positive stable resource stack");
    }
    if (!resources.key_context().matches(resource_runtime_index_.key_context())) {
        return invalid_state("AE autocrafting resource owner belongs to a different runtime snapshot");
    }
    const auto compact = resolve_resource_stack(requested, resource_runtime_index_);
    if (!compact || !compact->is_valid()) {
        return invalid_state("AE autocrafting request cannot resolve its stable resource key");
    }
    AeAutocraftingPlanner planner{*compiled_catalog_, resource_runtime_index_, resources};
    auto steps = planner.build(*compact);
    if (!steps) return steps.error();
    return AeAutocraftingPlan{
        .requested = std::move(requested),
        .steps = std::move(*steps),
    };
}

snt::core::Expected<AeAutocraftingJobHandle> AeAutocraftingService::submit_job(
    IAeAutocraftingResourceAccess& resources,
    ResourceContentStack requested) {
    auto planned = plan(resources, std::move(requested));
    if (!planned) return planned.error();

    uint32_t slot_index = 0;
    if (!reusable_job_slots_.empty()) {
        slot_index = reusable_job_slots_.back();
        reusable_job_slots_.pop_back();
    } else {
        if (jobs_.size() > std::numeric_limits<uint32_t>::max()) {
            return invalid_state("AE autocrafting job handle slots are exhausted");
        }
        slot_index = static_cast<uint32_t>(jobs_.size());
        jobs_.emplace_back(std::make_unique<JobSlot>());
    }
    JobSlot& slot = *jobs_[slot_index];
    if (slot.occupied || slot.generation == 0) {
        return invalid_state("AE autocrafting encountered an unavailable job slot");
    }
    slot.occupied = true;
    slot.job = {
        .requested = std::move(planned->requested),
        .steps = std::move(planned->steps),
        .state = AeAutocraftingJobState::kRunning,
    };
    if (slot.job.steps.empty()) {
        slot.job.state = AeAutocraftingJobState::kCompleted;
    } else {
        slot.job.remaining_operations_in_step = slot.job.steps.front().operations;
    }
    const AeAutocraftingJobHandle handle{.slot = slot_index, .generation = slot.generation};
    SNT_LOG_INFO("Submitted AE autocrafting job slot=%u steps=%zu state=%u",
                 static_cast<unsigned int>(handle.slot), slot.job.steps.size(),
                 static_cast<unsigned int>(slot.job.state));
    return handle;
}

snt::core::Expected<AeAutocraftingJobSnapshot> AeAutocraftingService::tick(
    AeAutocraftingJobHandle handle,
    IAeAutocraftingResourceAccess& resources,
    uint32_t max_operations) {
    if (max_operations == 0) {
        return invalid_argument("AE autocrafting tick requires a positive operation budget");
    }
    JobSlot* const slot = find_slot(handle);
    if (slot == nullptr) return invalid_state("AE autocrafting tick received a stale job handle");
    Job& job = slot->job;
    if (job.state == AeAutocraftingJobState::kCompleted ||
        job.state == AeAutocraftingJobState::kCancelledByContentReload ||
        job.state == AeAutocraftingJobState::kFailed) {
        return snapshot_of(handle, job);
    }
    if (!resources.key_context().matches(resource_runtime_index_.key_context())) {
        return invalid_state("AE autocrafting tick resource owner belongs to a different runtime snapshot");
    }
    if (!compiled_catalog_) {
        return invalid_state("AE autocrafting tick has no compiled pattern catalog");
    }
    job.state = AeAutocraftingJobState::kRunning;
    uint32_t executed = 0;
    while (executed < max_operations && job.next_step_index < job.steps.size()) {
        AeAutocraftingPlanStep& step = job.steps[job.next_step_index];
        const auto pattern = compiled_catalog_->patterns.find(step.pattern_id);
        if (pattern == compiled_catalog_->patterns.end()) {
            job.state = AeAutocraftingJobState::kFailed;
            return invalid_state("AE autocrafting job refers to a pattern absent from its catalog");
        }
        if (job.remaining_operations_in_step == 0) {
            job.remaining_operations_in_step = step.operations;
        }
        auto operation = execute_operation(pattern->second, resources);
        if (!operation) {
            job.state = AeAutocraftingJobState::kFailed;
            return operation.error();
        }
        if (*operation == OperationResult::kBlockedMissingInput) {
            job.state = AeAutocraftingJobState::kBlockedMissingInput;
            break;
        }
        if (*operation == OperationResult::kBlockedOutputCapacity) {
            job.state = AeAutocraftingJobState::kBlockedOutputCapacity;
            break;
        }
        ++executed;
        ++job.completed_operations;
        --job.remaining_operations_in_step;
        if (job.remaining_operations_in_step == 0) ++job.next_step_index;
    }
    if (job.next_step_index == job.steps.size()) {
        job.state = AeAutocraftingJobState::kCompleted;
    }
    return snapshot_of(handle, job);
}

std::optional<AeAutocraftingJobSnapshot> AeAutocraftingService::find_job(
    AeAutocraftingJobHandle handle) const noexcept {
    const JobSlot* const slot = find_slot(handle);
    if (slot == nullptr) return std::nullopt;
    return snapshot_of(handle, slot->job);
}

bool AeAutocraftingService::remove_job(AeAutocraftingJobHandle handle) noexcept {
    JobSlot* const slot = find_slot(handle);
    if (slot == nullptr) return false;
    slot->occupied = false;
    slot->job = {};
    if (slot->generation != std::numeric_limits<uint32_t>::max()) {
        ++slot->generation;
        reusable_job_slots_.push_back(handle.slot);
    }
    return true;
}

snt::core::Expected<void> AeAutocraftingService::prepare_resource_runtime_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    if (pending_resource_runtime_snapshot_) {
        return invalid_state("AE autocrafting resource snapshot preparation is already pending");
    }
    if (!next_snapshot.key_context().is_valid()) {
        return invalid_argument("AE autocrafting resource snapshot is invalid");
    }
    auto compiled = compile_catalog(definitions_, next_snapshot);
    if (!compiled) return compiled.error();
    pending_resource_runtime_snapshot_ = std::make_unique<PendingResourceRuntimeSnapshot>(
        PendingResourceRuntimeSnapshot{
        .resource_runtime_index = std::move(next_snapshot),
        .compiled_catalog = std::move(*compiled),
    });
    return {};
}

void AeAutocraftingService::commit_resource_runtime_snapshot() noexcept {
    if (!pending_resource_runtime_snapshot_) return;
    resource_runtime_index_ = std::move(pending_resource_runtime_snapshot_->resource_runtime_index);
    compiled_catalog_ = std::make_unique<CompiledCatalog>(
        std::move(pending_resource_runtime_snapshot_->compiled_catalog));
    pending_resource_runtime_snapshot_.reset();
    cancel_active_jobs_for_catalog_boundary();
    SNT_LOG_INFO("Recompiled AE autocrafting patterns for resource snapshot generation=%llu",
                 static_cast<unsigned long long>(resource_runtime_index_.generation()));
}

void AeAutocraftingService::cancel_resource_runtime_snapshot() noexcept {
    pending_resource_runtime_snapshot_.reset();
}

snt::core::Expected<AeAutocraftingService::CompiledCatalog>
AeAutocraftingService::compile_catalog(
    const std::map<std::string, AeAutocraftingPatternDefinition, std::less<>>& definitions,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    if (!resource_runtime_index.key_context().is_valid()) {
        return invalid_argument("AE autocrafting pattern compilation requires a valid resource snapshot");
    }
    CompiledCatalog result;
    result.patterns.clear();
    for (const auto& [id, definition] : definitions) {
        if (id != definition.id || !valid_identifier(definition.id) || definition.inputs.empty() ||
            definition.outputs.empty() || definition.ticks_per_operation == 0) {
            return invalid_argument("AE autocrafting pattern has an invalid identity or operation shape");
        }
        CompiledPattern compiled{
            .id = definition.id,
            .ticks_per_operation = definition.ticks_per_operation,
        };
        std::unordered_set<ResourceKey, ResourceKey::Hash> input_keys;
        std::unordered_set<ResourceKey, ResourceKey::Hash> output_keys;
        const auto compile_stacks = [&resource_runtime_index](
                                        const std::vector<ResourceContentStack>& source,
                                        std::vector<ResourceStack>& destination,
                                        std::unordered_set<ResourceKey, ResourceKey::Hash>& seen,
                                        std::string_view label) -> snt::core::Expected<void> {
            destination.reserve(source.size());
            for (const ResourceContentStack& content_stack : source) {
                if (!content_stack.is_valid()) {
                    return invalid_argument("AE autocrafting pattern contains an invalid " +
                                            std::string(label) + " stack");
                }
                const auto resolved = resolve_resource_stack(content_stack, resource_runtime_index);
                if (!resolved || !resolved->is_valid()) {
                    return invalid_state("AE autocrafting pattern contains an unresolved " +
                                         std::string(label) + " resource key");
                }
                if (!seen.insert(resolved->key).second) {
                    return invalid_argument("AE autocrafting pattern has duplicate " +
                                            std::string(label) + " resource keys");
                }
                destination.push_back(*resolved);
            }
            std::sort(destination.begin(), destination.end(),
                      [](const ResourceStack& left, const ResourceStack& right) {
                          if (left.key.kind != right.key.kind) return left.key.kind < right.key.kind;
                          if (left.key.runtime_id != right.key.runtime_id) {
                              return left.key.runtime_id < right.key.runtime_id;
                          }
                          return left.key.variant < right.key.variant;
                      });
            return {};
        };
        if (auto compiled_inputs = compile_stacks(definition.inputs, compiled.inputs, input_keys,
                                                  "input");
            !compiled_inputs) {
            return compiled_inputs.error();
        }
        if (auto compiled_outputs = compile_stacks(definition.outputs, compiled.outputs, output_keys,
                                                   "output");
            !compiled_outputs) {
            return compiled_outputs.error();
        }
        const auto inserted = result.patterns.emplace(definition.id, std::move(compiled));
        if (!inserted.second) {
            return invalid_state("AE autocrafting compiler found a duplicate pattern id");
        }
        for (const ResourceStack& output : inserted.first->second.outputs) {
            result.producers[output.key].push_back(definition.id);
        }
    }
    for (auto& [key, producers] : result.producers) {
        static_cast<void>(key);
        std::sort(producers.begin(), producers.end());
        producers.erase(std::unique(producers.begin(), producers.end()), producers.end());
    }
    return result;
}

snt::core::Expected<AeAutocraftingService::OperationResult>
AeAutocraftingService::execute_operation(
    const CompiledPattern& pattern,
    IAeAutocraftingResourceAccess& resources) {
    const ResourceKeyContext context = resource_runtime_index_.key_context();
    for (const ResourceStack& input : pattern.inputs) {
        if (resources.extract(context, input, ResourceTransferMode::kSimulate) != input.amount) {
            return OperationResult::kBlockedMissingInput;
        }
    }

    std::vector<ResourceStack> extracted;
    extracted.reserve(pattern.inputs.size());
    for (const ResourceStack& input : pattern.inputs) {
        if (resources.extract(context, input, ResourceTransferMode::kExecute) != input.amount) {
            if (auto rollback = restore_inputs(resources, context, extracted); !rollback) {
                SNT_LOG_ERROR("AE autocrafting input rollback failed after an unexpected extraction shortfall: %s",
                              rollback.error().format().c_str());
                return rollback.error();
            }
            return OperationResult::kBlockedMissingInput;
        }
        extracted.push_back(input);
    }

    std::vector<ResourceStack> inserted;
    inserted.reserve(pattern.outputs.size());
    for (const ResourceStack& output : pattern.outputs) {
        const int64_t accepted = resources.insert(context, output, ResourceTransferMode::kExecute);
        if (accepted != output.amount) {
            if (accepted > 0) inserted.push_back({.key = output.key, .amount = accepted});
            if (auto rollback = restore_outputs_and_inputs(resources, context, inserted, extracted);
                !rollback) {
                SNT_LOG_ERROR("AE autocrafting transaction rollback failed after output shortfall: %s",
                              rollback.error().format().c_str());
                return rollback.error();
            }
            return OperationResult::kBlockedOutputCapacity;
        }
        inserted.push_back(output);
    }
    return OperationResult::kCompleted;
}

AeAutocraftingService::JobSlot* AeAutocraftingService::find_slot(
    AeAutocraftingJobHandle handle) noexcept {
    if (!handle.is_valid() || handle.slot >= jobs_.size()) return nullptr;
    JobSlot& slot = *jobs_[handle.slot];
    if (!slot.occupied || slot.generation != handle.generation) return nullptr;
    return &slot;
}

const AeAutocraftingService::JobSlot* AeAutocraftingService::find_slot(
    AeAutocraftingJobHandle handle) const noexcept {
    if (!handle.is_valid() || handle.slot >= jobs_.size()) return nullptr;
    const JobSlot& slot = *jobs_[handle.slot];
    if (!slot.occupied || slot.generation != handle.generation) return nullptr;
    return &slot;
}

AeAutocraftingJobSnapshot AeAutocraftingService::snapshot_of(
    AeAutocraftingJobHandle handle, const Job& job) const {
    return {
        .handle = handle,
        .requested = job.requested,
        .state = job.state,
        .next_step_index = job.next_step_index,
        .remaining_operations_in_step = job.remaining_operations_in_step,
        .completed_operations = job.completed_operations,
    };
}

void AeAutocraftingService::cancel_active_jobs_for_catalog_boundary() noexcept {
    size_t cancelled = 0;
    for (size_t index = 1; index < jobs_.size(); ++index) {
        JobSlot& slot = *jobs_[index];
        if (!slot.occupied || slot.job.state == AeAutocraftingJobState::kCompleted ||
            slot.job.state == AeAutocraftingJobState::kCancelledByContentReload ||
            slot.job.state == AeAutocraftingJobState::kFailed) {
            continue;
        }
        slot.job.state = AeAutocraftingJobState::kCancelledByContentReload;
        ++cancelled;
    }
    if (cancelled != 0) {
        SNT_LOG_INFO("Cancelled %zu active AE autocrafting job(s) at a catalog/snapshot boundary",
                     cancelled);
    }
}

}  // namespace snt::game
