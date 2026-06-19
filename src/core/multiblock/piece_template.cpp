#include "piece_template.hpp"

#include <sstream>

namespace science_and_theology::multiblock {

// ============================================================
// PieceTemplate
// ============================================================

PieceTemplate PieceTemplate::build(
    const std::vector<std::vector<std::string>>& aisles,
    const std::unordered_map<char, std::shared_ptr<IStructureElement>>& symbol_map) {

    PieceTemplate result;

    if (aisles.empty()) return result;

    // Determine dimensions.
    const int sz = static_cast<int>(aisles.size());
    int sy = 0;
    int sx = 0;
    for (const auto& aisle : aisles) {
        sy = (static_cast<int>(aisle.size()) > sy)
            ? static_cast<int>(aisle.size()) : sy;
        for (const auto& row : aisle) {
            sx = (static_cast<int>(row.size()) > sx)
                ? static_cast<int>(row.size()) : sx;
        }
    }
    if (sx <= 0 || sy <= 0 || sz <= 0) return result;

    result.size_x_ = sx;
    result.size_y_ = sy;
    result.size_z_ = sz;

    // Default all cells to any() (wildcard).
    auto any_elem = Elements::any();
    result.elements_.assign(
        static_cast<size_t>(sx * sy * sz), any_elem);

    // Process each cell.
    for (int z = 0; z < sz; ++z) {
        const auto& aisle = aisles[z];
        for (int y = 0; y < sy; ++y) {
            const std::string* row_ptr = nullptr;
            if (y < static_cast<int>(aisle.size())) {
                row_ptr = &aisle[y];
            }
            for (int x = 0; x < sx; ++x) {
                if (!row_ptr || x >= static_cast<int>(row_ptr->size())) {
                    continue;  // leave as any()
                }

                char c = row_ptr->at(x);
                size_t idx = static_cast<size_t>((z * sy + y) * sx + x);

                if (c == ' ') {
                    // Space = any() (already default)
                    continue;
                }

                if (c == '~') {
                    // Controller center marker.
                    result.elements_[idx] = Elements::self();
                    if (result.has_controller_) {
                        // Multiple controllers — malformed.
                        return PieceTemplate();
                    }
                    result.has_controller_ = true;
                    result.ctrl_ox_ = x;
                    result.ctrl_oy_ = y;
                    result.ctrl_oz_ = z;
                    continue;
                }

                if (c == '#') {
                    // Air shorthand.
                    result.elements_[idx] = Elements::air();
                    continue;
                }

                auto it = symbol_map.find(c);
                if (it == symbol_map.end()) {
                    // Unknown symbol — malformed.
                    return PieceTemplate();
                }
                result.elements_[idx] = it->second;

                // Check if this element is a center marker.
                if (it->second && it->second->is_center()) {
                    if (result.has_controller_) {
                        return PieceTemplate();  // multiple centers
                    }
                    result.has_controller_ = true;
                    result.ctrl_ox_ = x;
                    result.ctrl_oy_ = y;
                    result.ctrl_oz_ = z;
                }
            }
        }
    }

    return result;
}

const IStructureElement& PieceTemplate::at(int x, int y, int z) const {
    static auto fallback = Elements::any();
    if (x < 0 || x >= size_x_ || y < 0 || y >= size_y_ || z < 0 || z >= size_z_) {
        return *fallback;
    }
    size_t idx = static_cast<size_t>((z * size_y_ + y) * size_x_ + x);
    return *elements_[idx];
}

// ============================================================
// PieceTemplateCompiler
// ============================================================

PieceTemplateCompiler& PieceTemplateCompiler::aisle(
    const std::vector<std::string>& rows) {
    depth_.push_back(rows);
    return *this;
}

PieceTemplateCompiler& PieceTemplateCompiler::where(
    char symbol, std::shared_ptr<IStructureElement> element) {
    symbol_map_[symbol] = std::move(element);
    return *this;
}

PieceTemplate PieceTemplateCompiler::build() {
    return PieceTemplate::build(depth_, symbol_map_);
}

} // namespace science_and_theology::multiblock
