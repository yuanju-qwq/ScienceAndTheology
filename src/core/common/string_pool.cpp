#include "string_pool.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace {

constexpr size_t kBlockSize = 65536;

std::mutex g_mutex;
std::vector<std::unique_ptr<char[]>> g_blocks;
size_t g_block_used = 0;

} // namespace

namespace science_and_theology::gt {

const char* intern_string(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return "";
    }

    size_t len = std::strlen(s) + 1;

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_blocks.empty() || g_block_used + len > kBlockSize) {
        size_t block_size = std::max(kBlockSize, len);
        g_blocks.push_back(std::make_unique<char[]>(block_size));
        g_block_used = 0;
    }

    char* p = g_blocks.back().get() + g_block_used;
    std::memcpy(p, s, len);
    g_block_used += len;
    // NOTE: no dedup — each call appends a fresh copy. This is intentional:
    // all callers use std::string-keyed maps and strcmp, never pointer identity.
    return p;
}

void clear_string_pool() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_blocks.clear();
    g_block_used = 0;
}

} // namespace science_and_theology::gt
