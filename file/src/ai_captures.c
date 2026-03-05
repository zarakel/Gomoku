#include "../include/gomoku.h"

/**
 * Module de gestion strategique des captures.
 * 
 * Fonctions principales :
 * - Evaluation de la valeur defensive des captures
 * - Recherche du meilleur coup de capture offensif
 * - Calcul du niveau de danger des captures adverses
 */

/**
 * Evalue la valeur defensive d'une capture.
 * 
 * Calcule un bonus si la capture retire une pierre adverse qui participe
 * a plusieurs formations dangereuses (jonctions).
 * 
 * Retourne un score de bonus (0 si capture sans valeur defensive speciale).
 */
int evaluate_defensive_capture_value(game *g, int capture_idx, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    /* Trouver quelles pierres seraient capturées */
    int dx[] = {1, 0, 1, 1, -1, 0, -1, -1};
    int dy[] = {0, 1, 1, -1, 0, -1, -1, 1};
    int x = GET_X(capture_idx);
    int y = GET_Y(capture_idx);
    
    int defensive_value = 0;
    
    for (int d = 0; d < 8; d++) {
        /* Pattern: EMPTY-OPP-OPP-IA ou IA-OPP-OPP-EMPTY */
        int x1 = x + dx[d];
        int y1 = y + dy[d];
        int x2 = x + dx[d] * 2;
        int y2 = y + dy[d] * 2;
        int x3 = x + dx[d] * 3;
        int y3 = y + dy[d] * 3;
        
        if (!IS_VALID(x1, y1) || !IS_VALID(x2, y2) || !IS_VALID(x3, y3))
            continue;
        
        if (g->board[GET_INDEX(x1, y1)] == opponent &&
            g->board[GET_INDEX(x2, y2)] == opponent &&
            g->board[GET_INDEX(x3, y3)] == ia_player) {
            
            /* Les pierres (x1,y1) et (x2,y2) seraient capturées */
            /* Calculer leur valeur défensive */
            int stone1_formations = compute_junction_potential(g, GET_INDEX(x1, y1), opponent);
            int stone2_formations = compute_junction_potential(g, GET_INDEX(x2, y2), opponent);
            
            if (stone1_formations >= 2 || stone2_formations >= 2) {
                defensive_value += 500000;  /* Valeur d'un OPEN_THREE */
                #ifdef DEBUG
                printf("CAPTURE DÉFENSIVE: (%d,%d) retire pierres critiques (f1=%d, f2=%d)\n",
                       GET_X(capture_idx), GET_Y(capture_idx),
                       stone1_formations, stone2_formations);
                #endif
            }
        }
    }
    
    return defensive_value;
}

/**
 * Recherche le meilleur coup de capture disponible.
 * 
 * Evalue tous les coups possibles qui capturent des paires adverses.
 * Criteres d'evaluation :
 * - Nombre de paires capturees
 * - Victoire immediate par 5eme capture
 * - Alignements crees simultanement
 * - Captures en chaine possibles
 * - Valeur defensive de la capture
 * 
 * Retourne l'index du meilleur coup, ou -1 si aucune capture possible.
 */
int find_best_capture_move(game *g, int player) {
    int best_move = -1;
    int best_score = -1;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        MoveUndo undo;
        apply_move(g, i, player, &undo);
        
        /* Pas de capture = ignorer */
        if (undo.captured_count == 0) {
            undo_move(g, player, &undo);
            continue;
        }
        
        int total_captures = g->captures[player];
        int score = total_captures * 1000000;
        
        /* Victoire immédiate par capture */
        if (total_captures >= 5) {
            undo_move(g, player, &undo);
            return i;
        }
        
        /* Bonus : alignement créé par le coup */
        int alignment_score = get_point_score(g, GET_X(i), GET_Y(i), player);
        score += alignment_score;
        
        /* Bonus : captures en chaîne possibles */
        int next_capture = find_capture_move(g, player);
        if (next_capture != -1) {
            g->board[next_capture] = player;
            int next_caps = count_potential_captures(g, GET_X(next_capture), 
                                                     GET_Y(next_capture), player) / 2;
            g->board[next_capture] = EMPTY;
            
            if (total_captures + next_caps >= 5) {
                score += OPEN_FOUR;
            } else {
                score += next_caps * 500000;
            }
        }
        
        /* Bonus : valeur défensive de la capture */
        int defensive_value = evaluate_defensive_capture_value(g, i, player);
        score += defensive_value;
        
        undo_move(g, player, &undo);
        
        if (score > best_score) {
            best_score = score;
            best_move = i;
        }
    }
    
    return best_move;
}

/* Dead code removed: compute_capture_danger */