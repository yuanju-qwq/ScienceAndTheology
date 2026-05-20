#pragma once

namespace science_and_theology {

/// Pure virtual interface for input operations.
class IInputAdapter {
public:
    virtual ~IInputAdapter() = default;
};

} // namespace science_and_theology