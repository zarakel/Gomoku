#include "../include/gomoku.h"

// Déclarations externes
extern int find_gapped_four_hole(game *g, int player);
extern int find_gapped_three_hole(game *g, int player);

// ============================================================================
// RECHERCHE DE COUPS GAGNANTS / BLOQUANTS
// ============================================================================

int find_winning_move(game *g, int player) {
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        int score = evaluate_move_with_captures_full(g, i, player);
        if (score >= WIN_SCORE) return i;
    }
    return -1;
}

int find_line_blocking_moves(game *g, int player, int *blocking_moves, int max_moves) {
    int count = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    for (int idx = 0; idx < MAX_BOARD && count < max_moves; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int stones = 1;
            int empty_before = -1;
            int empty_after = -1;
            
            for (int k = 1; k < 5; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones++;
                else if (cell == EMPTY) {
                    empty_after = GET_INDEX(nx, ny);
                    break;
                }
                else break;
            }
            
            for (int k = 1; k < 5; k++) {
                int nx = x - dx[d] * k;
                int ny = y - dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones++;
                else if (cell == EMPTY) {
                    empty_before = GET_INDEX(nx, ny);
                    break;
                }
                else break;
            }
            
            if (stones >= 3) {
                if (empty_before != -1 && count < max_moves) {
                    bool found = false;
                    for (int i = 0; i < count; i++) {
                        if (blocking_moves[i] == empty_before) { found = true; break; }
                    }
                    if (!found) blocking_moves[count++] = empty_before;
                }
                if (empty_after != -1 && count < max_moves) {
                    bool found = false;
                    for (int i = 0; i < count; i++) {
                        if (blocking_moves[i] == empty_after) { found = true; break; }
                    }
                    if (!found) blocking_moves[count++] = empty_after;
                }
            }
        }
    }
    return count;
}

int find_blocking_move(game *g, int threat_player) {
    int ia_player = (threat_player == P1) ? P2 : P1;
    
    // ÉTAPE 0 : Gapped Four
    int gapped_hole = find_gapped_four_hole(g, threat_player);
    if (gapped_hole != -1 && g->board[gapped_hole] == EMPTY) {
        return gapped_hole;
    }
    
    // ÉTAPE 1 : Cases WIN directes
    int win_moves[10];
    int win_count = 0;
    
    for (int i = 0; i < MAX_BOARD && win_count < 10; i++) {
        if (g->board[i] != EMPTY) continue;
        int score = evaluate_move_with_captures_full(g, i, threat_player);
        if (score >= WIN_SCORE) win_moves[win_count++] = i;
    }
    
    if (win_count == 1) return win_moves[0];
    if (win_count > 1) return win_moves[0];
    
    // ÉTAPE 1.5 : Gapped Three
    int gapped_three = find_gapped_three_hole(g, threat_player);
    if (gapped_three != -1 && g->board[gapped_three] == EMPTY) {
        return gapped_three;
    }
    
    // ÉTAPE 2 : Extrémités des alignements
    int blocking_candidates[20];
    int block_count = find_line_blocking_moves(g, threat_player, blocking_candidates, 20);
    
    if (block_count > 0) {
        int best_block = blocking_candidates[0];
        int best_score = -1000000000;
        
        for (int i = 0; i < block_count; i++) {
            int idx = blocking_candidates[i];
            if (g->board[idx] != EMPTY) continue;
            
            int threat_before = 0;
            for (int j = 0; j < MAX_BOARD; j++) {
                if (g->board[j] != EMPTY) continue;
                int s = evaluate_move_with_captures_full(g, j, threat_player);
                if (s > threat_before) threat_before = s;
            }
            
            g->board[idx] = ia_player;
            
            int threat_after = 0;
            for (int j = 0; j < MAX_BOARD; j++) {
                if (g->board[j] != EMPTY) continue;
                int s = evaluate_move_with_captures_full(g, j, threat_player);
                if (s > threat_after) threat_after = s;
            }
            
            int our_attack = get_point_score(g, GET_X(idx), GET_Y(idx), ia_player);
            
            g->board[idx] = EMPTY;
            
            int reduction = threat_before - threat_after;
            int combined_score = reduction + (our_attack / 2);
            
            if (our_attack >= OPEN_THREE) combined_score += 2000000;
            if (our_attack >= CLOSED_FOUR) combined_score += 10000000;
            
            if (combined_score > best_score) {
                best_score = combined_score;
                best_block = idx;
            }
        }
        
        return best_block;
    }
    
    return -1;
}

int find_best_dual_purpose_move(game *g, int ia_player, int opponent) {
    int best_idx = -1;
    int best_combined_score = 0;
    
    int gapped_three = find_gapped_three_hole(g, opponent);
    if (gapped_three != -1 && g->board[gapped_three] == EMPTY) {
        g->board[gapped_three] = ia_player;
        int attack_score = get_point_score(g, GET_X(gapped_three), GET_Y(gapped_three), ia_player);
        g->board[gapped_three] = EMPTY;
        
        if (attack_score >= CLOSED_THREE) {
            return gapped_three;
        }
    }
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = ia_player;
        int attack_score = get_point_score(g, GET_X(i), GET_Y(i), ia_player);
        g->board[i] = EMPTY;
        
        g->board[i] = opponent;
        int defense_score = get_point_score(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        int blocks_gapped = 0;
        if (i == gapped_three) {
            blocks_gapped = 5000000;
        }
        
        int combined = 0;
        
        if (attack_score >= OPEN_THREE && defense_score >= OPEN_THREE) {
            combined = attack_score + defense_score + 50000000 + blocks_gapped;
        }
        else if (attack_score >= CLOSED_THREE && defense_score >= CLOSED_THREE) {
            combined = attack_score + defense_score + 10000000 + blocks_gapped;
        }
        else {
            combined = attack_score + (defense_score / 2) + blocks_gapped;
        }
        
        if (combined > best_combined_score) {
            best_combined_score = combined;
            best_idx = i;
        }
    }
    
    return best_idx;
}