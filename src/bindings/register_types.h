#pragma once

#include <godot_cpp/core/class_db.hpp>

namespace science_and_theology {

void initialize_snt_extension(godot::ModuleInitializationLevel p_level);
void uninitialize_snt_extension(godot::ModuleInitializationLevel p_level);

} // namespace science_and_theology