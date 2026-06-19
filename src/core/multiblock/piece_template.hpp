#pragma once

// ============================================================
// GT V3 Multiblock System — Middle Layer: Piece Template
// ============================================================
//
// Ported from GregTech CEu's V3 PieceTemplate + PieceTemplateCompiler.
//
// A PieceTemplate is a compiled, immutable 3D array of
// IStructureElement pointers. It represents a single structural
// "piece" of a multiblock (most multiblocks have exactly one piece;
// large structures like industrial blast furnaces have multiple).
//
// Layout convention (matches GT aisle format):
//   - aisles are indexed by Z (depth, front-to-back in canonical orient)
//   - within an aisle, rows are indexed by Y (bottom-to-top)
//   - within a row, chars are indexed by X (left-to-right)
//
// The element with is_center()==true marks the controller position.
// Its offset within the pattern is stored as the controller offset,
// used to align the pattern against the controller's world position.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "structure_element.hpp"

namespace science_and_theology::multiblock {

// ------------------------------------------------------------
// PieceTemplate — compiled 3D element array.
//
// Immutable after construction. Elements are stored as shared_ptr
// because the same element instance may be referenced by multiple
// cells in the pattern (e.g. all 'X' cells share the same casing
// element).
// ------------------------------------------------------------
class PieceTemplate {
public:
    PieceTemplate() = default;

    // Compile aisles + symbol map into a PieceTemplate.
    // aisles[z] is a vector of strings (one per Y row, bottom-to-top).
    // symbol_map maps each char to its element.
    // Special chars:
    //   ' ' (space) → any() (wildcard, skipped during matching)
    //   '~'         → self() (controller center, marks is_center)
    //   '#'         → air() (must be empty)
    // Other chars must be in symbol_map or the build fails.
    static PieceTemplate build(
        const std::vector<std::vector<std::string>>& aisles,
        const std::unordered_map<char, std::shared_ptr<IStructureElement>>& symbol_map);

    // Dimensions.
    int size_x() const { return size_x_; }
    int size_y() const { return size_y_; }
    int size_z() const { return size_z_; }

    // Controller offset within the pattern (where the self() element is).
    int controller_offset_x() const { return ctrl_ox_; }
    int controller_offset_y() const { return ctrl_oy_; }
    int controller_offset_z() const { return ctrl_oz_; }

    // Element at pattern coords. Returns any() for out-of-range (defensive).
    const IStructureElement& at(int x, int y, int z) const;

    // A trivial 1x1x1 pattern (controller only) needs no structure check.
    bool is_trivial() const {
        return size_x_ <= 1 && size_y_ <= 1 && size_z_ <= 1;
    }

    bool valid() const {
        return size_x_ > 0 && size_y_ > 0 && size_z_ > 0;
    }

    // Has a controller center been found?
    bool has_controller() const { return has_controller_; }

private:
    std::vector<std::shared_ptr<IStructureElement>> elements_;  // [z*sy*y + y]*sx + x
    int size_x_ = 0, size_y_ = 0, size_z_ = 0;
    int ctrl_ox_ = 0, ctrl_oy_ = 0, ctrl_oz_ = 0;
    bool has_controller_ = false;
};

// ------------------------------------------------------------
// PieceTemplateCompiler — fluent builder for PieceTemplate.
//
// Mirrors GT V3's PieceTemplateCompiler. Usage:
//
//   auto tpl = PieceTemplateCompiler()
//       .aisle({"XXX", "XXX", "XXX"})
//       .aisle({"XXX", "X#X", "XXX"})
//       .aisle({"XXX", "X~X", "XXX"})
//       .where('X', Elements::material(5))
//       .where('#', Elements::air())
//       .build();
// ------------------------------------------------------------
class PieceTemplateCompiler {
public:
    PieceTemplateCompiler() = default;

    // Add an aisle (z-slice). Each string is a row (y, bottom-to-top).
    // All aisles must have the same number of rows and row lengths.
    PieceTemplateCompiler& aisle(const std::vector<std::string>& rows);

    // Register a symbol → element mapping.
    PieceTemplateCompiler& where(char symbol,
                                  std::shared_ptr<IStructureElement> element);

    // Build the compiled PieceTemplate.
    // Returns an invalid template (valid()==false) on malformed input.
    PieceTemplate build();

private:
    std::vector<std::vector<std::string>> depth_;  // [aisle][row]
    std::unordered_map<char, std::shared_ptr<IStructureElement>> symbol_map_;
};

} // namespace science_and_theology::multiblock
