#include "flow_program.hpp"

#include "flow_node_factory.hpp"

#include <functional>
#include <sstream>

namespace science_and_theology::sfm {

// ============================================================
// Node lifecycle
// ============================================================

FlowNodeId FlowProgram::add_node(FlowNodeType type) {
    FlowNodeId id = next_node_id_++;
    FlowNode node = FlowNodeFactory::create(id, type);
    nodes_[id] = std::move(node);
    return id;
}

bool FlowProgram::remove_node(FlowNodeId id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;

    // Remove all connections touching this node.
    std::vector<FlowConnectionId> to_remove;
    for (const auto& [cid, conn] : connections_) {
        if (conn.from_node == id || conn.to_node == id) {
            to_remove.push_back(cid);
        }
    }
    for (FlowConnectionId cid : to_remove) {
        disconnect(cid);
    }

    nodes_.erase(it);
    return true;
}

// ============================================================
// Connection lifecycle
// ============================================================

bool FlowProgram::validate_connection(FlowNodeId from_node, FlowPortId from_port,
                                      FlowNodeId to_node, FlowPortId to_port,
                                      FlowPortType& out_type) const {
    if (from_node == to_node) return false;

    const FlowNode* src = get_node(from_node);
    const FlowNode* dst = get_node(to_node);
    if (!src || !dst) return false;

    const FlowPort* src_port = src->find_output_port(from_port);
    const FlowPort* dst_port = dst->find_input_port(to_port);
    if (!src_port || !dst_port) return false;

    // Port types must match.
    if (src_port->type != dst_port->type) return false;
    if (src_port->type == FlowPortType::NONE) return false;

    out_type = src_port->type;
    return true;
}

FlowConnectionId FlowProgram::connect(FlowNodeId from_node, FlowPortId from_port,
                                      FlowNodeId to_node, FlowPortId to_port) {
    FlowPortType ptype = FlowPortType::NONE;
    if (!validate_connection(from_node, from_port, to_node, to_port, ptype)) {
        return kInvalidFlowConnectionId;
    }

    // For data input ports, replace any existing incoming connection
    // (a data input has exactly one source).
    if (ptype != FlowPortType::FLOW) {
        const FlowConnection* existing = get_connection_to(to_node, to_port);
        if (existing) {
            disconnect(existing->id);
        }
    }

    FlowConnectionId id = next_conn_id_++;
    FlowConnection conn;
    conn.id = id;
    conn.from_node = from_node;
    conn.from_port = from_port;
    conn.to_node = to_node;
    conn.to_port = to_port;
    conn.port_type = ptype;
    connections_[id] = conn;

    out_index_[port_key(from_node, from_port)].push_back(id);
    in_index_[port_key(to_node, to_port)] = id;

    return id;
}

bool FlowProgram::disconnect(FlowConnectionId id) {
    auto it = connections_.find(id);
    if (it == connections_.end()) return false;

    const FlowConnection& conn = it->second;

    // Remove from out_index_.
    uint64_t out_key = port_key(conn.from_node, conn.from_port);
    auto out_it = out_index_.find(out_key);
    if (out_it != out_index_.end()) {
        auto& vec = out_it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
        if (vec.empty()) out_index_.erase(out_it);
    }

    // Remove from in_index_.
    uint64_t in_key = port_key(conn.to_node, conn.to_port);
    auto in_it = in_index_.find(in_key);
    if (in_it != in_index_.end() && in_it->second == id) {
        in_index_.erase(in_it);
    }

    connections_.erase(it);
    return true;
}

void FlowProgram::disconnect_output(FlowNodeId node, FlowPortId port) {
    auto conns = get_connections_from(node, port);
    for (const FlowConnection* c : conns) {
        disconnect(c->id);
    }
}

// ============================================================
// Topology queries
// ============================================================

std::vector<const FlowConnection*> FlowProgram::get_connections_from(
        FlowNodeId node, FlowPortId port) const {
    std::vector<const FlowConnection*> result;
    auto it = out_index_.find(port_key(node, port));
    if (it == out_index_.end()) return result;
    for (FlowConnectionId cid : it->second) {
        auto cit = connections_.find(cid);
        if (cit != connections_.end()) {
            result.push_back(&cit->second);
        }
    }
    return result;
}

const FlowConnection* FlowProgram::get_connection_to(
        FlowNodeId node, FlowPortId port) const {
    auto it = in_index_.find(port_key(node, port));
    if (it == in_index_.end()) return nullptr;
    auto cit = connections_.find(it->second);
    return cit == connections_.end() ? nullptr : &cit->second;
}

std::vector<const FlowConnection*> FlowProgram::get_connections_from_node(
        FlowNodeId node) const {
    std::vector<const FlowConnection*> result;
    for (const auto& [cid, conn] : connections_) {
        if (conn.from_node == node) {
            result.push_back(&conn);
        }
    }
    return result;
}

std::vector<FlowNodeId> FlowProgram::get_trigger_nodes() const {
    std::vector<FlowNodeId> result;
    for (const auto& [id, node] : nodes_) {
        if (FlowNodeFactory::is_trigger(node.type)) {
            result.push_back(id);
        }
    }
    return result;
}

void FlowProgram::clear() {
    nodes_.clear();
    connections_.clear();
    out_index_.clear();
    in_index_.clear();
    next_node_id_ = 1;
    next_conn_id_ = 1;
}

// ============================================================
// Serialization (compact JSON)
// ============================================================

// Simple JSON string escaping.
static std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string FlowProgram::to_json() const {
    std::ostringstream ss;
    ss << "{\"nodes\":[";
    bool first = true;
    for (const auto& [id, node] : nodes_) {
        if (!first) ss << ",";
        first = false;
        ss << "{\"id\":" << id
           << ",\"type\":" << static_cast<int>(node.type)
           << ",\"x\":" << node.editor_x
           << ",\"y\":" << node.editor_y
           << ",\"params\":{";
        bool pfirst = true;
        for (const auto& [k, v] : node.params) {
            if (!pfirst) ss << ",";
            pfirst = false;
            ss << "\"" << escape_json_string(k) << "\":\""
               << escape_json_string(v) << "\"";
        }
        ss << "}";

        // Item filter (if non-empty).
        if (!node.item_filter.item_ids.empty()) {
            ss << ",\"item_filter\":{\"mode\":"
               << static_cast<int>(node.item_filter.mode) << ",\"ids\":[";
            bool ffirst = true;
            for (auto iid : node.item_filter.item_ids) {
                if (!ffirst) ss << ",";
                ffirst = false;
                ss << iid;
            }
            ss << "]}";
        }
        // Fluid filter (if non-empty).
        if (!node.fluid_filter.fluid_ids.empty()) {
            ss << ",\"fluid_filter\":{\"mode\":"
               << static_cast<int>(node.fluid_filter.mode) << ",\"ids\":[";
            bool ffirst = true;
            for (auto fid : node.fluid_filter.fluid_ids) {
                if (!ffirst) ss << ",";
                ffirst = false;
                ss << fid;
            }
            ss << "]}";
        }
        ss << "}";
    }
    ss << "],\"connections\":[";
    first = true;
    for (const auto& [id, conn] : connections_) {
        if (!first) ss << ",";
        first = false;
        ss << "{\"id\":" << id
           << ",\"from_node\":" << conn.from_node
           << ",\"from_port\":" << static_cast<int>(conn.from_port)
           << ",\"to_node\":" << conn.to_node
           << ",\"to_port\":" << static_cast<int>(conn.to_port)
           << "}";
    }
    ss << "]}";
    return ss.str();
}

// ============================================================
// Minimal JSON parser (sufficient for FlowProgram serialization)
// ============================================================

namespace {

class JsonParser {
public:
    JsonParser(const std::string& s) : s_(s), pos_(0) {}

