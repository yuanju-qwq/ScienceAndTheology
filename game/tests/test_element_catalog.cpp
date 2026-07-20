// Tests for the immutable canonical periodic-table catalog.

#include <array>
#include <cstdint>
#include <set>
#include <string_view>

#include <gtest/gtest.h>

#include "game/chemistry/chemical_species.h"
#include "game/chemistry/element_catalog.h"

namespace {

using snt::game::chemistry::ChemicalSpeciesReference;
using snt::game::chemistry::ChemicalSpeciesReferenceKind;
using snt::game::chemistry::ElementCatalog;
using snt::game::chemistry::ElementCategory;
using snt::game::chemistry::ElementId;
using snt::game::chemistry::ExtensionSpeciesId;
using snt::game::chemistry::kElementCount;
using snt::game::chemistry::kInvalidElementId;

TEST(ElementCatalogTest, ContainsEveryCanonicalAtomicNumberExactlyOnce) {
    const auto elements = ElementCatalog::all();
    ASSERT_EQ(elements.size(), static_cast<size_t>(kElementCount));

    constexpr std::array<std::string_view, kElementCount> kExpectedSymbols = {{
        "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne",
        "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca",
        "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn",
        "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr",
        "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn",
        "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd",
        "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb",
        "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg",
        "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th",
        "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm",
        "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds",
        "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og",
    }};

    std::set<std::string_view> symbols;
    for (uint16_t atomic_number = 1; atomic_number <= kElementCount; ++atomic_number) {
        const ElementId id{static_cast<uint8_t>(atomic_number)};
        const auto* element = ElementCatalog::find(id);
        ASSERT_NE(element, nullptr) << atomic_number;
        EXPECT_EQ(element->id.atomic_number, atomic_number);
        EXPECT_EQ(element, &elements[atomic_number - 1]);
        EXPECT_EQ(element->symbol, kExpectedSymbols[atomic_number - 1]);
        EXPECT_TRUE(symbols.insert(element->symbol).second) << element->symbol;
    }
    EXPECT_EQ(symbols.size(), static_cast<size_t>(kElementCount));
    EXPECT_EQ(ElementCatalog::find(kInvalidElementId), nullptr);
    EXPECT_EQ(ElementCatalog::find(ElementId{119}), nullptr);
}

TEST(ElementCatalogTest, ResolvesKnownSymbolsAndRejectsNonCanonicalSpelling) {
    const auto* hydrogen = ElementCatalog::find_by_symbol("H");
    ASSERT_NE(hydrogen, nullptr);
    EXPECT_EQ(hydrogen->id.atomic_number, 1);
    EXPECT_EQ(hydrogen->period, 1);
    EXPECT_EQ(hydrogen->group, 1);

    const auto* iron = ElementCatalog::find_by_symbol("Fe");
    ASSERT_NE(iron, nullptr);
    EXPECT_EQ(iron->id.atomic_number, 26);
    EXPECT_EQ(iron->category, ElementCategory::kTransitionMetal);

    const auto* oganesson = ElementCatalog::find_by_symbol("Og");
    ASSERT_NE(oganesson, nullptr);
    EXPECT_EQ(oganesson->id.atomic_number, 118);
    EXPECT_EQ(oganesson->group, 18);

    EXPECT_EQ(ElementCatalog::find_by_symbol("fe"), nullptr);
    EXPECT_EQ(ElementCatalog::find_by_symbol("FE"), nullptr);
    EXPECT_EQ(ElementCatalog::find_by_symbol("Naq"), nullptr);
    EXPECT_EQ(ElementCatalog::find_by_symbol(""), nullptr);
}

TEST(ChemicalSpeciesBoundaryTest, KeepsExtensionsOutsideTheAtomicNumberSpace) {
    const ChemicalSpeciesReference iron =
        ChemicalSpeciesReference::from_element(ElementId{26});
    EXPECT_TRUE(iron.is_valid());
    EXPECT_EQ(iron.kind, ChemicalSpeciesReferenceKind::kStandardElement);
    EXPECT_EQ(iron.element.atomic_number, 26);

    const ChemicalSpeciesReference naquadah =
        ChemicalSpeciesReference::from_extension(ExtensionSpeciesId{1});
    EXPECT_TRUE(naquadah.is_valid());
    EXPECT_EQ(naquadah.kind, ChemicalSpeciesReferenceKind::kExtension);
    EXPECT_EQ(naquadah.element.atomic_number, 0);
    EXPECT_EQ(ElementCatalog::find(naquadah.element), nullptr);
}

}  // namespace
