extends StaticBody3D

# Configuration
@export var grid_size: int = 19
@export var websocket_url: String = "ws://localhost:8000"

# Game State
var visual_pieces: Dictionary = {} # Map of Vector2i -> MeshInstance3D
var hint_piece: MeshInstance3D = null
var current_hint_idx: int = -1

# Ghost Piece
var ghost_piece: MeshInstance3D = null

# UI Nodes
var ui_layer: CanvasLayer
var info_label: Label
var status_label: Label

# Node References
@onready var collision_shape: CollisionShape3D = $CollisionShape3D
var playable_grid_size: float = 0.0

# WebSocket Implementation
var socket = WebSocketPeer.new()
var last_state = WebSocketPeer.STATE_CLOSED

func _ready() -> void:
	# Attempt to connect to the backend
	print("Connecting to WebSocket at ", websocket_url, "...")
	var err = socket.connect_to_url(websocket_url)
	if err != OK:
		push_error("Unable to connect to WebSocket. Error code: %d" % err)
	
	if collision_shape and collision_shape.shape is BoxShape3D:
		playable_grid_size = collision_shape.shape.size.x
	else:
		push_error("Error: CollisionShape3D is missing or does not use a BoxShape3D!")
		
	_setup_ghost_piece()
	_setup_ui()

func _setup_ghost_piece() -> void:
	ghost_piece = MeshInstance3D.new()
	var mesh = SphereMesh.new()
	mesh.radius = 0.008
	mesh.height = 0.005 
	ghost_piece.mesh = mesh
	
	var material = StandardMaterial3D.new()
	material.albedo_color = Color(1.0, 1.0, 1.0, 0.4) 
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	ghost_piece.material_override = material
	
	ghost_piece.visible = false
	add_child(ghost_piece)

func _setup_ui() -> void:
	ui_layer = CanvasLayer.new()
	add_child(ui_layer)
	
	var panel = Panel.new()
	panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	panel.custom_minimum_size = Vector2(250, 160)
	panel.position = Vector2(20, 20)
	ui_layer.add_child(panel)
	
	info_label = Label.new()
	info_label.text = "Initializing..."
	info_label.position = Vector2(15, 15)
	panel.add_child(info_label)
	
	status_label = Label.new()
	status_label.text = "Connecting..."
	status_label.position = Vector2(15, 130)
	status_label.add_theme_color_override("font_color", Color.YELLOW)
	panel.add_child(status_label)

# Continuous Polling Loop
func _process(_delta: float) -> void:
	socket.poll()
	var state = socket.get_ready_state()
	
	if state != last_state:
		_on_state_changed(state)
		last_state = state

	if state == WebSocketPeer.STATE_OPEN:
		while socket.get_available_packet_count():
			var message = socket.get_packet().get_string_from_utf8()
			_on_message_received(message)

func _on_state_changed(new_state: int) -> void:
	match new_state:
		WebSocketPeer.STATE_OPEN:
			status_label.text = "Status: Connected ✅"
			status_label.add_theme_color_override("font_color", Color.GREEN)
		WebSocketPeer.STATE_CLOSED:
			status_label.text = "Status: Disconnected ❌"
			status_label.add_theme_color_override("font_color", Color.RED)
		WebSocketPeer.STATE_CONNECTING:
			status_label.text = "Status: Connecting..."
			status_label.add_theme_color_override("font_color", Color.YELLOW)

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed:
		if event.keycode == KEY_R: _send_command("reset")
		elif event.keycode == KEY_H: _send_command("hint")
		elif event.keycode == KEY_I: _send_command("toggle_ia")

func _input_event(_camera: Node, event: InputEvent, event_position: Vector3, _normal: Vector3, _shape_idx: int) -> void:
	if event is InputEventMouseMotion:
		_update_ghost_piece(event_position)
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		_handle_interaction(event_position)

func _update_ghost_piece(click_pos_global: Vector3) -> void:
	if playable_grid_size <= 0.0: return
	var local_pos = to_local(click_pos_global)
	var cell_size = playable_grid_size / float(grid_size - 1)
	var grid_half_size = playable_grid_size / 2.0
	var grid_x = int(round((local_pos.x + grid_half_size) / cell_size))
	var grid_z = int(round((local_pos.z + grid_half_size) / cell_size))
	
	if grid_x >= 0 and grid_x < grid_size and grid_z >= 0 and grid_z < grid_size:
		ghost_piece.position = Vector3((grid_x * cell_size) - grid_half_size, 0.001, (grid_z * cell_size) - grid_half_size)
		ghost_piece.visible = true
	else:
		ghost_piece.visible = false

