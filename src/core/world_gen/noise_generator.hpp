#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace science_and_theology {

// 2D fractal noise generator based on gradient noise.
// Produces deterministic, seed-based terrain height and feature maps.
//
// Usage:
//   NoiseGenerator noise(seed);
//   float h = noise.noise_2d(x, y, 4, 0.5f);
//   float h_scaled = noise.noise_2d_scaled(x, y, 0.02f, 4);
class NoiseGenerator {
public:
    static constexpr int kTableSize = 256;

    explicit NoiseGenerator(uint64_t seed);
    ~NoiseGenerator() = default;

    // Fractal (multi-octave) 2D noise. Returns values roughly in [-1, 1].
    // octaves: number of detail layers (higher = more detail, slower).
    // persistence: amplitude multiplier per octave (0.5 = standard).
    float noise_2d(float x, float y, int octaves = 4,
                   float persistence = 0.5f) const;

    // Scaled 2D noise. Convenience wrapper that applies a coordinate scale
    // before calling noise_2d(). Smaller scale = larger features.
    float noise_2d_scaled(float x, float y, float scale,
                          int octaves = 4) const;

private:
    void init_permutation(uint64_t seed);

    float raw_noise_2d(float x, float y) const;

    float fade(float t) const;
    float lerp(float a, float b, float t) const;
    float gradient(int hash, float x, float y) const;

    std::array<uint8_t, kTableSize * 2> perm_;
};

// --- Inline implementations ---

inline float NoiseGenerator::fade(float t) const {
    // Perlin's improved fade curve: 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float NoiseGenerator::lerp(float a, float b, float t) const {
    return a + t * (b - a);
}

inline float NoiseGenerator::gradient(int hash, float x, float y) const {
    // Low 3 bits determine gradient direction.
    int h = hash & 7;
    float u = (h < 4) ? x : y;
    float v = (h < 4) ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

} // namespace science_and_theology