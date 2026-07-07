// RenderGraph implementation.
//
// P2.2 scope:
//   - init() creates a VkCommandPool on the graphics family.
//   - add_pass() stores a RenderGraphPass and returns a pointer for filling.
//   - execute() iterates passes in registration order, for each:
//       * acquire a CommandContext (one per pass, recycled after submit)
//       * begin_recording -> invoke pass.execute(ctx) -> end_recording
//       * submit to graphics queue + wait for completion (serial execution)
//   - reset() clears the pass list but keeps the command pool.
//
// P2.3 will add: dependency-driven ordering, automatic barriers,
// transient resource allocation.

#include "renderer/render_graph.h"
#include "renderer/transient_pool.h"

#include "render_backend/command_context.h"
#include "render_backend/vulkan_device.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdio>
#include <deque>
#include <unordered_map>
#include <vector>

namespace snt::renderer {

// ---------------------------------------------------------------------------
// ResourceEntry: tracks a single registered resource (transient or external).
// ---------------------------------------------------------------------------
struct ResourceEntry {
    ResourceType type = ResourceType::kInvalid;

    // For transient textures: descriptor + lazy-allocated Vulkan objects.
    TextureDesc tex_desc;
    BufferDesc  buf_desc;
    VkImage     image       = VK_NULL_HANDLE;
    VkImageView view        = VK_NULL_HANDLE;
    VkBuffer    buffer      = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;

    // External resources: graph does not own these.
    bool is_external = false;

    // Current layout (tracked for barrier insertion, P2.3.5).
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// ---------------------------------------------------------------------------
// Impl: holds all state that depends on Vulkan types.
// ---------------------------------------------------------------------------
struct RenderGraph::Impl {
    snt::render_backend::VulkanDevice* device = nullptr;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    uint32_t frames_in_flight = 2;

    // Registered passes. Pointers handed out by add_pass() index into here.
    // reset() clears this vector; new add_pass() calls reallocate.
    std::vector<RenderGraphPass> passes;

    // CommandContext pool, indexed by [frame_index][pass_index].
    std::vector<std::vector<snt::render_backend::CommandContext>> contexts;

    // Resource pool: id -> ResourceEntry. ids are assigned sequentially.
    std::vector<ResourceEntry> resources;
    uint32_t next_resource_id = 0;

    // Transient resource allocator (VMA-backed).
    TransientPool transient_pool;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
RenderGraph::RenderGraph() : impl_(std::make_unique<Impl>()) {
    std::printf("[snt::renderer] RenderGraph created (P2.2)\n");
}

RenderGraph::~RenderGraph() {
    destroy();
}

bool RenderGraph::init(snt::render_backend::VulkanDevice& device,
                       uint32_t frames_in_flight) {
    if (impl_->device != nullptr) {
        std::fprintf(stderr,
                     "[snt::renderer] RenderGraph::init already called\n");
        return false;
    }
    if (frames_in_flight == 0) {
        std::fprintf(stderr,
                     "[snt::renderer] RenderGraph::init: frames_in_flight=0\n");
        return false;
    }
    impl_->device = &device;
    impl_->frames_in_flight = frames_in_flight;

    // Create a command pool with RESET_COMMAND_BUFFER flag. We need
    // individual buffer resets because each pass records fresh each frame.
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.graphics_family(),
    };
    if (vkCreateCommandPool(device.logical(), &pool_info, nullptr,
                            &impl_->command_pool) != VK_SUCCESS) {
        std::fprintf(stderr,
                     "[snt::renderer] RenderGraph: vkCreateCommandPool failed\n");
        return false;
    }

    // Pre-size the per-frame CommandContext slots. Each slot will hold
    // `max_passes` contexts, grown lazily on execute_record_only().
    impl_->contexts.resize(frames_in_flight);

    // Initialize the transient resource pool with VMA.
    if (!impl_->transient_pool.init(device.vma_allocator())) {
        std::fprintf(stderr,
                     "[snt::renderer] RenderGraph: TransientPool init failed\n");
        return false;
    }

