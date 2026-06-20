#include "gd_prediction_buffer.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace science_and_theology {

GDPredictionBuffer::GDPredictionBuffer()
    : buffer_(std::make_unique<PredictionBuffer>()) {}

GDPredictionBuffer::~GDPredictionBuffer() = default;

void GDPredictionBuffer::predict(int64_t client_tick) {
    if (buffer_) {
        buffer_->predict(static_cast<uint64_t>(client_tick));
    }
}

godot::PackedInt64Array GDPredictionBuffer::confirm(int64_t client_tick) {
    godot::PackedInt64Array arr;
    if (!buffer_) return arr;
    auto confirmed = buffer_->confirm(static_cast<uint64_t>(client_tick));
    arr.resize(static_cast<int64_t>(confirmed.size()));
    auto* ptr = arr.ptrw();
    for (size_t i = 0; i < confirmed.size(); ++i) {
        ptr[i] = static_cast<int64_t>(confirmed[i]);
    }
    return arr;
}

godot::PackedInt64Array GDPredictionBuffer::reject_and_rollback(
    int64_t client_tick) const {
    godot::PackedInt64Array arr;
    if (!buffer_) return arr;
    auto to_reapply = buffer_->reject_and_rollback(
        static_cast<uint64_t>(client_tick));
    arr.resize(static_cast<int64_t>(to_reapply.size()));
    auto* ptr = arr.ptrw();
    for (size_t i = 0; i < to_reapply.size(); ++i) {
        ptr[i] = static_cast<int64_t>(to_reapply[i]);
    }
    return arr;
}

int64_t GDPredictionBuffer::get_last_confirmed_tick() const {
    return buffer_ ? static_cast<int64_t>(buffer_->last_confirmed_tick()) : 0;
}

int64_t GDPredictionBuffer::get_pending_count() const {
    return buffer_ ? static_cast<int64_t>(buffer_->pending_count()) : 0;
}

godot::PackedInt64Array GDPredictionBuffer::get_pending_ticks() const {
    godot::PackedInt64Array arr;
    if (!buffer_) return arr;
    const auto& pending = buffer_->pending_ticks();
    arr.resize(static_cast<int64_t>(pending.size()));
    auto* ptr = arr.ptrw();
    for (size_t i = 0; i < pending.size(); ++i) {
        ptr[i] = static_cast<int64_t>(pending[i]);
    }
    return arr;
}

void GDPredictionBuffer::clear() {
    if (buffer_) buffer_->clear();
}

void GDPredictionBuffer::clear_pending() {
    if (buffer_) buffer_->clear_pending();
}

void GDPredictionBuffer::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("predict", "client_tick"),
        &GDPredictionBuffer::predict);
    godot::ClassDB::bind_method(
        godot::D_METHOD("confirm", "client_tick"),
        &GDPredictionBuffer::confirm);
    godot::ClassDB::bind_method(
        godot::D_METHOD("reject_and_rollback", "client_tick"),
        &GDPredictionBuffer::reject_and_rollback);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_last_confirmed_tick"),
        &GDPredictionBuffer::get_last_confirmed_tick);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_pending_count"),
        &GDPredictionBuffer::get_pending_count);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_pending_ticks"),
        &GDPredictionBuffer::get_pending_ticks);
    godot::ClassDB::bind_method(
        godot::D_METHOD("clear"),
        &GDPredictionBuffer::clear);
    godot::ClassDB::bind_method(
        godot::D_METHOD("clear_pending"),
        &GDPredictionBuffer::clear_pending);
}

} // namespace science_and_theology
