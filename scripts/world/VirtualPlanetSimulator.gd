# VirtualPlanetSimulator — advances time for distant unloaded planets.
# When a planet's chunk data is serialized to disk and unloaded from memory,
# this simulator uses the PlanetSummary to approximate factory output
# without requiring the full chunk data or machine tick.
#
# The simulator runs at a reduced cadence (e.g., once per second) and
# accumulates fractional production. When the player returns to the planet,
# the accumulated production is reconciled into the actual game state.
class_name VirtualPlanetSimulator
extends Node

# How often (in seconds) the virtual tick runs for each planet.
# Lower values = more accurate but more CPU overhead.
# 1.0 second is a good default for a factory game.
@export var virtual_tick_interval: float = 1.0

# Maximum virtual time (in seconds) that can be accumulated per planet
# before the summary is considered stale. Prevents unbounded accumulation
# when a player is away for a very long time.
@export var max_virtual_time_seconds: float = 3600.0 * 24.0

# Active planet summaries keyed by dimension_id.
var _summaries: Dictionary = {}

# Elapsed time since last virtual tick for each planet.
var _elapsed: Dictionary = {}

# Total virtual seconds elapsed per planet since unload.
var _total_virtual_time: Dictionary = {}

var _debug_elapsed := 0.0


func _process(delta: float) -> void:
	_advance_all_virtual_time(delta)


# --- Public API ---

# Register a planet for virtual simulation.
# Called when a planet is about to be serialized and unloaded.
func register_planet(summary: PlanetSummary) -> void:
	var dim := summary.dimension_id
	_summaries[dim] = summary
	_elapsed[dim] = 0.0
	_total_virtual_time[dim] = 0.0


# Remove a planet from virtual simulation.
# Called when a planet is loaded back into memory.
func unregister_planet(dimension_id: StringName) -> void:
	_summaries.erase(dimension_id)
	_elapsed.erase(dimension_id)
	_total_virtual_time.erase(dimension_id)


# Get the summary for a planet (for reconciliation).
func get_summary(dimension_id: StringName) -> PlanetSummary:
	return _summaries.get(dimension_id)


# Check if a planet is currently being virtually simulated.
func is_simulating(dimension_id: StringName) -> bool:
	return _summaries.has(dimension_id)


# Get the total virtual time elapsed for a planet.
func get_virtual_time(dimension_id: StringName) -> float:
	return _total_virtual_time.get(dimension_id, 0.0)


# Get all currently simulated dimension IDs.
func get_simulated_dimensions() -> Array[StringName]:
	var result: Array[StringName] = []
	for dim in _summaries.keys():
		result.append(dim)
	return result


# --- Virtual tick ---

func _advance_all_virtual_time(delta: float) -> void:
	for dim in _summaries.keys():
		var summary: PlanetSummary = _summaries[dim]
		if not summary.has_production():
			continue

		_elapsed[dim] = _elapsed.get(dim, 0.0) + delta

		# Only run the virtual tick at the configured interval.
		if _elapsed[dim] < virtual_tick_interval:
			continue

		var tick_delta: float = _elapsed[dim]
		_elapsed[dim] = 0.0

		# Cap virtual time to prevent unbounded accumulation.
		var total_time: float = _total_virtual_time.get(dim, 0.0) + tick_delta
		if total_time > max_virtual_time_seconds:
			tick_delta = maxf(0.0, max_virtual_time_seconds - _total_virtual_time.get(dim, 0.0))
			if tick_delta <= 0.0:
				continue

		_total_virtual_time[dim] = _total_virtual_time.get(dim, 0.0) + tick_delta
		summary.advance_virtual_time(tick_delta)
