#include "../include/gomoku.h"

/*
 * ai_threat_response.c - Réponses aux menaces et scan adverse
 */

/* ============================================================================
 * SCAN DES CAPTURES ADVERSES DANGEREUSES
 * ============================================================================ */

int scan_dangerous_opponent_captures(game *g, int player, UnifiedThreat *threats, int start_idx, int max_threats) {
    int count = start_idx;
    int opponent = (player == P1) ? P2 : P1;
    
    for (int i = 0; i < MAX_BOARD && count < max_threats; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = opponent;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent) / 2;
        g->board[i] = EMPTY;
        
        if (caps < 1) continue;
        
        MoveUndo undo;
        apply_move(g, i, opponent, &undo);
        
        int best_threat_after = 0;
        
        for (int j = 0; j < MAX_BOARD; j++) {
            if (g->board[j] != EMPTY) continue;
            int score = evaluate_move_with_captures_full(g, j, opponent);
            if (score > best_threat_after) {
                best_threat_after = score;
            }
        }
        
        ExistingThreat existing[10];
        int existing_count = scan_all_existing_threats(g, opponent, existing, 10);
        for (int t = 0; t < existing_count; t++) {
            if (existing[t].score > best_threat_after) {
                best_threat_after = existing[t].score;
            }
        }
        
        undo_move(g, opponent, &undo);
        
        if (best_threat_after >= CLOSED_FOUR && count < max_threats) {
            int moves = MOVES_NEXT;
            if (best_threat_after >= WIN_SCORE) moves = MOVES_IMMEDIATE;
            else if (best_threat_after >= OPEN_FOUR) moves = MOVES_IMMEDIATE;
            
            threats[count].index = i;
            threats[count].score = best_threat_after;
            threats[count].stones = 0;
            threats[count].captures = caps;
            threats[count].direction = -1;
            threats[count].type = THREAT_CAPTURE_ALIGN;
            threats[count].moves_to_win = moves;
            threats[count].is_blocking = true;
            count++;
            
            #ifdef DEBUG
            printf("DANGER: Capture adverse en (%d,%d) créerait menace score=%d\n",
                   GET_X(i), GET_Y(i), best_threat_after);
            #endif
        }
    }
    
    return count;
}

/* ============================================================================
 * COMPARATEUR POUR TRI
 * ============================================================================ */

int compare_unified_threats(const void *a, const void *b) {
    const UnifiedThreat *ta = (const UnifiedThreat *)a;
    const UnifiedThreat *tb = (const UnifiedThreat *)b;
    
    if (ta->moves_to_win != tb->moves_to_win) {
        return ta->moves_to_win - tb->moves_to_win;
    }
    
    if (ta->score != tb->score) {
        return tb->score - ta->score;
    }
    
    int ta_power = ta->stones + ta->captures * 2;
    int tb_power = tb->stones + tb->captures * 2;
    
    return tb_power - ta_power;
}