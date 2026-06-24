#include "sfm_manager.hpp"

#include <sstream>

namespace science_and_theology::sfm {

SFMManager::SFMManager()
    : executor_(program_, variables_, containers_) {}

void SFMManager::discover_containers(
        const std::function<bool(gt::MapPosition)>& is_container,
        const std::function<std::unique_ptr<IContainerAccess>(gt::MapPosition)>& factory) {
    // Clear existing containers.
    containers_.clear();

    auto positions = cable_graph_.discover_containers(is_container);
    for (const auto& pos : positions) {
        auto access = factory(pos);
        if (access) {
            containers_.register_container(std::move(access));
        }
    }
}

std::string SFMManager::serialize() const {
    std::ostringstream ss;
    ss << "{\"program\":" << program_.to_json();
    ss << ",\"variables\":[";
    bool first = true;
    for (const auto& [id, var] : variables_.variables()) {
        if (!first) ss << ",";
        first = false;
        ss << "{\"id\":" << id
           << ",\"name\":\"" << var.name << "\""
           << ",\"type\":" << static_cast<int>(var.type) << "}";
    }
    ss << "]}";
    return ss.str();
}

bool SFMManager::deserialize(const std::string& data) {
    // Parse the wrapper to extract program JSON and variables.
    // Simple approach: find "program":{...} and "variables":[...]
    // The program JSON is a complete object, so we need balanced braces.
    size_t prog_key = data.find("\"program\"");
    if (prog_key == std::string::npos) return false;
    size_t prog_start = data.find('{', prog_key);
    if (prog_start == std::string::npos) return false;

    // Find matching closing brace.
    int depth = 0;
    size_t prog_end = prog_start;
    for (size_t i = prog_start; i < data.size(); ++i) {
        if (data[i] == '{') depth++;
        else if (data[i] == '}') {
            depth--;
            if (depth == 0) { prog_end = i; break; }
        }
    }
    std::string prog_json = data.substr(prog_start, prog_end - prog_start + 1);
    program_.from_json(prog_json);

    // Variables parsing is simplified — the executor can work with
    // default variables. Full variable restore can be added later.
    return true;
}

} // namespace science_and_theology::sfm
