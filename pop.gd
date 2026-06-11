extends StaticBody3D

# Configuration
@export var grid_size: int = 19
@export var websocket_url: String = "ws://localhost:8000"

# Game State
var is_black_turn: bool = true 
var board_state: Array = []       
var visual_pieces: Dictionary = {} 

# Node References
@onready var collision_shape: CollisionShape3D = $CollisionShape3D
var playable_grid_size: float = 0.0

# WebSocket Implementation
var socket = WebSocketPeer.new()
var last_state = WebSocketPeer.STATE_CLOSED

func _ready() -> void:
	# Initialize Data Structure
	for x in range(grid_size):
		var column = []
		for z in range(grid_size):
			column.append(0)
		board_state.append(column)
		
	# Attempt to connect to the backend
	print("Connecting to WebSocket at ", websocket_url, "...")
	var err = socket.connect_to_url(websocket_url)
	if err != OK:
		push_error("Unable to connect to WebSocket. Error code: %d" % err)
	
	if collision_shape and collision_shape.shape is BoxShape3D:
		playable_grid_size = collision_shape.shape.size.x
	else:
		push_error("Error: CollisionShape3D is missing or does not use a BoxShape3D!")

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
			print("✅ Connected to WebSocket server")
		WebSocketPeer.STATE_CLOSED:
			var code = socket.get_close_code()
			var reason = socket.get_close_reason()
			print("❌ Disconnected from WebSocket server. Code: %d, Reason: %s" % [code, reason])
		WebSocketPeer.STATE_CONNECTING:
			print("⏳ Connecting to server...")
		WebSocketPeer.STATE_CLOSING:
			print("🔌 Closing connection...")

func _input_event(camera: Node, event: InputEvent, event_position: Vector3, normal: Vector3, shape_idx: int) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		_handle_interaction(event_position)

func _handle_interaction(click_pos_global: Vector3) -> void:
	if playable_grid_size <= 0.0:
		return
		
	var local_pos = to_local(click_pos_global)
	var cell_size = playable_grid_size / float(grid_size - 1)
	var grid_half_size = playable_grid_size / 2.0
	
	var x_offset = local_pos.x + grid_half_size
	var z_offset = local_pos.z + grid_half_size
	
	var grid_x = round(x_offset / cell_size)
	var grid_z = round(z_offset / cell_size)
	
	if grid_x >= 0 and grid_x < grid_size and grid_z >= 0 and grid_z < grid_size:
		# Validation Checker
		if board_state[grid_x][grid_z] != 0:
			return 
			
		_place_piece_visually(grid_x, grid_z, local_pos.y)

func _place_piece_visually(grid_x: int, grid_z: int, surface_height: float) -> void:
	var piece = MeshInstance3D.new()
	var mesh = SphereMesh.new()
	mesh.radius = 0.008
	mesh.height = 0.005 
	piece.mesh = mesh
	
	var material = StandardMaterial3D.new()
	var player_id: int
	
	if is_black_turn:
		material.albedo_color = Color(0.1, 0.1, 0.1) 
		player_id = 2 
	else:
		material.albedo_color = Color(0.8, 0.8, 0.8) 
		player_id = 1 
		
	material.roughness = 0.2 
	piece.material_override = material
	
	add_child(piece)
	
	var cell_size = playable_grid_size / float(grid_size - 1)
	var grid_half_size = playable_grid_size / 2.0
	var exact_x = (grid_x * cell_size) - grid_half_size
	var exact_z = (grid_z * cell_size) - grid_half_size
	
	piece.position = Vector3(exact_x, surface_height, exact_z)
	
	board_state[grid_x][grid_z] = player_id
	
	var coords = Vector2i(grid_x, grid_z)
	visual_pieces[coords] = piece
	
	is_black_turn = !is_black_turn
	
	# Send the move to the server
	_send_state_to_backend(grid_x, grid_z, player_id)

func remove_piece(grid_x: int, grid_z: int) -> void:
	var coords = Vector2i(grid_x, grid_z)
	
	if visual_pieces.has(coords):
		var piece_to_remove = visual_pieces[coords]
		piece_to_remove.queue_free() 
		visual_pieces.erase(coords)  
		
		board_state[grid_x][grid_z] = 0

# WebSocket Transmission
func _send_state_to_backend(last_x: int, last_z: int, player: int) -> void:
	if socket.get_ready_state() != WebSocketPeer.STATE_OPEN:
		push_warning("WebSocket is not currently connected. Move was registered locally but not sent.")
		return

	var payload = {
		"action": "player_move",
		"last_move": {"x": last_x, "z": last_z, "player": player},
		"board": board_state
	}
	
	var json_data = JSON.stringify(payload)
	
	# Packets must be sent as raw bytes (UTF-8 buffer)
	socket.put_packet(json_data.to_utf8_buffer())

# WebSocket Reception
func _on_message_received(message: String) -> void:
	var json = JSON.new()
	var parse_result = json.parse(message)
	
	if parse_result == OK:
		var response_data = json.data
		print("📨 Server said: ", response_data)
		# Trigger update logic here
	else:
		push_error("Failed to parse incoming JSON from server.")
