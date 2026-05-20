#pragma once

namespace science_and_theology {

/// Pure virtual interface for world queries.
/// Decouples core logic from the specific game engine's world representation.
class IWorldAdapter {
public:
    virtual ~IWorldAdapter() = default;
};

} // namespace science_and_theology