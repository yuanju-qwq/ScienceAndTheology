# GameSession — Autoload singleton that stores the current game session data.
# Persists the selected world name, save path, and universe configuration
# across scene transitions (MainMenu → WorldMap).
extends Node

# The display name of the currently selected world.
var world_name: String = ""

# The absolute path to the world's save directory (e.g., user://saves/MyWorld/).
var save_path: String = ""

# Universe generation mode: "solar_system" or "random".
var universe_mode: String = "solar_system"

# Universe seed for deterministic generation. 0 means auto-generate.
var universe_seed: int = 0

# Density of star systems in the universe grid (random mode only).
# Fraction of grid cells that contain a star system (0.05 - 0.8).
# Default: 0.0 (use SpatialUniverseGrid.DEFAULT_DENSITY).
var system_density: float = 0.0
