# GameSession — Autoload singleton that stores the current game session data.
# Persists the selected world name, save path, and universe configuration
# across scene transitions (MainMenu → WorldMap).
extends Node

# The display name of the currently selected world.
var world_name: String = ""

# The absolute path to the world's save directory (e.g., user://saves/MyWorld/).
var save_path: String = ""

# Current local identity snapshot. Player saves are keyed by player_save_key
# inside the selected world's players/ directory.
var identity: Dictionary = {}
var player_save_key: String = ""
var build_channel: String = "offline"
var can_host_lan: bool = false

# Universe generation mode: "solar_system" or "random".
var universe_mode: String = "solar_system"

# Universe seed for deterministic generation. 0 means auto-generate.
var universe_seed: int = 0

# Density of star systems in the universe grid (random mode only).
# Fraction of grid cells that contain a star system (0.05 - 0.8).
# Default: 0.0 (use SpatialUniverseGrid.DEFAULT_DENSITY).
var system_density: float = 0.0

# Initial game mode: 0=SURVIVAL, 1=CREATIVE, 2=OBSERVER.
var game_mode: int = 0

# GameplayConfig overrides applied on world init.
# Keys match GDWorldData.set_gameplay_config() dictionary fields.
var gameplay_config: Dictionary = {}

# Default console permission level: 0=PLAYER, 1=CHEATER, 2=OP.
var permission_level: int = 1

# Planet generation overrides (random mode only).
# Keys: size_preset, terrain_preset, sea_level_preset, cave_preset, atmosphere_preset.
# Each value is a string: "default" or a preset name.
var planet_overrides: Dictionary = {}
