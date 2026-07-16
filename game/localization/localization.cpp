// Game-owned JSON localization implementation.

#define SNT_LOG_CHANNEL "game.localization"
#include "localization.h"

#include "core/log.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace snt::game::localization {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

[[nodiscard]] bool is_valid_locale_id(std::string_view locale) {
    if (locale.empty()) return false;
    for (const unsigned char character : locale) {
        if (!std::isalnum(character) && character != '-' && character != '_') return false;
    }
    return true;
}

[[nodiscard]] snt::core::Expected<LocalizationCatalog> parse_catalog(
    std::string_view requested_locale, const fs::path& source_path, std::string_view text) {
    json document;
    try {
        document = json::parse(text.begin(), text.end());
    } catch (const std::exception& error) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Localization JSON parse error in '" + source_path.string() + "': " + error.what()};
    }

    if (!document.is_object()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Localization catalog '" + source_path.string() +
                                    "' must be a JSON object"};
    }
    if (!document.contains("locale") || !document["locale"].is_string()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Localization catalog '" + source_path.string() +
                                    "' is missing string field 'locale'"};
    }
    const std::string catalog_locale = document["locale"].get<std::string>();
    if (catalog_locale != requested_locale) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Localization catalog '" + source_path.string() + "' declares locale '" +
                catalog_locale + "' but was requested as '" + std::string(requested_locale) + "'"};
    }
    if (!document.contains("messages") || !document["messages"].is_object()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Localization catalog '" + source_path.string() +
                                    "' is missing object field 'messages'"};
    }

    LocalizationCatalog catalog;
    catalog.locale = catalog_locale;
    for (auto entry = document["messages"].begin(); entry != document["messages"].end(); ++entry) {
        if (entry.key().empty()) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "Localization catalog '" + source_path.string() +
                                        "' contains an empty message key"};
        }
        if (!entry.value().is_string()) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidArgument,
                "Localization catalog '" + source_path.string() + "' key '" + entry.key() +
                    "' must have a string value"};
        }
        catalog.messages.emplace(entry.key(), entry.value().get<std::string>());
    }
    return catalog;
}

[[nodiscard]] const LocalizationArgument* find_argument(
    std::initializer_list<LocalizationArgument> arguments, std::string_view name) {
    for (const LocalizationArgument& argument : arguments) {
        if (argument.name == name) return &argument;
    }
    return nullptr;
}

}  // namespace

JsonFileLocalizationCatalogSource::JsonFileLocalizationCatalogSource(std::string directory)
    : directory_(std::move(directory)) {}

snt::core::Expected<LocalizationCatalog> JsonFileLocalizationCatalogSource::load_catalog(
    std::string_view locale) const {
    if (!is_valid_locale_id(locale)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Localization locale identifier is invalid: '" +
                                    std::string(locale) + "'"};
    }

    const fs::path catalog_path = fs::path(directory_) / (std::string(locale) + ".json");
    std::ifstream input(catalog_path, std::ios::binary);
    if (!input.is_open()) {
        return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                "Localization catalog is missing: '" + catalog_path.string() + "'"};
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (input.bad()) {
        return snt::core::Error{snt::core::ErrorCode::kFileOpenFailed,
                                "Failed to read localization catalog '" + catalog_path.string() + "'"};
    }

    auto catalog = parse_catalog(locale, catalog_path, text);
    if (!catalog) return catalog.error();

    SNT_LOG_INFO("Loaded localization catalog '%s' with %zu messages from '%s'",
                 catalog->locale.c_str(), catalog->messages.size(), catalog_path.string().c_str());
    return catalog;
}

