// Vulkan Mesh implementation.

#include "vulkan_mesh.h"
#include "vulkan_buffer.h"
#include "vulkan_device.h"

#include <volk.h>
#include <tiny_obj_loader.h>

#include <cstdio>
#include <cstring>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanMesh::~VulkanMesh() {
    destroy();
}

bool VulkanMesh::load_obj(VulkanDevice& device, const std::string& path,
                          const float default_color[3]) {
    device_ = &device;

    // --- Load .obj via tinyobjloader ---
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        std::fprintf(stderr, "[snt::render_backend] Failed to load .obj: %s\n%s\n",
                     path.c_str(), err.c_str());
        return false;
    }
    if (!warn.empty()) {
        std::printf("[snt::render_backend] .obj warning: %s\n", warn.c_str());
    }

    // --- Build vertex + index arrays ---
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            MeshVertex vertex{};

            // Position (tinyobj stores positions as a flat float array).
            if (index.vertex_index >= 0) {
                vertex.position[0] = attrib.vertices[3 * index.vertex_index + 0];
                vertex.position[1] = attrib.vertices[3 * index.vertex_index + 1];
                vertex.position[2] = attrib.vertices[3 * index.vertex_index + 2];
            }

            // Color: use default (P1.5: no material/vertex color support).
            vertex.color[0] = default_color[0];
            vertex.color[1] = default_color[1];
            vertex.color[2] = default_color[2];

            vertices.push_back(vertex);
            indices.push_back(static_cast<uint32_t>(indices.size()));
        }
    }

    vertex_count_ = static_cast<uint32_t>(vertices.size());
    index_count_ = static_cast<uint32_t>(indices.size());

    std::printf("[snt::render_backend] Mesh loaded: %s (%u verts, %u indices)\n",
                path.c_str(), vertex_count_, index_count_);

    // --- Create vertex buffer (CPU-visible for P1.5; P2: staging buffer) ---
    vertex_buffer_ = new VulkanBuffer();
    if (!vertex_buffer_->init(*device_, sizeof(MeshVertex) * vertices.size(),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              true /* cpu_visible */)) {
        std::fprintf(stderr, "[snt::render_backend] Vertex buffer init failed\n");
        return false;
    }
    vertex_buffer_->write(vertices.data(),
                          sizeof(MeshVertex) * vertices.size());

    // --- Create index buffer ---
    index_buffer_ = new VulkanBuffer();
    if (!index_buffer_->init(*device_, sizeof(uint32_t) * indices.size(),
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             true /* cpu_visible */)) {
        std::fprintf(stderr, "[snt::render_backend] Index buffer init failed\n");
        return false;
    }
    index_buffer_->write(indices.data(), sizeof(uint32_t) * indices.size());

    return true;
}

void VulkanMesh::destroy() {
    delete vertex_buffer_;  // VulkanBuffer destructor calls destroy()
    delete index_buffer_;
    vertex_buffer_ = nullptr;
    index_buffer_ = nullptr;
    vertex_count_ = 0;
    index_count_ = 0;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void VulkanMesh::draw(VkCommandBuffer cmd) {
    if (!vertex_buffer_ || !index_buffer_) return;

    // Bind vertex buffer.
    VkBuffer vertex_buffers[] = {vertex_buffer_->handle()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);

    // Bind index buffer.
    vkCmdBindIndexBuffer(cmd, index_buffer_->handle(), 0, VK_INDEX_TYPE_UINT32);

    // Draw indexed.
    vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);
}

}  // namespace snt::render_backend
