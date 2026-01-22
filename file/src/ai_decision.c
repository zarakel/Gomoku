#include "../include/gomoku.h"

/*
 * ai_decision.c - Module de décision tactique UNIFIÉ
 * 
 * Utilise le système unifié de scan des menaces (ai_unified.c)
 * Fallback vers TSS et Minimax si nécessaire
 */

#define TSS_OFFENSIVE_BUDGET  50

/* ============================================================================
 * FONCTION PRINCIPALE : DÉCISION TACTIQUE
 * ============================================================================ */

int make_tactical_decision(game *g, int ia_player, clock_t start_time) {
    int opponent = (ia_player == P1) ? P2 : P1;
    int decision = -1;
    
    /* ══════════════════════════════════════════════════════════════════════
     * PHASE 1 : SCAN UNIFIÉ DES MENACES
     * ══════════════════════════════════════════════════════════════════════ */
    
    UnifiedThreat my_threats[30];
    int my_count = scan_unified_threats(g, ia_player, my_threats, 30);
    
    UnifiedThreat opp_threats[30];
    int opp_count = scan_unified_opponent_threats(g, ia_player, opp_threats, 30);
    
    #ifdef DEBUG
    printf("DEBUG UNIFIED: %d menaces IA, %d menaces adverses\n", my_count, opp_count);
    printf("DEBUG: Adversaire a %d paires\n", g->captures[opponent]);
    #endif
    
    /* ══════════════════════════════════════════════════════════════════════
     * PHASE 2 : DÉCISION BASÉE SUR LE SCAN UNIFIÉ
     * ══════════════════════════════════════════════════════════════════════ */
    
    decision = get_best_response(g, ia_player, NULL, 0);
    if (decision != -1) {
        #ifdef DEBUG
        printf("DECISION UNIFIED: Coup en (%d, %d)\n", GET_X(decision), GET_Y(decision));
        #endif
        return decision;
    }
    
    /* ══════════════════════════════════════════════════════════════════════
     * PHASE 3 : DÉTECTIONS SPÉCIALES (captures qui créent des menaces)
     * ══════════════════════════════════════════════════════════════════════ */
    
    /* Captures adverses qui connectent des segments */
    int capture_connects = detect_capture_connects_segments(g, ia_player);
    if (capture_connects != -1) {
        #ifdef DEBUG
        printf("DECISION: Blocage capture qui connecte segments en (%d, %d)\n", 
               GET_X(capture_connects), GET_Y(capture_connects));
        #endif
        return capture_connects;
    }
    
    /* Double CLOSED_FOUR créé par capture */
    int capture_double = detect_capture_creates_double_closed_four(g, ia_player);
    if (capture_double != -1) {
        #ifdef DEBUG
        printf("DECISION: Blocage capture double menace en (%d, %d)\n",
               GET_X(capture_double), GET_Y(capture_double));
        #endif
        return capture_double;
    }
    
    /* ══════════════════════════════════════════════════════════════════════
     * PHASE 4 : TSS OFFENSIF
     * ══════════════════════════════════════════════════════════════════════ */
    
    decision = tss_find_winning_sequence(g, ia_player, start_time, TSS_OFFENSIVE_BUDGET);
    if (decision != -1) {
        #ifdef DEBUG
        printf("DECISION: TSS trouve séquence gagnante en (%d, %d)\n", 
               GET_X(decision), GET_Y(decision));
        #endif
        return decision;
    }
    
    /* ══════════════════════════════════════════════════════════════════════
     * PHASE 5 : CAPTURES OFFENSIVES/DÉFENSIVES
     * ══════════════════════════════════════════════════════════════════════ */
    
    /* Capture défensive si l'adversaire a 3+ paires */
    if (g->captures[opponent] >= 3) {
        int block_capture = find_critical_capture_block(g, ia_player);
        if (block_capture != -1) {
            #ifdef DEBUG
            printf("DECISION: Blocage capture (3+ paires) en (%d, %d)\n",
                   GET_X(block_capture), GET_Y(block_capture));
            #endif
            return block_capture;
        }
    }
    
    /* Capture offensive si on a 3+ paires et pas de menace adverse */
    if (g->captures[ia_player] >= 3 && opp_count == 0) {
        int capture = find_best_capture_move(g, ia_player);
        if (capture != -1) {
            #ifdef DEBUG
            printf("DECISION: Capture offensive en (%d, %d)\n", GET_X(capture), GET_Y(capture));
            #endif
            return capture;
        }
    }
    
    /* ══════════════════════════════════════════════════════════════════════
     * AUCUNE DÉCISION TACTIQUE → MINIMAX
     * ══════════════════════════════════════════════════════════════════════ */
    
    #ifdef DEBUG
    printf("DECISION: Aucune urgence tactique, passage au Minimax\n");
    #endif
    
    return -1;
}