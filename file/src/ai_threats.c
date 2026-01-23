#include "../include/gomoku.h"

/*
 * ai_threats.c - Détection et analyse des menaces
 * 
 * Responsabilité unique : Détecter les menaces sur le plateau
 * - Menaces existantes (alignements déjà formés)
 * - Menaces potentielles (si un joueur joue à une case)
 * - Patterns gappés (X_XXX, .X_XX.)
 */


/* Dans ai_threats.c ou heuristics.c */

static int count_empty_neighbors(game *g, int idx) {
    if (idx == -1) return 0;
    
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int count = 0;
    
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] == EMPTY) {
                count++;
            }
        }
    }
    
    return count;
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
            /* Ne scanner que le début de ligne */
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player) 
                continue;
            
            int stones = 1;
            int open_ends = 0;
            
            /* Scan positif */
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
            
            /* Scan négatif */
            int bx = x - dx[d];
            int by = y - dy[d];
            if (IS_VALID(bx, by) && g->board[GET_INDEX(bx, by)] == EMPTY) {
                open_ends++;
            }
            
            /* Menace sérieuse : 3+ pierres avec espace */
            if (stones >= 3 && open_ends >= 1 && !counted[idx]) {
                threat_count++;
                counted[idx] = true;
            }
        }
    }
    return threat_count;
}

/* ============================================================================
 * ÉVALUATION D'UN COUP AVEC CAPTURES
 * Simule le coup, applique les captures, retourne le meilleur score
 * ============================================================================ */

int evaluate_move_with_captures_full(game *g, int idx, int player) {
    MoveUndo undo;
    apply_move(g, idx, player, &undo);
    
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
    
    /* NOUVEAU : Si capture, scanner TOUTES les lignes affectées */
    if (undo.captured_count > 0) {
        
        /* 1. Scanner les lignes passant par les pierres capturées */
        for (int i = 0; i < undo.captured_count; i++) {
            int cap_idx = undo.captured_indices[i];
            int cx = GET_X(cap_idx);
            int cy = GET_Y(cap_idx);
            
            /* Scanner dans les 4 directions depuis la case vidée */
            int dx[] = {1, 0, 1, 1};
            int dy[] = {0, 1, 1, -1};
            
            for (int d = 0; d < 4; d++) {
                /* Chercher nos pierres de chaque côté de la case vidée */
                int stones_pos = 0, stones_neg = 0;
                int first_stone_pos = -1, first_stone_neg = -1;
                
                /* Scan positif */
                for (int k = 1; k <= 5; k++) {
                    int nx = cx + dx[d] * k;
                    int ny = cy + dy[d] * k;
                    if (!IS_VALID(nx, ny)) break;
                    int cell = g->board[GET_INDEX(nx, ny)];
                    if (cell == player) {
                        stones_pos++;
                        if (first_stone_pos == -1) first_stone_pos = GET_INDEX(nx, ny);
                    }
                    else if (cell != EMPTY) break;
                }
                
                /* Scan négatif */
                for (int k = 1; k <= 5; k++) {
                    int nx = cx - dx[d] * k;
                    int ny = cy - dy[d] * k;
                    if (!IS_VALID(nx, ny)) break;
                    int cell = g->board[GET_INDEX(nx, ny)];
                    if (cell == player) {
                        stones_neg++;
                        if (first_stone_neg == -1) first_stone_neg = GET_INDEX(nx, ny);
                    }
                    else if (cell != EMPTY) break;
                }
                
                /* La capture a-t-elle CONNECTÉ deux segments ? */
                if (stones_pos >= 1 && stones_neg >= 1) {
                    int total_connected = stones_pos + stones_neg;
                    
                    /* Évaluer la nouvelle ligne connectée */
                    if (first_stone_pos != -1) {
                        int new_score = get_point_score(g, GET_X(first_stone_pos), 
                                                        GET_Y(first_stone_pos), player);
                        if (new_score > score) score = new_score;
                    }
                    
                    /* Bonus pour connexion de segments */
                    if (total_connected >= 3) score += OPEN_THREE / 2;
                    if (total_connected >= 4) score += CLOSED_FOUR;
                }
            }
        }
        
        /* 2. Re-scanner le coup lui-même (les captures peuvent avoir ouvert des lignes) */
        int post_score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
        if (post_score > score) score = post_score;
    }
    
    undo_move(g, player, &undo);
    return score;
}