    bool parse_object(std::function<void(const std::string&)> on_key) {
        skip_ws();
        if (!expect('{')) return false;
        skip_ws();
        if (peek() == '}') { pos_++; return true; }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (!expect(':')) return false;
            on_key(key);
            skip_ws();
            char c = peek();
            if (c == ',') { pos_++; continue; }
            if (c == '}') { pos_++; return true; }
            return false;
        }
    }

    bool parse_array(std::function<void(size_t)> on_item) {
        skip_ws();
        if (!expect('[')) return false;
        skip_ws();
        if (peek() == ']') { pos_++; return true; }
        size_t idx = 0;
        while (true) {
            on_item(idx++);
            skip_ws();
            char c = peek();
            if (c == ',') { pos_++; continue; }
            if (c == ']') { pos_++; return true; }
            return false;
        }
    }

    bool parse_string(std::string& out) {
        skip_ws();
        if (!expect('"')) return false;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        return false;
    }

    bool parse_number(int64_t& out) {
        skip_ws();
        size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) pos_++;
        while (pos_ < s_.size() && (s_[pos_] >= '0' && s_[pos_] <= '9')) pos_++;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            pos_++;
            while (pos_ < s_.size() && (s_[pos_] >= '0' && s_[pos_] <= '9')) pos_++;
        }
        if (pos_ == start) return false;
        try {
            out = std::stoll(s_.substr(start, pos_ - start));
        } catch (...) {
            return false;
        }
        return true;
    }

    bool parse_double(double& out) {
        skip_ws();
        size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) pos_++;
        while (pos_ < s_.size() && (s_[pos_] >= '0' && s_[pos_] <= '9')) pos_++;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            pos_++;
            while (pos_ < s_.size() && (s_[pos_] >= '0' && s_[pos_] <= '9')) pos_++;
        }
        if (pos_ == start) return false;
        try {
            out = std::stod(s_.substr(start, pos_ - start));
        } catch (...) {
            return false;
        }
        return true;
    }

    void skip_value() {
        skip_ws();
        if (pos_ >= s_.size()) return;
        char c = s_[pos_];
        if (c == '{') {
            pos_++;
            int depth = 1;
            while (pos_ < s_.size() && depth > 0) {
                if (s_[pos_] == '{') depth++;
                else if (s_[pos_] == '}') depth--;
                else if (s_[pos_] == '"') {
                    pos_++;
                    while (pos_ < s_.size() && s_[pos_] != '"') {
                        if (s_[pos_] == '\\') pos_++;
                        pos_++;
                    }
                }
                pos_++;
            }
        } else if (c == '[') {
            pos_++;
            int depth = 1;
            while (pos_ < s_.size() && depth > 0) {
                if (s_[pos_] == '[') depth++;
                else if (s_[pos_] == ']') depth--;
                else if (s_[pos_] == '"') {
                    pos_++;
                    while (pos_ < s_.size() && s_[pos_] != '"') {
                        if (s_[pos_] == '\\') pos_++;
                        pos_++;
                    }
                }
                pos_++;
            }
        } else if (c == '"') {
            pos_++;
            while (pos_ < s_.size() && s_[pos_] != '"') {
                if (s_[pos_] == '\\') pos_++;
                pos_++;
            }
            if (pos_ < s_.size()) pos_++;
        } else {
            while (pos_ < s_.size() && s_[pos_] != ',' && s_[pos_] != '}'
                   && s_[pos_] != ']' && !isspace(static_cast<unsigned char>(s_[pos_]))) {
                pos_++;
            }
        }
    }

