# PlanetSummary — lightweight production summary for a distant planet.
# When a planet's chunk data is serialized to disk and unloaded from memory,
# this summary captures the essential production rates so that a virtual
# tick can approximate factory output without requiring the full chunk data.
#
# The summary is updated every time the player leaves a planet and is used
# by VirtualPlanetSimulator to advance time while the planet is unloaded.
class_name PlanetSummary
extends Resource

# Dimension ID this summary belongs to.
@export var dimension_id: StringName = &""

# Last known game-time tick when this summary was captured.
@export var captured_tick: int = 0

# Production entries: each entry describes one production line.
# Format: { "recipe_key": String, "rate_per_minute": float, "active_count": int }
@export var production_lines: Array[Dictionary] = []

# Power summary: { "consumption_mw": float, "generation_mw": float, "surplus_mw": float }
@export var power_summary: Dictionary = {}

# Storage levels: { "item_key": { "count": int, "capacity": int } }
# Only tracks items that are produced or consumed on this planet.
@export var storage_levels: Dictionary = {}

# Mining sites: { "ore_key": { "rate_per_minute": float, "remaining_approx": int } }
@export var mining_sites: Dictionary = {}

# Accumulated virtual production since unload.
# { "item_key": float } — can be fractional, rounded on reconciliation.
var _accumulated_production: Dictionary = {}

# Accumulated virtual consumption since unload.
# { "item_key": float } — can be fractional, rounded on reconciliation.
var _accumulated_consumption: Dictionary = {}


# Create a summary from the current state of a planet's factories.
# This should be called just before serializing and unloading the planet.
static func create_from_world(dimension: StringName, tick_count: int) -> PlanetSummary:
	var summary := PlanetSummary.new()
	summary.dimension_id = dimension
	summary.captured_tick = tick_count
	# TODO: Scan TickSystem's machine registry for this dimension
	# and populate production_lines, power_summary, storage_levels, mining_sites.
	# For now, create an empty summary as the baseline.
	return summary


# Advance virtual time by delta_seconds.
# Uses production rates to compute accumulated output without
# simulating individual machines.
func advance_virtual_time(delta_seconds: float) -> void:
	var minutes := delta_seconds / 60.0

	for line in production_lines:
		var rate: float = line.get("rate_per_minute", 0.0)
		var count: int = line.get("active_count", 0)
		if rate <= 0.0 or count <= 0:
			continue

		var recipe_key: String = line.get("recipe_key", "")
		var output_per_minute := rate * float(count)
		var output_this_tick := output_per_minute * minutes

		_accumulated_production[recipe_key] = \
			_accumulated_production.get(recipe_key, 0.0) + output_this_tick

	# Mining sites also produce.
	for ore_key in mining_sites.keys():
		var site: Dictionary = mining_sites[ore_key]
		var rate: float = site.get("rate_per_minute", 0.0)
		var remaining: int = site.get("remaining_approx", 0)
		if rate <= 0.0 or remaining <= 0:
			continue

		var produced := rate * minutes
		if produced > float(remaining):
			produced = float(remaining)
			site["rate_per_minute"] = 0.0
		else:
			site["remaining_approx"] = remaining - int(produced)

		_accumulated_production[ore_key] = \
			_accumulated_production.get(ore_key, 0.0) + produced


# Get the total accumulated production for an item since unload.
func get_accumulated_production(item_key: String) -> int:
	return int(_accumulated_production.get(item_key, 0.0))


# Get all accumulated production entries (for reconciliation).
func get_all_accumulated_production() -> Dictionary:
	var result: Dictionary = {}
	for key in _accumulated_production.keys():
		var amount := int(_accumulated_production[key])
		if amount > 0:
			result[key] = amount
	return result


# Clear accumulated production after reconciliation.
func clear_accumulated() -> void:
	_accumulated_production.clear()
	_accumulated_consumption.clear()


# Check if this planet has any active production.
func has_production() -> bool:
	for line in production_lines:
		if line.get("rate_per_minute", 0.0) > 0.0 and line.get("active_count", 0) > 0:
			return true
	for ore_key in mining_sites.keys():
		var site: Dictionary = mining_sites[ore_key]
		if site.get("rate_per_minute", 0.0) > 0.0 and site.get("remaining_approx", 0) > 0:
			return true
	return false


# Serialize to a Dictionary for save files.
func to_dict() -> Dictionary:
	return {
		"dimension_id": String(dimension_id),
		"captured_tick": captured_tick,
		"production_lines": production_lines,
		"power_summary": power_summary,
		"storage_levels": storage_levels,
		"mining_sites": mining_sites,
	}


# Deserialize from a Dictionary.
static func from_dict(data: Dictionary) -> PlanetSummary:
	var summary := PlanetSummary.new()
	summary.dimension_id = StringName(data.get("dimension_id", ""))
	summary.captured_tick = int(data.get("captured_tick", 0))
	summary.production_lines = data.get("production_lines", [])
	summary.power_summary = data.get("power_summary", {})
	summary.storage_levels = data.get("storage_levels", {})
	summary.mining_sites = data.get("mining_sites", {})
	return summary
