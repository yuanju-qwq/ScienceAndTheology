#pragma once

namespace science_and_theology::gt {

// Arena-based string storage. Strings are copied into fixed-size blocks
// and never freed individually. The pool lives until clear_string_pool().
// Empty/null input always returns "" (static storage).
//
// CONTRACT:
//   - Does NOT deduplicate. Multiple calls with identical content return
//     different pointers. Do NOT rely on pointer equality for comparison;
//     use strcmp or convert to std::string.
//   - The returned pointer is valid until clear_string_pool() is called.
//   - Thread-safe (internally mutex-protected).
const char* intern_string(const char* s);
void clear_string_pool();

} // namespace science_and_theology::gt
