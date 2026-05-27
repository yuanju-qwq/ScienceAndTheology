#pragma once

#include <vector>

#include "ae2_pattern.hpp"

namespace science_and_theology::gt {

// Interface for machines that can provide patterns to the autocrafting system.
// Mirrors AE2's concept of a pattern provider (e.g. ME Interface).
class PatternProvider {
public:
    virtual ~PatternProvider() = default;

    // Returns all patterns currently held by this provider.
    virtual const std::vector<const AEPattern*>& get_patterns() const = 0;

    // Called after patterns have changed (provider should re-sync).
    virtual void on_patterns_changed() {}

    // Unique ID for this provider (for lookup/deduplication).
    virtual uint64_t provider_id() const = 0;
};

// Manages a collection of pattern providers and syncs their patterns
// into the static PatternRegistry for use by the crafting tree.
class PatternProviderHost {
public:
    void add_provider(PatternProvider* provider);
    void remove_provider(PatternProvider* provider);
    PatternProvider* find_provider(uint64_t id) const;

    // Sync all provider patterns into PatternRegistry.
    void sync_to_registry();

    // Clear all provider-added patterns from the registry,
    // then re-sync all providers.
    void resync_all();

    const std::vector<PatternProvider*>& providers() const { return providers_; }

private:
    std::vector<PatternProvider*> providers_;
};

} // namespace science_and_theology::gt
