#pragma once

namespace science_and_theology {

/// Pure virtual interface for audio operations.
class IAudioAdapter {
public:
    virtual ~IAudioAdapter() = default;
};

} // namespace science_and_theology