snt::core::Expected<std::shared_ptr<LocalizationService>> LocalizationService::load(
    std::shared_ptr<const ILocalizationCatalogSource> source, LocalizationLoadConfig config) {
    if (!source) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Localization requires a catalog source"};
    }
    if (!is_valid_locale_id(config.locale) || !is_valid_locale_id(config.fallback_locale)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Localization locale and fallback locale must be non-empty identifiers"};
    }

    auto fallback_catalog = source->load_catalog(config.fallback_locale);
    if (!fallback_catalog) {
        auto error = fallback_catalog.error();
        error.with_context("LocalizationService::load(fallback catalog)");
        return error;
    }

    std::optional<LocalizationCatalog> active_catalog;
    if (config.locale != config.fallback_locale) {
        auto loaded_active = source->load_catalog(config.locale);
        if (loaded_active) {
            active_catalog = std::move(*loaded_active);
        } else if (loaded_active.error().code() == snt::core::ErrorCode::kFileNotFound) {
            SNT_LOG_WARN("Localization catalog '%s' is unavailable; using fallback '%s'",
                         config.locale.c_str(), config.fallback_locale.c_str());
        } else {
            auto error = loaded_active.error();
            error.with_context("LocalizationService::load(active catalog)");
            return error;
        }
    }

    const size_t active_message_count = active_catalog ? active_catalog->messages.size() : 0;
    const size_t fallback_message_count = fallback_catalog->messages.size();
    auto service = std::shared_ptr<LocalizationService>(
        new LocalizationService(std::move(config.locale), std::move(config.fallback_locale),
                                std::move(active_catalog), std::move(*fallback_catalog)));
    SNT_LOG_INFO("Localization initialized (locale='%s', fallback='%s', active_messages=%zu, "
                 "fallback_messages=%zu)",
                 service->locale().c_str(), service->fallback_locale().c_str(), active_message_count,
                 fallback_message_count);
    return service;
}

LocalizationService::LocalizationService(std::string locale, std::string fallback_locale,
                                         std::optional<LocalizationCatalog> active_catalog,
                                         LocalizationCatalog fallback_catalog)
    : locale_(std::move(locale)), fallback_locale_(std::move(fallback_locale)),
      active_catalog_(std::move(active_catalog)), fallback_catalog_(std::move(fallback_catalog)) {}

const std::string* LocalizationService::find_message(std::string_view key) const {
    const std::string lookup_key(key);
    if (active_catalog_) {
        const auto active = active_catalog_->messages.find(lookup_key);
        if (active != active_catalog_->messages.end()) return &active->second;
    }
    const auto fallback = fallback_catalog_.messages.find(lookup_key);
    if (fallback != fallback_catalog_.messages.end()) return &fallback->second;
    return nullptr;
}

std::string LocalizationService::translate(std::string_view key) const {
    if (key.empty()) return {};
    if (const std::string* message = find_message(key)) return *message;
    report_missing_key(key);
    return std::string(key);
}

std::string LocalizationService::format(
    std::string_view key, std::initializer_list<LocalizationArgument> arguments) const {
    const std::string message = translate(key);
    std::string formatted;
    formatted.reserve(message.size());

    for (size_t index = 0; index < message.size();) {
        if (message[index] == '{' && index + 1 < message.size() && message[index + 1] == '{') {
            formatted.push_back('{');
            index += 2;
            continue;
        }
        if (message[index] == '}' && index + 1 < message.size() && message[index + 1] == '}') {
            formatted.push_back('}');
            index += 2;
            continue;
        }
        if (message[index] != '{') {
            formatted.push_back(message[index++]);
            continue;
        }

        const size_t close = message.find('}', index + 1);
        if (close == std::string::npos) {
            formatted.append(message, index, std::string::npos);
            break;
        }
        const std::string_view placeholder(message.data() + index + 1, close - index - 1);
        if (placeholder.empty()) {
            formatted.append(message, index, close - index + 1);
            index = close + 1;
            continue;
        }
        if (const LocalizationArgument* argument = find_argument(arguments, placeholder)) {
            formatted.append(argument->value);
        } else {
            report_missing_placeholder(key, placeholder);
            formatted.append(message, index, close - index + 1);
        }
        index = close + 1;
    }
    return formatted;
}

void LocalizationService::report_missing_key(std::string_view key) const {
    bool should_log = false;
    {
        std::scoped_lock lock(diagnostics_mutex_);
        should_log = reported_missing_keys_.emplace(key).second;
    }
    if (should_log) {
        SNT_LOG_WARN("Localization key '%.*s' is missing for locale '%s' and fallback '%s'",
                     static_cast<int>(key.size()), key.data(), locale_.c_str(), fallback_locale_.c_str());
    }
}

void LocalizationService::report_missing_placeholder(std::string_view key,
                                                     std::string_view placeholder) const {
    std::string diagnostic_key(key);
    diagnostic_key += "{";
    diagnostic_key.append(placeholder);
    diagnostic_key += "}";

    bool should_log = false;
    {
        std::scoped_lock lock(diagnostics_mutex_);
        should_log = reported_missing_placeholders_.emplace(std::move(diagnostic_key)).second;
    }
    if (should_log) {
        SNT_LOG_WARN("Localization key '%.*s' is missing format argument '%.*s'",
                     static_cast<int>(key.size()), key.data(), static_cast<int>(placeholder.size()),
                     placeholder.data());
    }
}

}  // namespace snt::game::localization
