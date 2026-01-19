#include "../include/gomoku.h"

// ============================================================================
// ÉVALUATION UNIFIÉE DES CAPTURES
// ============================================================================

int compute_unified_threat_level(game *g, int player) {
    int captures = g->captures[player];
    int capture_move = find_capture_move(g, player);
    int can_capture = 0;
    
    if (capture_move != -1) {
        g->board[capture_move] = player;
        can_capture = count_potential_captures(g, GET_X(capture_move), GET_Y(capture_move), player) / 2;
        g->board[capture_move] = EMPTY;
    }
    
    int future_captures = captures + can_capture;
    
    int capture_threat = 0;
    if (future_captures >= 5) {
        capture_threat = WIN_SCORE;
    } else if (future_captures >= 4) {
        capture_threat = OPEN_FOUR;
    } else if (captures >= 3 && can_capture >= 1) {
        capture_threat = CLOSED_FOUR;
    } else if (captures >= 2 && can_capture >= 2) {
        capture_threat = OPEN_THREE;
    } else if (captures >= 2 && can_capture >= 1) {
        capture_threat = CLOSED_THREE;
    } else if (captures >= 1 && can_capture >= 1) {
        capture_threat = OPEN_TWO;
    }
    
    return capture_threat;
}

int get_capture_danger_score(game *g, int player) {
    int captures = g->captures[player];
    int capture_move = find_capture_move(g, player);
    int can_capture = 0;
    
    if (capture_move != -1) {
        g->board[capture_move] = player;
        can_capture = count_potential_captures(g, GET_X(capture_move), GET_Y(capture_move), player) / 2;
        g->board[capture_move] = EMPTY;
    }
    
    int future_total = captures + can_capture;
    
    if (future_total >= 5) return 10000000;
    if (future_total >= 4) return 5000000;
    if (future_total >= 3) return 2000000;
    if (future_total >= 2) return 500000;
    if (future_total >= 1) return 100000;
    return 0;
}

bool is_alignment_vulnerable(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int vulnerable = count_vulnerable_pairs(g, player);
    
    if (vulnerable > 0) {
        int opp_capture = find_capture_move(g, opponent);
        if (opp_capture != -1) {
            return true;
        }
    }
    return false;
}

void analyze_pair_safety(game *g, int player, int *protected_pairs, int *exposed_pairs) {
    int opponent = (player == P1) ? P2 : P1;
    *protected_pairs = 0;
    *exposed_pairs = 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d];
            int ny = y + dy[d];
            
            if (!IS_VALID(nx, ny)) continue;
            if (g->board[GET_INDEX(nx, ny)] != player) continue;
            
            int before_x = x - dx[d];
            int before_y = y - dy[d];
            int after_x = nx + dx[d];
            int after_y = ny + dy[d];
            
            bool exposed = false;
            
            if (IS_VALID(before_x, before_y) && IS_VALID(after_x, after_y)) {
                int before = g->board[GET_INDEX(before_x, before_y)];
                int after = g->board[GET_INDEX(after_x, after_y)];
                
                if (before == opponent && after == EMPTY) exposed = true;
                if (before == EMPTY && after == opponent) exposed = true;
            }
            
            if (exposed) {
                (*exposed_pairs)++;
            } else {
                (*protected_pairs)++;
            }
        }
    }
    
    *protected_pairs /= 2;
    *exposed_pairs /= 2;
}

// ============================================================================
// ACTIONS DE CAPTURE
// ============================================================================

int find_best_capture_move(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int best_move = -1;
    int best_score = -1;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        MoveUndo undo;
        apply_move(g, i, player, &undo);
        
        if (undo.captured_count == 0) {
            undo_move(g, player, &undo);
            continue;
        }
        
        int total_captures = g->captures[player];
        int score = total_captures * 1000000;
        
        if (total_captures >= 5) {
            undo_move(g, player, &undo);
            return i;
        }
        
        int alignment_score = get_point_score(g, GET_X(i), GET_Y(i), player);
        score += alignment_score;
        
        for (int j = 0; j < undo.captured_count; j++) {
            int cap_idx = undo.captured_indices[j];
            
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = GET_X(cap_idx) + dx;
                    int ny = GET_Y(cap_idx) + dy;
                    if (!IS_VALID(nx, ny)) continue;
                    
                    if (g->board[GET_INDEX(nx, ny)] == player) {
                        int new_line_score = get_point_score(g, nx, ny, player);
                        score += new_line_score / 2;
                    }
                }
            }
        }
        
        int next_capture = find_capture_move(g, player);
        if (next_capture != -1) {
            g->board[next_capture] = player;
            int next_caps = count_potential_captures(g, GET_X(next_capture), GET_Y(next_capture), player) / 2;
            g->board[next_capture] = EMPTY;
            
            if (total_captures + next_caps >= 5) {
                score += OPEN_FOUR;
            } else {
                score += next_caps * 500000;
            }
        }
        
        undo_move(g, player, &undo);
        
        if (score > best_score) {
            best_score = score;
            best_move = i;
        }
    }
    
    return best_move;
}

int find_critical_capture_block(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int opp_captures = g->captures[opponent];
    
    int critical_moves[20];
    int critical_scores[20];
    int critical_count = 0;
    
    for (int i = 0; i < MAX_BOARD && critical_count < 20; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = opponent;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent) / 2;
        g->board[i] = EMPTY;
        
        if (caps >= 1) {
            int future_total = opp_captures + caps;
            int danger_score = 0;
            
            if (future_total >= 5) {
                danger_score = WIN_SCORE;
            } else if (future_total >= 4) {
                danger_score = OPEN_FOUR;
            } else if (opp_captures >= 3) {
                danger_score = CLOSED_FOUR;
            } else if (opp_captures >= 2) {
                danger_score = OPEN_THREE;
            } else {
                danger_score = CLOSED_THREE;
            }
            
            critical_moves[critical_count] = i;
            critical_scores[critical_count] = danger_score;
            critical_count++;
        }
    }
    
    int most_critical = -1;
    int highest_danger = 0;
    
    for (int i = 0; i < critical_count; i++) {
        if (critical_scores[i] > highest_danger) {
            highest_danger = critical_scores[i];
            most_critical = critical_moves[i];
        }
    }
    
    return most_critical;
}