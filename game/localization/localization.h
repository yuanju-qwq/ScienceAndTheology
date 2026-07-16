// Game-owned localization catalog and lookup contract.
//
// This module owns localized game content only. It is deliberately headless:
// simulation, persistence, and network code keep stable keys/IDs, while
// presentation resolves those keys at the final UI boundary. JSON is an
// authoring and load-time format; callers only query immutable in-memory maps.

#pragma once

#include "core/expected.h"

#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace snt::game::localization {

// One fully validated locale catalog held in memory after startup loading.
struct LocalizationCatalog {
    std::string locale;
    std::unordered_map<std::string, std::string> messages;
};

// Source boundary reserved for packaged catalogs, mod catalogs, and editor
// sources. Sources return complete catalogs so precedence remains owned by
// LocalizationService rather than by filesystem traversal order.
class ILocalizationCatalogSource {
public:
    virtual ~ILocalizationCatalogSource() = default;

    [[nodiscard]] virtual snt::core::Expected<LocalizationCatalog> load_catalog(
        std::string_view locale) const = 0;
};

// Loads `<directory>/<locale>.json` catalogs from the packaged game content.
class JsonFileLocalizationCatalogSource final : public ILocalizationCatalogSource {
public:
    explicit JsonFileLocalizationCatalogSource(std::string directory);

    [[nodiscard]] snt::core::Expected<LocalizationCatalog> load_catalog(
        std::string_view locale) const override;

private:
    std::string directory_;
};

struct LocalizationLoadConfig {
    // The active locale follows the runtime UI locale so text shaping and
    // translated content use the same language selection at startup.
    std::string locale = "en";
    std::string fallback_locale = "en";
};

// Only named string substitutions are supported in v1, for example
// `"{item} x{count}"`. Number/date/plural formatting remains explicitly out
// of scope until the product needs ICU MessageFormat semantics.
struct LocalizationArgument {
    std::string_view name;
    std::string_view value;
};

class LocalizationService final {
public:
    // Loads the fallback catalog first. A missing active catalog falls back
    // cleanly; malformed active or fallback catalogs fail startup.
    [[nodiscard]] static snt::core::Expected<std::shared_ptr<LocalizationService>> load(
        std::shared_ptr<const ILocalizationCatalogSource> source,
        LocalizationLoadConfig config = {});

    // Resolves a stable key from the active catalog, then its fallback. A
    // missing key returns the key itself and emits one warning for that key.
    [[nodiscard]] std::string translate(std::string_view key) const;

    // Resolves `key` and replaces `{name}` placeholders. Missing arguments
    // remain visible in the result and are logged once per key/placeholder.
    [[nodiscard]] std::string format(
        std::string_view key,
        std::initializer_list<LocalizationArgument> arguments) const;

    [[nodiscard]] const std::string& locale() const noexcept { return locale_; }
    [[nodiscard]] const std::string& fallback_locale() const noexcept { return fallback_locale_; }

private:
    LocalizationService(std::string locale, std::string fallback_locale,
                        std::optional<LocalizationCatalog> active_catalog,
                        LocalizationCatalog fallback_catalog);

    [[nodiscard]] const std::string* find_message(std::string_view key) const;
    void report_missing_key(std::string_view key) const;
    void report_missing_placeholder(std::string_view key, std::string_view placeholder) const;

    std::string locale_;
    std::string fallback_locale_;
    std::optional<LocalizationCatalog> active_catalog_;
    LocalizationCatalog fallback_catalog_;
    mutable std::mutex diagnostics_mutex_;
    mutable std::unordered_set<std::string> reported_missing_keys_;
    mutable std::unordered_set<std::string> reported_missing_placeholders_;
};

}  // namespace snt::game::localization
