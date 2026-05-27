#include "ae2_pattern_provider.hpp"

#include <algorithm>

namespace science_and_theology::gt {

void PatternProviderHost::add_provider(PatternProvider* provider) {
    if (!provider) return;
    if (std::find(providers_.begin(), providers_.end(), provider) != providers_.end())
        return;
    providers_.push_back(provider);
}

void PatternProviderHost::remove_provider(PatternProvider* provider) {
    auto it = std::find(providers_.begin(), providers_.end(), provider);
    if (it != providers_.end()) {
        PatternRegistry::remove_provider_patterns((*it)->provider_id());
        providers_.erase(it);
    }
}

PatternProvider* PatternProviderHost::find_provider(uint64_t id) const {
    for (auto* p : providers_) {
        if (p->provider_id() == id) return p;
    }
    return nullptr;
}

void PatternProviderHost::sync_to_registry() {
    for (auto* provider : providers_) {
        uint64_t pid = provider->provider_id();
        // Remove old patterns for this provider, then re-add.
        PatternRegistry::remove_provider_patterns(pid);
        for (const auto* pattern : provider->get_patterns()) {
            PatternRegistry::add_provider_pattern(pattern, pid);
        }
    }
}

void PatternProviderHost::resync_all() {
    // Clear all provider patterns from registry.
    for (auto* provider : providers_) {
        PatternRegistry::remove_provider_patterns(provider->provider_id());
    }
    // Re-add all.
    sync_to_registry();
}

} // namespace science_and_theology::gt
