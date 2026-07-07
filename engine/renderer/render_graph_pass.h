// Render graph pass: a unit of rendering work declared by the application.
//
// Each pass declares its input/output resources and an execute callback.
// The graph (P2.2) orders passes by dependency; the callback is invoked
// at execute time with a CommandContext ready for recording.
//
// Reference pattern: Granite Renderer::RenderPass (callback-based execute)
// with vkb-style attachment declarations.

#pragma once

#include "renderer/render_graph_resource.h"

#include <functional>
#include <string>
#include <vector>

namespace snt::render_backend {
class CommandContext;
}

namespace snt::renderer {

// Attachment declaration: which resource + how it's used in this pass.
struct PassAttachment {
    RenderResource resource;
    ResourceUsage usage = ResourceUsage::kNone;
};

// Execute callback signature.
// The CommandContext is already in recording state when the callback runs;
// the pass should NOT call begin/end_recording (graph manages lifecycle).
using PassExecuteCallback =
    std::function<void(snt::render_backend::CommandContext&)>;

// A single render pass node in the graph.
// Owned by RenderGraph; the application fills in fields after add_pass().
struct RenderGraphPass {
    std::string name;
    std::vector<PassAttachment> inputs;
    std::vector<PassAttachment> outputs;

    // Explicit pass-to-pass dependencies (by name). The graph topologically
    // sorts passes using these edges before execute (P2.3). A pass with no
    // dependencies runs first. Cycles are rejected with an error.
    //
    // Rationale: P2.3 starts with explicit dependencies (simpler than
    // deriving from attachment read/write sets). Implicit dependency
    // derivation from attachment overlap lands in P2.5 along with the
    // transient resource pool.
    std::vector<std::string> depends_on;

    PassExecuteCallback execute;
};

}  // namespace snt::renderer