    std::printf("[snt::renderer] RenderGraph initialized (frames_in_flight=%u)\n",
                frames_in_flight);
    return true;
}

void RenderGraph::destroy() {
    if (!impl_->device) return;

    // Wait idle before tearing down any resources.
    impl_->device->wait_idle();

    // CommandContexts own command buffers allocated from our pool; reset
    // them first so the buffers are freed back to the pool.
    impl_->contexts.clear();
    impl_->passes.clear();

    // Release transient resources + TransientPool.
    impl_->resources.clear();
    impl_->transient_pool.destroy();

    if (impl_->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(impl_->device->logical(), impl_->command_pool,
                             nullptr);
        impl_->command_pool = VK_NULL_HANDLE;
    }

    impl_->device = nullptr;
}

// ---------------------------------------------------------------------------
// Resource creation / import (P2.3)
// ---------------------------------------------------------------------------
// Transient resources are registered with a descriptor but NOT allocated
// yet. Allocation happens lazily on first use inside execute_record_only()
// (P2.3.6 will wire the lazy alloc). For P2.3.4 (dependency derivation) we
// only need the descriptor + a valid handle.
// ---------------------------------------------------------------------------
RenderResource RenderGraph::create_texture(const TextureDesc& desc) {
    RenderResource r;
    r.id = impl_->next_resource_id++;
    r.type = ResourceType::kTexture;

    ResourceEntry e{};
    e.type = ResourceType::kTexture;
    e.tex_desc = desc;
    e.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    impl_->resources.push_back(e);
    return r;
}

RenderResource RenderGraph::create_buffer(const BufferDesc& desc) {
    RenderResource r;
    r.id = impl_->next_resource_id++;
    r.type = ResourceType::kBuffer;

    ResourceEntry e{};
    e.type = ResourceType::kBuffer;
    e.buf_desc = desc;
    impl_->resources.push_back(e);
    return r;
}

RenderResource RenderGraph::import_texture(VkImage image, VkImageView view,
                                           VkFormat format,
                                           VkImageLayout current_layout) {
    RenderResource r;
    r.id = impl_->next_resource_id++;
    r.type = ResourceType::kTexture;

    ResourceEntry e{};
    e.type = ResourceType::kTexture;
    e.image = image;
    e.view = view;
    e.is_external = true;
    e.current_layout = current_layout;
    // Fill tex_desc.format so passes can query it; width/height unknown for
    // external resources (pass must query separately if needed).
    e.tex_desc.format = static_cast<uint32_t>(format);
    impl_->resources.push_back(e);
    return r;
}

// ---------------------------------------------------------------------------
// Pass registration
// ---------------------------------------------------------------------------
RenderGraphPass* RenderGraph::add_pass(const std::string& name) {
    if (!impl_->device) {
        std::fprintf(stderr,
                     "[snt::renderer] RenderGraph::add_pass called before init\n");
        return nullptr;
    }
    impl_->passes.push_back(RenderGraphPass{.name = name});
    return &impl_->passes.back();
}

// ---------------------------------------------------------------------------
// Execute: serial pass-by-pass submission (P2.2 baseline, Mode A)
// ---------------------------------------------------------------------------
bool RenderGraph::execute() {
    // Mode A uses frame slot 0 by default (no frames-in-flight pipelining).
    if (!execute_record_only(0)) return false;

    // Submit each context's command buffer + wait serially.
    // This is the baseline; P2.3 will replace with dependency scheduling.
    auto& slot = impl_->contexts[0];
    for (size_t i = 0; i < impl_->passes.size(); ++i) {
        snt::render_backend::CommandContext& ctx = slot[i];
        VkCommandBuffer cb = ctx.handle();

        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
        };

        VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(impl_->device->logical(), &fence_info, nullptr,
                          &fence) != VK_SUCCESS) {
            std::fprintf(stderr,
                         "[snt::renderer] Pass %zu: vkCreateFence failed\n", i);
            return false;
        }

        if (vkQueueSubmit(impl_->device->graphics_queue(), 1, &submit_info,
                          fence) != VK_SUCCESS) {
            std::fprintf(stderr,
                         "[snt::renderer] Pass %zu: vkQueueSubmit failed\n", i);
            vkDestroyFence(impl_->device->logical(), fence, nullptr);
            return false;
        }

