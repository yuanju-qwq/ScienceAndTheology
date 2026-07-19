# ServerMain — Dedicated server entry point for headless mode.
#
# Legacy server entry; current multiplayer protocol: docs/游戏网络协议设计.md.
#
# Usage:
#   godot --headless --main-scene res://ServerScene.tscn -- --port 8910 --password secret --seed 12345
#
# This script sets up the authoritative server pipeline without any rendering
# nodes:
#   GDWorldData → GameCommandServer + GDTickSystem
#   GDNetworkServer wraps snt_server::ServerCore, bridging network frames
#   to command execution and per-observer delta production.
#
# Player lifecycle:
#   player_connected signal → create inventory/equipment, register_player,
#                             add_player_chunk (default spawn at origin)
#   player_disconnected signal → remove_player_chunk (unregister_player is
#                                 already called in C++ on_disconnect)
extends Node

const BuiltinTerrainContent := preload("res://scripts/worldgen/BuiltinTerrainContent.gd")
const SolarSystemPresetScript := preload("res://scripts/world/SolarSystemPreset.gd")

const DEFAULT_TCP_PORT := 8910
const DEFAULT_UDP_PORT := 8911
const DEFAULT_WORLD_SEED := 0
const INVENTORY_WIDTH := 9
const INVENTORY_HEIGHT := 4
const SPAWN_DIMENSION := "planet_earth"
const SPAWN_CX := 0
const SPAWN_CY := 0
const SPAWN_CZ := 0

@onready var _command_server: GameCommandServer = $GameCommandServer
@onready var _tick_system: GDTickSystem = $GDTickSystem
@onready var _net_server: GDNetworkServer = $GDNetworkServer

var _world_data: GDWorldData = null
var _tick_accumulator: float = 0.0
const TICK_RATE := 20.0  # 20 ticks per second
const TICK_INTERVAL := 1.0 / TICK_RATE


func _ready() -> void:
	var args := _parse_args()
	_setup_world(args.seed)
	_setup_systems()
	_setup_network(args)
	print("[ServerMain] ready — listening on tcp:%d udp:%d" % [args.tcp_port, args.udp_port])


func _process(delta: float) -> void:
	# Poll network (non-blocking, drains all ready sockets).
	_net_server.poll()

	# Drive simulation at fixed tick rate.
	_tick_accumulator += delta
	while _tick_accumulator >= TICK_INTERVAL:
		_tick_accumulator -= TICK_INTERVAL
		_tick_system.tick(TICK_INTERVAL)

	# Process async world generation results.
	if _world_data:
		_world_data.process_async_results()


# --- Setup ---

func _setup_world(seed: int) -> void:
	var resolved_seed := seed if seed != 0 else randi()
	_world_data = GDWorldData.new()
	_world_data.seed = resolved_seed
	_world_data.set_max_async_results_per_frame(8)

	var solar_system := SolarSystemPresetScript.generate(resolved_seed)
	var config := BuiltinTerrainContent.create_config_for_universe(solar_system.planets)
	_world_data.worldgen_config = config

	# Generate spawn-area chunks so players have terrain immediately.
	_world_data.request_chunk_async(SPAWN_DIMENSION, SPAWN_CX, SPAWN_CY, SPAWN_CZ)


func _setup_systems() -> void:
	# GameCommandServer: inject world data.
	# (GameCommandServer.gd's _configure_server tries ChunkRendererBridge
	# which doesn't exist here, so we set world_data explicitly.)
	_command_server.set_world_data(_world_data)

	# GDTickSystem: inject world data + register all subsystems.
	_tick_system.set_world_data(_world_data)
	_tick_system.register_day_night_system()
	_tick_system.register_region_system()
	_tick_system.register_tree_growth_system()
	_tick_system.register_crop_growth_system()
	_tick_system.register_season_system()
	_tick_system.register_ecosystem_system()


func _setup_network(args: Args) -> void:
	_net_server.set_command_server(_command_server)
	_net_server.set_tick_system(_tick_system)
	if args.password != "":
		_net_server.set_password(args.password)
	_net_server.set_server_name(args.server_name)
	_net_server.player_connected.connect(_on_player_connected)
	_net_server.player_disconnected.connect(_on_player_disconnected)
	var ok := _net_server.start(args.tcp_port, args.udp_port)
	if not ok:
		push_error("[ServerMain] failed to start network server on tcp:%d" % args.tcp_port)
		get_tree().quit(1)


# --- Player lifecycle ---

func _on_player_connected(player_handle: int) -> void:
	print("[ServerMain] player %d connected, registering..." % player_handle)
	var inventory := GDPlayerInventory.new()
	inventory.init(INVENTORY_WIDTH, INVENTORY_HEIGHT)
	var equipment := GDPlayerEquipment.new()
	_command_server.register_player(player_handle, inventory, equipment)
	_tick_system.add_player_chunk(player_handle, SPAWN_DIMENSION,
		SPAWN_CX, SPAWN_CY, SPAWN_CZ)


func _on_player_disconnected(player_handle: int) -> void:
	print("[ServerMain] player %d disconnected, cleaning up..." % player_handle)
	_tick_system.remove_player_chunk(player_handle)
	# unregister_player is already called in C++ GDNetworkServer::on_disconnect.


# --- Command-line argument parsing ---

class Args:
	var tcp_port: int = DEFAULT_TCP_PORT
	var udp_port: int = DEFAULT_UDP_PORT
	var password: String = ""
	var seed: int = DEFAULT_WORLD_SEED
	var server_name: String = "ScienceAndTheology Server"

func _parse_args() -> Args:
	var args := Args.new()
	var user_args := OS.get_cmdline_user_args()
	var i := 0
	while i < user_args.size():
		var a: String = user_args[i]
		match a:
			"--port", "--tcp-port":
				if i + 1 < user_args.size():
					args.tcp_port = int(user_args[i + 1])
					i += 1
			"--udp-port":
				if i + 1 < user_args.size():
					args.udp_port = int(user_args[i + 1])
					i += 1
			"--password":
				if i + 1 < user_args.size():
					args.password = user_args[i + 1]
					i += 1
			"--seed":
				if i + 1 < user_args.size():
					args.seed = int(user_args[i + 1])
					i += 1
			"--name", "--server-name":
				if i + 1 < user_args.size():
					args.server_name = user_args[i + 1]
					i += 1
		i += 1
	return args
