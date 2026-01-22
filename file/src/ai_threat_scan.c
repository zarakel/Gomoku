#include "../include/gomoku.h"

/*
 * ai_threat_scan.c - Scan des menaces d'alignement et de capture
 */

/* ============================================================================
 * HELPER : Calcul du nombre de coups avant victoire
 * ============================================================================ */

int compute_moves_to_win_alignment(int score, int stones, int open_ends) {
    if (score >= WIN_SCORE || stones >= 5) return MOVES_IMMEDIATE;
    
    if (stones == 4) {
        if (open_ends >= 2) return MOVES_IMMEDIATE;
        if (open_ends == 1) return MOVES_NEXT;
    }
    
    if (stones == 3) {
        if (open_ends == 2) return MOVES_NEXT;
        if (open_ends == 1) return MOVES_TWO_AWAY;
    }
    
    return MOVES_DEVELOPING;
}

int compute_moves_to_win_capture(int current_captures, int potential_captures) {
    int total = current_captures + potential_captures;
    
    if (total >= 5) return MOVES_IMMEDIATE;
    if (total >= 4) return MOVES_NEXT;
    if (total >= 3) return MOVES_TWO_AWAY;
    
    return MOVES_DEVELOPING;
}

/* ============================================================================
 * SCAN DES MENACES D'ALIGNEMENT
 * ============================================================================ */

int scan_alignment_threats(game *g, int player, UnifiedThreat *threats, int start_idx, int max_threats) {
    int count = start_idx;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    char* dir_names[] = {"H", "V", "D\\", "D/"};
    int opponent = (player == P1) ? P2 : P1;
    
    for (int idx = 0; idx < MAX_BOARD && count < max_threats; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player) 
                continue;
            
            int stones = 1;
            int empty_before = -1;
            int empty_after = -1;
            int blocked_before = 0;
            int blocked_after = 0;
            
            if (IS_VALID(px, py)) {
                int cell = g->board[GET_INDEX(px, py)];
                if (cell == EMPTY) {
                    empty_before = GET_INDEX(px, py);
                } else if (cell == opponent) {
                    blocked_before = 1;
                }
            } else {
                blocked_before = 1;
            }
            
            int last_x = x, last_y = y;
            for (int k = 1; k < 6; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) {
                    blocked_after = 1;
                    break;
                }
                
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) {
                    stones++;
                    last_x = nx;
                    last_y = ny;
                }
                else if (cell == EMPTY) {
                    empty_after = GET_INDEX(nx, ny);
                    break;
                }
                else {
                    blocked_after = 1;
                    break;
                }
            }
            
            (void)last_x; (void)last_y; (void)blocked_before; (void)blocked_after;
            
            int open_ends = (empty_before != -1 ? 1 : 0) + (empty_after != -1 ? 1 : 0);
            
            if (stones < 3) continue;
            
            int score = 0;
            int moves_to_win = MOVES_DEVELOPING;
            
            if (stones >= 5) {
                score = WIN_SCORE;
                moves_to_win = MOVES_IMMEDIATE;
            }
            else if (stones == 4) {
                if (open_ends >= 2) {
                    score = OPEN_FOUR;
                    moves_to_win = MOVES_IMMEDIATE;
                } else if (open_ends == 1) {
                    score = CLOSED_FOUR;
                    moves_to_win = MOVES_NEXT;
                } else {
                    score = CLOSED_FOUR / 2;
                    moves_to_win = MOVES_TWO_AWAY;
                }
            }
            else if (stones == 3) {
                if (open_ends >= 2) {
                    score = OPEN_THREE;
                    moves_to_win = MOVES_NEXT;
                } else if (open_ends == 1) {
                    score = CLOSED_THREE;
                    moves_to_win = MOVES_TWO_AWAY;
                } else {
                    score = CLOSED_THREE / 2;
                    moves_to_win = MOVES_DEVELOPING;
                }
            }
            
            int best_block = -1;
            if (empty_after != -1) {
                best_block = empty_after;
            } else if (empty_before != -1) {
                best_block = empty_before;
            }
            
            if (stones >= 5 && best_block == -1) {
                best_block = idx;
            }
            
            if (best_block == -1 && stones >= 3) {
                for (int k = -1; k <= stones; k++) {
                    int nx = x + dx[d] * k;
                    int ny = y + dy[d] * k;
                    if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] == EMPTY) {
                        best_block = GET_INDEX(nx, ny);
                        break;
                    }
                }
            }
            
            if (score >= CLOSED_THREE / 2 && count < max_threats) {
                int blocks_to_add[2] = {-1, -1};
                int num_blocks = 0;
                
                if (empty_after != -1) {
                    blocks_to_add[num_blocks++] = empty_after;
                }
                if (empty_before != -1 && open_ends == 2) {
                    blocks_to_add[num_blocks++] = empty_before;
                }
                
                if (num_blocks == 0 && best_block != -1) {
                    blocks_to_add[num_blocks++] = best_block;
                }
                
                for (int b = 0; b < num_blocks && count < max_threats; b++) {
                    int block_idx = blocks_to_add[b];
                    if (block_idx == -1) continue;
                    
                    bool exists = false;
                    for (int t = start_idx; t < count; t++) {
                        if (threats[t].index == block_idx && threats[t].direction == d) {
                            exists = true;
                            if (score > threats[t].score) {
                                threats[t].score = score;
                                threats[t].stones = stones;
                                threats[t].moves_to_win = moves_to_win;
                            }
                            break;
                        }
                    }
                    
                    if (!exists) {
                        threats[count].index = block_idx;
                        threats[count].score = score;
                        threats[count].stones = stones;
                        threats[count].captures = 0;
                        threats[count].direction = d;
                        threats[count].type = THREAT_ALIGNMENT;
                        threats[count].moves_to_win = moves_to_win;
                        threats[count].is_blocking = false;
                        count++;
                        
                        #ifdef DEBUG
                        printf("  THREAT: %d stones %s, block=(%d,%d), score=%d, moves=%d, open=%d\n",
                               stones, dir_names[d], 
                               GET_X(block_idx), GET_Y(block_idx),
                               score, moves_to_win, open_ends);
                        #endif
                    }
                }
            }
        }
    }
    
    return count;
}

