#include "noise_generator.hpp"

#include <algorithm>
#include <random>

namespace science_and_theology {

NoiseGenerator::NoiseGenerator(uint64_t seed) {
    init_permutation(seed);
}

void NoiseGenerator::init_permutation(uint64_t seed) {
    // Fill identity permutation.
    for (int i = 0; i < kTableSize; ++i) {
        perm_[i] = static_cast<uint8_t>(i);
    }

    // Shuffle using Fisher-Yates with a deterministic RNG.
    std::mt19937_64 rng(seed);
    for (int i = kTableSize - 1; i > 0; --i) {
        int j = static_cast<int>(rng() % (static_cast<uint64_t>(i) + 1));
        std::swap(perm_[i], perm_[j]);
    }

    // Duplicate for wrapping.
    for (int i = 0; i < kTableSize; ++i) {
        perm_[kTableSize + i] = perm_[i];
    }
}

float NoiseGenerator::noise_2d(
    float x, float y, int octaves, float persistence) const {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float max_value = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        total += raw_noise_2d(x * frequency, y * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    return total / max_value;
}

float NoiseGenerator::noise_2d_scaled(
    float x, float y, float scale, int octaves) const {
    return noise_2d(x * scale, y * scale, octaves);
}

float NoiseGenerator::raw_noise_2d(float x, float y) const {
    // Integer part.
    int xi = static_cast<int>(std::floor(x)) & (kTableSize - 1);
    int yi = static_cast<int>(std::floor(y)) & (kTableSize - 1);

    // Fractional part.
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);

    // Fade curves.
    float u = fade(xf);
    float v = fade(yf);

    // Hash corners of the unit square.
    int aa = perm_[perm_[xi] + yi];
    int ba = perm_[perm_[xi + 1] + yi];
    int ab = perm_[perm_[xi] + yi + 1];
    int bb = perm_[perm_[xi + 1] + yi + 1];

    // Interpolate.
    float x1 = lerp(gradient(aa, xf, yf), gradient(ba, xf - 1.0f, yf), u);
    float x2 = lerp(gradient(ab, xf, yf - 1.0f),
                    gradient(bb, xf - 1.0f, yf - 1.0f), u);

    // Scale from [-0.5, 0.5] to roughly [-1, 1].
    return lerp(x1, x2, v) * 2.0f;
}

} // namespace science_and_theology