        // Wait for this pass to finish before starting the next.
        vkWaitForFences(impl_->device->logical(), 1, &fence, VK_TRUE,
                        UINT64_MAX);
        vkDestroyFence(impl_->device->logical(), fence, nullptr);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Implicit dependency derivation (P2.3.4)
// ---------------------------------------------------------------------------
// Derives pass-to-pass dependencies from attachment read/write overlap.
// Rule: if pass B reads a resource that pass A writes, B depends on A.
// Writes are merged into `depends_on` before topological sort.
// ---------------------------------------------------------------------------
static void derive_implicit_dependencies(std::vector<RenderGraphPass>* passes) {
    const size_t n = passes->size();

    // Map: resource id -> list of (pass_index, is_write).
    std::unordered_map<uint32_t, std::vector<std::pair<size_t, bool>>> readers_writers;
    for (size_t i = 0; i < n; ++i) {
        RenderGraphPass& p = (*passes)[i];
        for (const auto& a : p.inputs) {
            readers_writers[a.resource.id].emplace_back(i, /*is_write=*/false);
        }
        for (const auto& a : p.outputs) {
            readers_writers[a.resource.id].emplace_back(i, /*is_write=*/true);
        }
    }

    // For each resource: every reader depends on every writer that came
    // before it (by registration order). This is the RAW / WAW / WAR
    // dependency; for simplicity we add writer→reader edges only (RAW).
    for (auto& kv : readers_writers) {
        const auto& accesses = kv.second;
        for (const auto& [reader_idx, is_read_write] : accesses) {
            if (is_read_write) continue;  // skip writers
            for (const auto& [writer_idx, w_is_write] : accesses) {
                if (!w_is_write) continue;
                if (writer_idx == reader_idx) continue;
                // reader depends on writer
                RenderGraphPass& reader = (*passes)[reader_idx];
                const std::string& writer_name = (*passes)[writer_idx].name;
                // Avoid duplicate entries.
                bool exists = false;
                for (const auto& d : reader.depends_on) {
                    if (d == writer_name) { exists = true; break; }
                }
                if (!exists) {
                    reader.depends_on.push_back(writer_name);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Topological sort (P2.3)
// ---------------------------------------------------------------------------
// Orders passes by their `depends_on` edges. Returns a permutation of pass
// indices; on cycle detection returns false + writes the offending pass name.
// ---------------------------------------------------------------------------
static bool topological_sort(const std::vector<RenderGraphPass>& passes,
                             std::vector<size_t>* out_order,
                             std::string* out_cycle_name) {
    const size_t n = passes.size();
    out_order->clear();
    out_order->reserve(n);

    // Build name -> index map.
    std::unordered_map<std::string, size_t> name_to_index;
    for (size_t i = 0; i < n; ++i) {
        name_to_index[passes[i].name] = i;
    }

    // Build adjacency list + in-degree count.
    std::vector<std::vector<size_t>> adj(n);  // adj[u] = passes that depend on u
    std::vector<size_t> in_degree(n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (const auto& dep_name : passes[i].depends_on) {
            auto it = name_to_index.find(dep_name);
            if (it == name_to_index.end()) {
                std::fprintf(stderr,
                             "[snt::renderer] Pass '%s' depends on unknown pass "
                             "'%s'\n",
                             passes[i].name.c_str(), dep_name.c_str());
                return false;
            }
            size_t dep_idx = it->second;
            adj[dep_idx].push_back(i);
            ++in_degree[i];
        }
    }

    // Kahn's algorithm: start with in_degree==0 passes, process in order.
    std::deque<size_t> queue;
    for (size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) queue.push_back(i);
    }

    while (!queue.empty()) {
        size_t u = queue.front();
        queue.pop_front();
        out_order->push_back(u);
        for (size_t v : adj[u]) {
            if (--in_degree[v] == 0) {
                queue.push_back(v);
            }
        }
    }

    if (out_order->size() != n) {
        // Cycle: some passes never reached in_degree 0.
        if (out_cycle_name) {
            for (size_t i = 0; i < n; ++i) {
                if (in_degree[i] > 0) {
                    *out_cycle_name = passes[i].name;
                    break;
                }
            }
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// execute_record_only: record all passes, do not submit (Mode B)
// ---------------------------------------------------------------------------
// `frame_index` selects which frame slot's CommandContexts to use. The
// caller MUST pass the same frame_index it uses for its frames-in-flight
// synchronization (e.g. VulkanFrame::current_frame()) so that a command
// buffer is not reset while still pending on the GPU.
//
// P2.3: passes are topologically sorted by their `depends_on` edges before
// recording. The recorded command buffer at slot[frame_index][pass_index]
// corresponds to the pass at `passes[sorted_order[pass_index]]`.
// ---------------------------------------------------------------------------
bool RenderGraph::execute_record_only(uint32_t frame_index) {
    if (!impl_->device) {
        std::fprintf(stderr,
                     "[snt::renderer] RenderGraph::execute called before init\n");
        return false;
    }
    if (frame_index >= impl_->frames_in_flight) {
        std::fprintf(stderr,
                     "[snt::renderer] execute_record_only: frame_index %u >= "
                     "frames_in_flight %u\n",
                     frame_index, impl_->frames_in_flight);
        return false;
    }
    if (impl_->passes.empty()) {
        return true;  // nothing to do — not an error
    }

    // --- Derive implicit dependencies from attachment overlap (P2.3.4) ---
    derive_implicit_dependencies(&impl_->passes);

    // --- Topological sort (P2.3) ---
    std::vector<size_t> order;
    std::string cycle_name;
    if (!topological_sort(impl_->passes, &order, &cycle_name)) {
        std::fprintf(stderr,
                     "[snt::renderer] RenderGraph: dependency cycle detected "
                     "(starting at '%s')\n",
                     cycle_name.c_str());
        return false;
    }

    auto& slot = impl_->contexts[frame_index];
    if (slot.size() < impl_->passes.size()) {
        slot.resize(impl_->passes.size());
    }

    // Record passes in topological order. The recorded command buffer at
    // slot[pass_index] corresponds to the pass at passes[order[pass_index]].
    for (size_t pass_index = 0; pass_index < order.size(); ++pass_index) {
        RenderGraphPass& pass = impl_->passes[order[pass_index]];
        snt::render_backend::CommandContext& ctx = slot[pass_index];

        if (!pass.execute) {
            std::fprintf(stderr,
                         "[snt::renderer] Pass '%s' has no execute callback\n",
                         pass.name.c_str());
            return false;
        }

        if (!ctx.begin_recording(*impl_->device, impl_->command_pool)) {
            std::fprintf(stderr,
                         "[snt::renderer] Pass '%s': begin_recording failed\n",
                         pass.name.c_str());
            return false;
        }

        pass.execute(ctx);
        ctx.end_recording();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Per-frame reset
// ---------------------------------------------------------------------------
void RenderGraph::reset() {
    impl_->passes.clear();
    // Clear per-frame transient resources (external resources are also
    // cleared — callers must re-import them each frame if they want to
    // reference external images as attachments).
    impl_->resources.clear();
    impl_->next_resource_id = 0;
    impl_->transient_pool.reset();
    // CommandContexts are kept (their command buffers are reusable).
}

// ---------------------------------------------------------------------------
// Access recorded command buffer (Mode B helper)
// ---------------------------------------------------------------------------
VkCommandBuffer RenderGraph::recorded_command_buffer(uint32_t frame_index,
                                                     size_t pass_index) const {
    if (frame_index >= impl_->contexts.size()) return VK_NULL_HANDLE;
    const auto& slot = impl_->contexts[frame_index];
    if (pass_index >= slot.size()) return VK_NULL_HANDLE;
    return slot[pass_index].handle();
}

uint32_t RenderGraph::recorded_command_buffers(uint32_t frame_index,
                                               std::vector<VkCommandBuffer>* out_buffers) const {
    out_buffers->clear();
    if (frame_index >= impl_->contexts.size()) return 0;
    const auto& slot = impl_->contexts[frame_index];
    out_buffers->reserve(slot.size());
    for (const auto& ctx : slot) {
        VkCommandBuffer cb = ctx.handle();
        if (cb != VK_NULL_HANDLE) {
            out_buffers->push_back(cb);
        }
    }
    return static_cast<uint32_t>(out_buffers->size());
}

}  // namespace snt::renderer
