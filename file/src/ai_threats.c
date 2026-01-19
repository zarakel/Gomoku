#include "../include/gomoku.h"

// ============================================================================
// ANALYSE DES MENACES EXISTANTES
// ============================================================================

int scan_existing_threats(game *g, int player, int *block_idx) {
    int max_threat = 0;
    *block_idx = -1;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player) continue;
            
            int stones = 1;
            int empty_end = -1;
            int empty_start = -1;
            bool start_open = false;
            bool end_open = false;
            
            for (int k = 1; k < 5; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones++;
                else if (cell == EMPTY) {
                    end_open = true;
                    empty_end = GET_INDEX(nx, ny);
                    break;
                }
                else break;
            }
            
            int bx = x - dx[d];
            int by = y - dy[d];
            if (IS_VALID(bx, by) && g->board[GET_INDEX(bx, by)] == EMPTY) {
                start_open = true;
                empty_start = GET_INDEX(bx, by);
            }
            
            int open_ends = (start_open ? 1 : 0) + (end_open ? 1 : 0);
            
            int threat_score = 0;
            int best_block = -1;
            
            if (stones >= 4) {
                if (open_ends >= 1) {
                    threat_score = CLOSED_FOUR;
                    best_block = (empty_end != -1) ? empty_end : empty_start;
                }
            }
            else if (stones == 3) {
                if (open_ends == 2) {
                    threat_score = OPEN_THREE;
                    best_block = (empty_end != -1) ? empty_end : empty_start;
                }
                else if (open_ends == 1) {
                    threat_score = CLOSED_THREE;
                    best_block = (empty_end != -1) ? empty_end : empty_start;
                }
            }
            
            if (threat_score > max_threat) {
                max_threat = threat_score;
                *block_idx = best_block;
            }
        }
    }
    
    return max_threat;
}

void find_all_threats(game *g, int player, int *best_idx, int *best_score) {
    *best_idx = -1;
    *best_score = 0;
    
    // 1. Scanner les menaces EXISTANTES
    int existing_block = -1;
    int existing_threat = scan_existing_threats(g, player, &existing_block);
    
    if (existing_threat > *best_score) {
        *best_score = existing_threat;
        *best_idx = existing_block;
    }
    
    // 2. Scanner les menaces FUTURES
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        int score = evaluate_move_with_captures_full(g, i, player);
        
        if (score > *best_score) {
            *best_score = score;
            *best_idx = i;
        }
    }
}

int count_serious_threats(game *g, int player) {
    int threat_count = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    bool counted[MAX_BOARD] = {false};
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player) continue;
            
            int stones = 1;
            int open_ends = 0;
            
            for (int k = 1; k < 6; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones++;
                else if (cell == EMPTY) {
                    open_ends++;
                    break;
                }
                else break;
            }
            
            int bx = x - dx[d];
            int by = y - dy[d];
            if (IS_VALID(bx, by) && g->board[GET_INDEX(bx, by)] == EMPTY) {
                open_ends++;
            }
            
            if (stones >= 3 && open_ends >= 1 && !counted[idx]) {
                threat_count++;
                counted[idx] = true;
            }
        }
    }
    return threat_count;
}

int count_gapped_threes(game *g, int player) {
    int count = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int opponent = (player == P1) ? P2 : P1;
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int line[7];
            for (int k = -3; k <= 3; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (IS_VALID(nx, ny)) {
                    line[k + 3] = g->board[GET_INDEX(nx, ny)];
                } else {
                    line[k + 3] = opponent;
                }
            }
            
            // Pattern: . X _ X X .
            for (int start = 0; start <= 1; start++) {
                if (start + 5 >= 7) continue;
                if (line[start] == EMPTY &&
                    line[start + 1] == player &&
                    line[start + 2] == EMPTY &&
                    line[start + 3] == player &&
                    line[start + 4] == player &&
                    line[start + 5] == EMPTY) {
                    count++;
                }
            }
            
            // Pattern: . X X _ X .
            for (int start = 0; start <= 1; start++) {
                if (start + 5 >= 7) continue;
                if (line[start] == EMPTY &&
                    line[start + 1] == player &&
                    line[start + 2] == player &&
                    line[start + 3] == EMPTY &&
                    line[start + 4] == player &&
                    line[start + 5] == EMPTY) {
                    count++;
                }
            }
        }
    }
    return count;
}

int evaluate_move_with_captures_full(game *g, int idx, int player) {
    MoveUndo undo;
    apply_move(g, idx, player, &undo);
    
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
    
    if (undo.captured_count > 0) {
        for (int i = 0; i < MAX_BOARD; i++) {
            if (g->board[i] != player) continue;
            int new_score = get_point_score(g, GET_X(i), GET_Y(i), player);
            if (new_score > score) score = new_score;
        }
    }
    
    undo_move(g, player, &undo);
    return score;
}