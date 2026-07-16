// Focused tests for the game-owned JSON localization boundary.

#include "game/localization/localization.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace snt::game::localization;

class ScopedCatalogDirectory {
public:
    ScopedCatalogDirectory() {
        path_ = fs::temp_directory_path() / "snt_game_localization_test_catalogs";
        std::error_code error;
        fs::remove_all(path_, error);
        fs::create_directories(path_);
    }

    ~ScopedCatalogDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    void write(std::string_view filename, std::string_view contents) const {
        std::ofstream output(path_ / std::string(filename), std::ios::binary);
        ASSERT_TRUE(output.is_open());
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(output.good());
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

TEST(Localization, LoadsJsonCatalogsFallsBackAndFormatsNamedArguments) {
    ScopedCatalogDirectory catalogs;
    catalogs.write("en.json", R"json({
        "locale": "en",
        "messages": {
            "ui.title": "Inventory",
            "ui.greeting": "Hello {name}, x{count}",
            "fallback.only": "Fallback text",
            "ui.braces": "{{literal}} {name}"
        }
    })json");
    catalogs.write("zh-Hans.json", R"json({
        "locale": "zh-Hans",
        "messages": {
            "ui.title": "背包",
            "ui.greeting": "你好，{name} x{count}"
        }
    })json");

    auto service = LocalizationService::load(
        std::make_shared<JsonFileLocalizationCatalogSource>(catalogs.path().string()),
        {.locale = "zh-Hans", .fallback_locale = "en"});
    ASSERT_TRUE(service) << service.error().format();

    EXPECT_EQ((*service)->translate("ui.title"), "背包");
    EXPECT_EQ((*service)->translate("fallback.only"), "Fallback text");
    EXPECT_EQ((*service)->translate("missing.key"), "missing.key");
    EXPECT_EQ((*service)->format("ui.greeting", {{"name", "Ada"}, {"count", "2"}}),
              "你好，Ada x2");
    EXPECT_EQ((*service)->format("ui.braces", {{"name", "Ada"}}), "{literal} Ada");
    EXPECT_EQ((*service)->format("ui.greeting", {{"name", "Ada"}}), "你好，Ada x{count}");
}

TEST(Localization, UsesFallbackWhenTheRequestedCatalogIsNotPackaged) {
    ScopedCatalogDirectory catalogs;
    catalogs.write("en.json", R"json({
        "locale": "en",
        "messages": {"ui.title": "Inventory"}
    })json");

    auto service = LocalizationService::load(
        std::make_shared<JsonFileLocalizationCatalogSource>(catalogs.path().string()),
        {.locale = "fr", .fallback_locale = "en"});
    ASSERT_TRUE(service) << service.error().format();
    EXPECT_EQ((*service)->locale(), "fr");
    EXPECT_EQ((*service)->translate("ui.title"), "Inventory");
}

TEST(Localization, RejectsCatalogWhoseDeclaredLocaleDoesNotMatchItsFilename) {
    ScopedCatalogDirectory catalogs;
    catalogs.write("en.json", R"json({
        "locale": "zh-Hans",
        "messages": {"ui.title": "背包"}
    })json");

    JsonFileLocalizationCatalogSource source(catalogs.path().string());
    const auto catalog = source.load_catalog("en");
    ASSERT_FALSE(catalog);
    EXPECT_EQ(catalog.error().code(), snt::core::ErrorCode::kInvalidArgument);
    EXPECT_NE(catalog.error().message().find("declares locale"), std::string::npos);
}

}  // namespace
