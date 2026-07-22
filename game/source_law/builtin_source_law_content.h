// Minimal built-in source-law content used to prove the V0.1 sand-armor loop.

#pragma once

#include "core/expected.h"
#include "game/source_law/source_law_definition.h"

namespace snt::game::source_law {

[[nodiscard]] snt::core::Expected<SourceLawContentSnapshot>
make_builtin_source_law_content_v0_1(uint64_t revision = 1);

}  // namespace snt::game::source_law