private:
    void skip_ws() {
        while (pos_ < s_.size() && isspace(static_cast<unsigned char>(s_[pos_]))) pos_++;
    }
    char peek() { return pos_ < s_.size() ? s_[pos_] : '\0'; }
    bool expect(char c) {
        if (peek() != c) return false;
        pos_++;
        return true;
    }

    const std::string& s_;
    size_t pos_;
};

} // anonymous namespace

bool FlowProgram::from_json(const std::string& json) {
    clear();
    JsonParser p(json);

    return p.parse_object([&](const std::string& key) {
        if (key == "nodes") {
            p.parse_array([&](size_t) {
                FlowNodeId nid = 0;
                FlowNodeType ntype = FlowNodeType::TRIGGER_TIMER;
                float x = 0, y = 0;
                std::unordered_map<std::string, std::string> params;
                ItemFilterDef ifilter;
                FluidFilterDef ffilter;
                bool has_ifilter = false, has_ffilter = false;

                p.parse_object([&](const std::string& nkey) {
                    if (nkey == "id") {
                        int64_t v; p.parse_number(v); nid = static_cast<FlowNodeId>(v);
                    } else if (nkey == "type") {
                        int64_t v; p.parse_number(v);
                        ntype = static_cast<FlowNodeType>(v);
                    } else if (nkey == "x") {
                        double v; p.parse_double(v); x = static_cast<float>(v);
                    } else if (nkey == "y") {
                        double v; p.parse_double(v); y = static_cast<float>(v);
                    } else if (nkey == "params") {
                        p.parse_object([&](const std::string& pk) {
                            std::string pv;
                            p.parse_string(pv);
                            params[pk] = pv;
                        });
                    } else if (nkey == "item_filter") {
                        has_ifilter = true;
                        p.parse_object([&](const std::string& fk) {
                            if (fk == "mode") {
                                int64_t v; p.parse_number(v);
                                ifilter.mode = static_cast<FilterMode>(v);
                            } else if (fk == "ids") {
                                p.parse_array([&](size_t) {
                                    int64_t v; p.parse_number(v);
                                    ifilter.item_ids.push_back(static_cast<gt::ItemId>(v));
                                });
                            } else {
                                p.skip_value();
                            }
                        });
                    } else if (nkey == "fluid_filter") {
                        has_ffilter = true;
                        p.parse_object([&](const std::string& fk) {
                            if (fk == "mode") {
                                int64_t v; p.parse_number(v);
                                ffilter.mode = static_cast<FilterMode>(v);
                            } else if (fk == "ids") {
                                p.parse_array([&](size_t) {
                                    int64_t v; p.parse_number(v);
                                    ffilter.fluid_ids.push_back(static_cast<gt::FluidId>(v));
                                });
                            } else {
                                p.skip_value();
                            }
                        });
                    } else {
                        p.skip_value();
                    }
                });

                // Reconstruct node via factory then override fields.
                FlowNode node = FlowNodeFactory::create(nid, ntype);
                node.editor_x = x;
                node.editor_y = y;
                node.params = std::move(params);
                if (has_ifilter) node.item_filter = ifilter;
                if (has_ffilter) node.fluid_filter = ffilter;
                nodes_[nid] = std::move(node);
                if (nid >= next_node_id_) next_node_id_ = nid + 1;
            });
        } else if (key == "connections") {
            p.parse_array([&](size_t) {
                FlowConnectionId cid = 0;
                FlowNodeId from_node = 0, to_node = 0;
                FlowPortId from_port = 0, to_port = 0;
                p.parse_object([&](const std::string& ckey) {
                    if (ckey == "id") {
                        int64_t v; p.parse_number(v); cid = static_cast<FlowConnectionId>(v);
                    } else if (ckey == "from_node") {
                        int64_t v; p.parse_number(v); from_node = static_cast<FlowNodeId>(v);
                    } else if (ckey == "from_port") {
                        int64_t v; p.parse_number(v); from_port = static_cast<FlowPortId>(v);
                    } else if (ckey == "to_node") {
                        int64_t v; p.parse_number(v); to_node = static_cast<FlowNodeId>(v);
                    } else if (ckey == "to_port") {
                        int64_t v; p.parse_number(v); to_port = static_cast<FlowPortId>(v);
                    } else {
                        p.skip_value();
                    }
                });

                // Re-validate and rebuild indices.
                FlowPortType ptype = FlowPortType::NONE;
                const FlowNode* src = get_node(from_node);
                const FlowNode* dst = get_node(to_node);
                if (src && dst) {
                    const FlowPort* sp = src->find_output_port(from_port);
                    const FlowPort* dp = dst->find_input_port(to_port);
                    if (sp && dp && sp->type == dp->type && sp->type != FlowPortType::NONE) {
                        ptype = sp->type;
                    }
                }
                if (cid == 0) cid = next_conn_id_++;
                FlowConnection conn;
                conn.id = cid;
                conn.from_node = from_node;
                conn.from_port = from_port;
                conn.to_node = to_node;
                conn.to_port = to_port;
                conn.port_type = ptype;
                connections_[cid] = conn;
                out_index_[port_key(from_node, from_port)].push_back(cid);
                in_index_[port_key(to_node, to_port)] = cid;
                if (cid >= next_conn_id_) next_conn_id_ = cid + 1;
            });
        } else {
            p.skip_value();
        }
    });

    return true;
}

} // namespace science_and_theology::sfm