func _handle_interaction(click_pos_global: Vector3) -> void:
	if playable_grid_size <= 0.0: return
	var local_pos = to_local(click_pos_global)
	var cell_size = playable_grid_size / float(grid_size - 1)
	var grid_half_size = playable_grid_size / 2.0
	var grid_x = int(round((local_pos.x + grid_half_size) / cell_size))
	var grid_z = int(round((local_pos.z + grid_half_size) / cell_size))
	if grid_x >= 0 and grid_x < grid_size and grid_z >= 0 and grid_z < grid_size:
		_send_play_request(grid_x, grid_z)

func _place_piece_visually(grid_x: int, grid_z: int, player_id: int) -> void:
	var coords = Vector2i(grid_x, grid_z)
	if visual_pieces.has(coords): return
		
	var piece = MeshInstance3D.new()
	var mesh = SphereMesh.new()
	mesh.radius = 0.008
	mesh.height = 0.005 
	piece.mesh = mesh
	
	var material = StandardMaterial3D.new()
	material.albedo_color = Color(0.1, 0.1, 0.1) if player_id == 1 else Color(0.9, 0.9, 0.9)
	material.roughness = 0.2 
	piece.material_override = material
	
	add_child(piece)
	
	var cell_size = playable_grid_size / float(grid_size - 1)
	var grid_half_size = playable_grid_size / 2.0
	var target_pos = Vector3((grid_x * cell_size) - grid_half_size, 0.005, (grid_z * cell_size) - grid_half_size)
	
	# Static Placement: No tweening logic
	piece.position = target_pos
	piece.scale = Vector3.ONE 
	
	visual_pieces[coords] = piece

func _remove_piece_visually(grid_x: int, grid_z: int) -> void:
	var coords = Vector2i(grid_x, grid_z)
	if visual_pieces.has(coords):
		var piece = visual_pieces[coords]
		visual_pieces.erase(coords)
		
		# Immediate Deletion: No tweening logic
		piece.queue_free()

func _send_play_request(x: int, z: int) -> void:
	if socket.get_ready_state() != WebSocketPeer.STATE_OPEN: return
	
	# CORRECTION: Adjust action string to match legacy expectations if needed
	# Change "player_move" back to "play" if your server was actually updated
	var payload = {
		"action": "player_move", 
		"last_move": {"x": x, "z": z}
	}
	print("Godot: Sending move ", payload)
	socket.put_packet(JSON.stringify(payload).to_utf8_buffer())

func _send_command(command: String) -> void:
	if socket.get_ready_state() != WebSocketPeer.STATE_OPEN: return
	var payload = {"action": command}
	print("Godot: Sending command ", command)
	socket.put_packet(JSON.stringify(payload).to_utf8_buffer())

func _on_message_received(message: String) -> void:
	var json = JSON.new()
	if json.parse(message) == OK:
		var data = json.data
		if data.has("board"): _sync_board(data["board"])
		if data.has("hint_idx"): _handle_hint(int(data["hint_idx"]))
		_update_info_ui(data)
	else:
		push_error("Failed to parse incoming JSON")

func _update_info_ui(data: Dictionary) -> void:
	var turn_str = "Black" if data.get("turn", 1) == 1 else "White"
	var info = "Turn: %s\n" % turn_str
	info += "Captures P1: %d\n" % int(data.get("captures_p1", 0))
	info += "Captures P2: %d\n" % int(data.get("captures_p2", 0))
	info += "Game Over: %s" % ("YES" if data.get("game_over", false) else "NO")
	info_label.text = info

func _sync_board(board_array: Array) -> void:
	for i in range(board_array.size()):
		# CORRECTION: Explicitly typecast to integers
		var val: int = int(board_array[i])
		var x: int = i % grid_size
		var z: int = i / grid_size # Strict integer division
		var coords := Vector2i(x, z)
		
		if val == 0:
			if visual_pieces.has(coords): 
				print("Godot Sync: Removing piece at ", coords)
				_remove_piece_visually(coords.x, coords.y)
		else:
			if not visual_pieces.has(coords): 
				print("Godot Sync: Adding piece P%d at " % val, coords)
				_place_piece_visually(coords.x, coords.y, val)

func _handle_hint(hint_idx: int) -> void:
	if hint_idx == current_hint_idx: return
	current_hint_idx = hint_idx
	if hint_piece:
		hint_piece.queue_free()
		hint_piece = null
	if hint_idx != -1:
		var x = hint_idx % grid_size
		var z = hint_idx / grid_size
		hint_piece = MeshInstance3D.new()
		var mesh = SphereMesh.new()
		mesh.radius = 0.006
		mesh.height = 0.004
		hint_piece.mesh = mesh
		var material = StandardMaterial3D.new()
		material.albedo_color = Color(1, 1, 0, 0.6)
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.emission_enabled = true
		material.emission = Color(1, 1, 0)
		hint_piece.material_override = material
		add_child(hint_piece)
		var cell_size = playable_grid_size / float(grid_size - 1)
		var grid_half_size = playable_grid_size / 2.0
		hint_piece.position = Vector3((x * cell_size) - grid_half_size, 0.005, (z * cell_size) - grid_half_size)
