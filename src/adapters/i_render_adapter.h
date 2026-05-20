#pragma once

namespace science_and_theology {

/// Pure virtual interface for rendering operations.
class IRenderAdapter {
public:
    virtual ~IRenderAdapter() = default;
};

} // namespace science_and_theology