/* ============================================================================
 * SCAN DES MENACES DE CAPTURE
 * ============================================================================ */

int scan_capture_threats(game *g, int player, UnifiedThreat *threats, int start_idx, int max_threats) {
    int count = start_idx;
    int current_captures = g->captures[player];
    
    for (int i = 0; i < MAX_BOARD && count < max_threats; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = player;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), player) / 2;
        g->board[i] = EMPTY;
        
        if (caps < 1) continue;
        
        int total_after = current_captures + caps;
        int moves_to_win = compute_moves_to_win_capture(current_captures, caps);
        
        int score = 0;
        if (total_after >= 5) score = WIN_SCORE;
        else if (total_after >= 4) score = OPEN_FOUR;
        else if (total_after >= 3) score = CLOSED_FOUR;
        else if (total_after >= 2) score = OPEN_THREE;
        else score = CLOSED_THREE;
        
        ThreatType type = THREAT_CAPTURE;
        MoveUndo undo;
        apply_move(g, i, player, &undo);
        
        int align_score = get_point_score(g, GET_X(i), GET_Y(i), player);
        if (align_score >= CLOSED_THREE) {
            type = THREAT_CAPTURE_ALIGN;
            if (align_score > score) score = align_score;
            if (moves_to_win > MOVES_NEXT) moves_to_win = MOVES_NEXT;
        }
        
        undo_move(g, player, &undo);
        
        if (count < max_threats) {
            threats[count].index = i;
            threats[count].score = score;
            threats[count].stones = 0;
            threats[count].captures = caps;
            threats[count].direction = -1;
            threats[count].type = type;
            threats[count].moves_to_win = moves_to_win;
            threats[count].is_blocking = false;
            count++;
        }
    }
    
    return count;
}