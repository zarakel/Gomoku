#include "../include/gomoku.h"
#include <cjson/cJSON.h>

// Mapping helper: Godot 2D board [x][z] to C 1D board [index]
static void sync_board_from_godot(game *gameData, cJSON *board_2d)
{
    if (!board_2d || !cJSON_IsArray(board_2d)) return;
    
    int x_size = cJSON_GetArraySize(board_2d);
    for (int x = 0; x < x_size && x < BOARD_SIZE; x++)
    {
        cJSON *col = cJSON_GetArrayItem(board_2d, x);
        if (col && cJSON_IsArray(col))
        {
            int z_size = cJSON_GetArraySize(col);
            for (int z = 0; z < z_size && z < BOARD_SIZE; z++)
            {
                cJSON *val = cJSON_GetArrayItem(col, z);
                if (val)
                {
                    // Godot uses [x][z]. C uses index = z * 19 + x
                    gameData->board[z * BOARD_SIZE + x] = val->valueint;
                }
            }
        }
    }
}

// Broadcast board state to all connected clients
static void broadcast_board_state(struct mg_mgr *mgr, game *gameData, screen *windows)
{
    if (!mgr) return;
    
    cJSON *json = cJSON_CreateObject();
    
    // Create board array (sending back 1D to keep it simple, front can parse)
    cJSON *board_array = cJSON_CreateArray();
    for (int i = 0; i < MAX_BOARD; i++)
        cJSON_AddItemToArray(board_array, cJSON_CreateNumber(gameData->board[i]));
    
    cJSON_AddItemToObject(json, "board", board_array);
    cJSON_AddNumberToObject(json, "turn", gameData->turn);
    cJSON_AddNumberToObject(json, "captures_p1", gameData->captures[P1]);
    cJSON_AddNumberToObject(json, "captures_p2", gameData->captures[P2]);
    cJSON_AddBoolToObject(json, "game_over", gameData->game_over);
    cJSON_AddNumberToObject(json, "hint_idx", gameData->hint_idx);
    
    char *json_str = cJSON_Print(json);
    
    for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next)
    {
        if (c->is_websocket)
            mg_ws_send(c, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
    }
    
    free(json_str);
    cJSON_Delete(json);
}

void broadcast_board_state_external(struct mg_mgr *mgr, game *gameData, screen *windows)
{
    broadcast_board_state(mgr, gameData, windows);
}

static void handle_client_message(struct mg_connection *c, const char *msg, both *args)
{
    cJSON *json = cJSON_Parse(msg);
    if (!json) return;
    
    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (!action || action->type != cJSON_String)
    {
        cJSON_Delete(json);
        return;
    }
    
    // Handle Godot's "player_move"
    if (strcmp(action->valuestring, "player_move") == 0 || strcmp(action->valuestring, "play") == 0)
    {
        // 1. Sync board from frontend
        cJSON *board_2d = cJSON_GetObjectItem(json, "board");
        if (board_2d) {
            sync_board_from_godot(args->gameData, board_2d);
        }

        // 2. Handle the move logic (captures, victory, etc.)
        cJSON *last_move = cJSON_GetObjectItem(json, "last_move");
        if (last_move)
        {
            cJSON *x_obj = cJSON_GetObjectItem(last_move, "x");
            cJSON *z_obj = cJSON_GetObjectItem(last_move, "z");
            if (x_obj && z_obj)
            {
                int x = x_obj->valueint;
                int z = z_obj->valueint;
                int idx = GET_INDEX(x, z);

                // Check for captures resulting from this move
                int capture_indices[10];
                int caps = apply_captures_for_ai(args->gameData, x, z, args->gameData->turn, capture_indices);
                
                // If the backend has capture logic, it updates the board here
                // MLX Sync
                drawSquare(args->windows, x, z, args->gameData->board[idx]);
                for (int k = 0; k < caps; k++)
                    drawSquare(args->windows, GET_X(capture_indices[k]), GET_Y(capture_indices[k]), EMPTY);
            }
        }

        // 3. Victory check & Turn swap
        checkVictoryCondition(args->gameData);
        if (!args->gameData->game_over)
            args->gameData->turn = (args->gameData->turn == P1) ? P2 : P1;
        
        args->windows->changed = true;
        
        // 4. Respond with updated state
        broadcast_board_state(args->mgr, args->gameData, args->windows);
    }
    else if (strcmp(action->valuestring, "reset") == 0)
    {
        resetGame(args->gameData, args->windows);
        broadcast_board_state(args->mgr, args->gameData, args->windows);
    }
    
    cJSON_Delete(json);
}

void websocket_handler(struct mg_connection *c, int ev, void *ev_data)
{
    both *args = (both *)c->mgr->userdata;
    
    if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        mg_ws_upgrade(c, hm, NULL);
    }
    else if (ev == MG_EV_WS_OPEN)
    {
        broadcast_board_state(args->mgr, args->gameData, args->windows);
    }
    else if (ev == MG_EV_WS_MSG)
    {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        char msg_buffer[4096]; // Increased buffer for large board JSON
        int len = wm->data.len;
        if (len > 4095) len = 4095;
        memcpy(msg_buffer, wm->data.buf, len);
        msg_buffer[len] = '\0';
        handle_client_message(c, msg_buffer, args);
    }
}

void init_websocket(both *args)
{
    args->mgr = (struct mg_mgr *)malloc(sizeof(struct mg_mgr));
    if (!args->mgr) return;
    
    mg_mgr_init(args->mgr);
    args->mgr->userdata = args;

    // Listening on 8000 (8080 is often used for web servers)
    struct mg_connection *c = mg_http_listen(args->mgr, "http://0.0.0.0:8000", 
                                              websocket_handler, NULL);
    if (c == NULL)
    {
        free(args->mgr);
        args->mgr = NULL;
    }
}

void cleanup_websocket(both *args)
{
    if (args->mgr)
    {
        mg_mgr_free(args->mgr);
        free(args->mgr);
        args->mgr = NULL;
    }
}
