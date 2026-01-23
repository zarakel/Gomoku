#include "../include/gomoku.h"

/*
 * ai_decision.c - Module de décision simplifié
 * 
 * ARCHITECTURE MINIMAX-FIRST :
 * Seules les urgences absolues (victoire/défaite immédiate) court-circuitent le Minimax.
 * Tout le reste est délégué au Minimax qui a une vue à plusieurs coups d'avance.
 */

/* ============================================================================
 * DÉTECTION VICTOIRE IMMÉDIATE (ALIGNEMENT)
 * ============================================================================ */

static int find_winning_alignment(game *g, int player) {
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        if (is_double_three(g, i, player)) continue;
        
        g->board[i] = player;
        int score = get_point_score(g, GET_X(i), GET_Y(i), player);
        g->board[i] = EMPTY;
        
        if (score >= WIN_SCORE) return i;
    }
    return -1;
}

/* ============================================================================
 * DÉTECTION VICTOIRE IMMÉDIATE (CAPTURE)
 * ============================================================================ */

static int find_winning_capture(game *g, int player) {
    if (g->captures[player] < 4) return -1;  /* Impossible de gagner par capture */
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = player;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), player) / 2;
        g->board[i] = EMPTY;
        
        if (g->captures[player] + caps >= 5) return i;
    }
    return -1;
}

/* ============================================================================
 * DÉTECTION DÉFENSE OBLIGATOIRE
 * 
 * Trouve TOUS les coups qui donnent la victoire à l'adversaire.
 * - Si 1 seul → le bloquer
 * - Si 2+ → chercher contre-attaque gagnante, sinon bloquer (position perdante)
 * ============================================================================ */

static int find_mandatory_defense(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    int opp_win_moves[16];
    int opp_win_count = 0;
    
    /* 1. Victoires par alignement */
    for (int i = 0; i < MAX_BOARD && opp_win_count < 16; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = opponent;
        int score = get_point_score(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        if (score >= WIN_SCORE) {
            opp_win_moves[opp_win_count++] = i;
        }
    }
    
    /* 2. Victoires par capture */
    if (g->captures[opponent] >= 4) {
        for (int i = 0; i < MAX_BOARD && opp_win_count < 16; i++) {
            if (g->board[i] != EMPTY) continue;
            
            /* Vérifier si déjà dans la liste */
            bool already = false;
            for (int k = 0; k < opp_win_count; k++) {
                if (opp_win_moves[k] == i) { already = true; break; }
            }
            if (already) continue;
            
            g->board[i] = opponent;
            int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent) / 2;
            g->board[i] = EMPTY;
            
            if (g->captures[opponent] + caps >= 5) {
                opp_win_moves[opp_win_count++] = i;
            }
        }
    }
    
    /* Aucune menace immédiate */
    if (opp_win_count == 0) return -1;
    
    /* Une seule menace → la bloquer */
    if (opp_win_count == 1) {
        #ifdef DEBUG
        printf("DEFENSE: Blocage unique en (%d,%d)\n", 
               GET_X(opp_win_moves[0]), GET_Y(opp_win_moves[0]));
        #endif
        return opp_win_moves[0];
    }
    
    /* Plusieurs menaces (double threat adverse) → chercher contre-attaque */
    #ifdef DEBUG
    printf("ALERTE: %d cases gagnantes adverses ! Recherche contre-attaque...\n", opp_win_count);
    #endif
    
    /* Peut-on gagner par alignement ? */
    int my_win = find_winning_alignment(g, ia_player);
    if (my_win != -1) return my_win;
    
    /* Peut-on gagner par capture ? */
    int my_cap_win = find_winning_capture(g, ia_player);
    if (my_cap_win != -1) return my_cap_win;
    
    /* Position perdante → bloquer une des menaces */
    #ifdef DEBUG
    printf("POSITION PERDANTE: blocage en (%d,%d)\n", 
           GET_X(opp_win_moves[0]), GET_Y(opp_win_moves[0]));
    #endif
    return opp_win_moves[0];
}

/* ============================================================================
 * FONCTION PRINCIPALE : DÉCISION IA
 * 
 * Architecture simple :
 * 1. Je peux gagner → je gagne
 * 2. Il peut gagner → je bloque (ou contre-attaque)
 * 3. Sinon → Minimax décide
 * ============================================================================ */

int make_tactical_decision(game *g, int ia_player, clock_t start_time) {
    (void)start_time;  /* Non utilisé ici, le temps est géré par le Minimax */
    
    /* ═══════════════════════════════════════════════════════════════════
     * RÈGLE 1 : VICTOIRE IMMÉDIATE
     * ═══════════════════════════════════════════════════════════════════ */
    
    int win_align = find_winning_alignment(g, ia_player);
    if (win_align != -1) {
        #ifdef DEBUG
        printf("VICTOIRE: Alignement gagnant en (%d,%d)\n", 
               GET_X(win_align), GET_Y(win_align));
        #endif
        return win_align;
    }
    
    int win_capture = find_winning_capture(g, ia_player);
    if (win_capture != -1) {
        #ifdef DEBUG
        printf("VICTOIRE: Capture gagnante en (%d,%d)\n", 
               GET_X(win_capture), GET_Y(win_capture));
        #endif
        return win_capture;
    }
    
    /* ═══════════════════════════════════════════════════════════════════
     * RÈGLE 2 : DÉFENSE OBLIGATOIRE
     * ═══════════════════════════════════════════════════════════════════ */
    
    int defense = find_mandatory_defense(g, ia_player);
    if (defense != -1) {
        return defense;
    }
    
    /* ═══════════════════════════════════════════════════════════════════
     * RÈGLE 3 : MINIMAX DÉCIDE
     * ═══════════════════════════════════════════════════════════════════ */
    
    #ifdef DEBUG
    printf("DECISION: Pas d'urgence → Minimax\n");
    #endif
    
    return -1;  /* Signal pour passer au Minimax */
}