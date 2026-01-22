#include "../include/gomoku.h"

/*
 * ai_captures.c - Logique spécifique aux captures
 * 
 * Responsabilité unique : Gérer la stratégie de captures
 * - Évaluer le danger des captures (pour les deux joueurs)
 * - Trouver les meilleurs coups de capture
 * - Trouver les blocages de capture urgents
 */

/* ============================================================================
 * ÉVALUATION UNIFIÉE DES MENACES DE CAPTURE
 * Convertit le nombre de captures en score équivalent aux alignements
 * ============================================================================ */

int compute_unified_threat_level(game *g, int player) {
    int captures = g->captures[player];
    int capture_move = find_capture_move(g, player);
    int can_capture = 0;
    
    /* Estimer le nombre de captures possibles au prochain coup */
    if (capture_move != -1) {
        g->board[capture_move] = player;
        can_capture = count_potential_captures(g, GET_X(capture_move), 
                                               GET_Y(capture_move), player) / 2;
        g->board[capture_move] = EMPTY;
    }
    
    int future_captures = captures + can_capture;
    
    /* Conversion en échelle de menace */
    if (future_captures >= 5) return WIN_SCORE;
    if (future_captures >= 4) return OPEN_FOUR;
    if (captures >= 3 && can_capture >= 1) return CLOSED_FOUR;
    if (captures >= 2 && can_capture >= 2) return OPEN_THREE;
    if (captures >= 2 && can_capture >= 1) return CLOSED_THREE;
    if (captures >= 1 && can_capture >= 1) return OPEN_TWO;
    
    return 0;
}

/* ============================================================================
 * RECHERCHE DU MEILLEUR COUP DE CAPTURE
 * Ne retourne pas juste le premier, mais le meilleur stratégiquement
 * ============================================================================ */

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
        
        undo_move(g, player, &undo);
        
        if (score > best_score) {
            best_score = score;
            best_move = i;
        }
    }
    
    return best_move;
}

/* ============================================================================
 * BLOCAGE DE CAPTURE URGENT
 * Trouve la case où l'adversaire ferait une capture dangereuse
 * ============================================================================ */

int find_critical_capture_block(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int opp_captures = g->captures[opponent];
    
    int critical_moves[20];
    int critical_scores[20];
    int critical_count = 0;
    
    for (int i = 0; i < MAX_BOARD && critical_count < 20; i++) {
        if (g->board[i] != EMPTY) continue;
        
        /* Simuler le coup adverse */
        g->board[i] = opponent;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent) / 2;
        g->board[i] = EMPTY;
        
        if (caps >= 1) {
            int future_total = opp_captures + caps;
            int danger_score = 0;
            
            /* Évaluer le danger */
            if (future_total >= 5) danger_score = WIN_SCORE;
            else if (future_total >= 4) danger_score = OPEN_FOUR;
            else if (opp_captures >= 3) danger_score = CLOSED_FOUR;
            else if (opp_captures >= 2) danger_score = OPEN_THREE;
            else danger_score = CLOSED_THREE;
            
            critical_moves[critical_count] = i;
            critical_scores[critical_count] = danger_score;
            critical_count++;
        }
    }
    
    /* Retourner le blocage le plus urgent */
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