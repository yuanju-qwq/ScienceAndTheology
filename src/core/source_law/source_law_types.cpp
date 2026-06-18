#include "source_law_types.hpp"

namespace science_and_theology::source_law {

// ============================================================
// Five-phase element relation table
// ============================================================
//
// Generating cycle (相生): FIRE→EARTH, EARTH→WATER, WATER→AIR,
//                         AIR→LIGHT, LIGHT→FIRE,
//                         DARK→CHAOS, CHAOS→ORDER, ORDER→DARK
// Conflicting cycle (相克): FIRE→WATER, WATER→EARTH,
//                          EARTH→AIR, AIR→FIRE
// Severe conflict: LIGHT↔DARK, ORDER↔CHAOS

static const ElementRelation kRelationTable[
    static_cast<int>(magic::RuneElement::COUNT)]
    [static_cast<int>(magic::RuneElement::COUNT)] = {
    //         FIRE              WATER             EARTH             AIR               LIGHT             DARK              ORDER             CHAOS
    /*FIRE*/  {ElementRelation::SAME,             ElementRelation::CONFLICTING, ElementRelation::GENERATING,  ElementRelation::CONFLICTING, ElementRelation::GENERATING,  ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL},
    /*WATER*/ {ElementRelation::CONFLICTING, ElementRelation::SAME,             ElementRelation::GENERATING,  ElementRelation::CONFLICTING, ElementRelation::NEUTRAL,     ElementRelation::GENERATING,  ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL},
    /*EARTH*/ {ElementRelation::CONFLICTING, ElementRelation::CONFLICTING, ElementRelation::SAME,             ElementRelation::GENERATING,  ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::GENERATING,  ElementRelation::NEUTRAL},
    /*AIR*/   {ElementRelation::GENERATING,  ElementRelation::CONFLICTING, ElementRelation::CONFLICTING, ElementRelation::SAME,             ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::GENERATING},
    /*LIGHT*/ {ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::SAME,             ElementRelation::SEVERE_CONFLICT, ElementRelation::GENERATING,  ElementRelation::NEUTRAL},
    /*DARK*/  {ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::SEVERE_CONFLICT, ElementRelation::SAME,             ElementRelation::NEUTRAL,     ElementRelation::GENERATING},
    /*ORDER*/ {ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::GENERATING,  ElementRelation::NEUTRAL,     ElementRelation::SAME,             ElementRelation::SEVERE_CONFLICT},
    /*CHAOS*/ {ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::NEUTRAL,     ElementRelation::GENERATING,  ElementRelation::SEVERE_CONFLICT, ElementRelation::SAME},
};

ElementRelation get_element_relation(magic::RuneElement a, magic::RuneElement b) {
    if (a == b) return ElementRelation::SAME;
    int ia = static_cast<int>(a);
    int ib = static_cast<int>(b);
    if (ia < 0 || ia >= static_cast<int>(magic::RuneElement::COUNT)) return ElementRelation::NEUTRAL;
    if (ib < 0 || ib >= static_cast<int>(magic::RuneElement::COUNT)) return ElementRelation::NEUTRAL;
    return kRelationTable[ia][ib];
}

} // namespace science_and_theology::source_law
