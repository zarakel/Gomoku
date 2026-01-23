#include "../include/gomoku.h"

/*
 * ai_unified.c - Système unifié de détection des menaces
 * 
 * PRINCIPE FONDAMENTAL :
 * Une menace est définie par "combien de coups avant de gagner"
 * PAS par son type (alignement vs capture)
 * 
 * Les fonctions de scan sont dans ai_threat_scan.c et ai_threat_response.c
 */

/* ============================================================================
 * FONCTION HELPER : VALIDATION DU COUP
 * ============================================================================ */

static int validate_and_return(game *g, int move, int player) {
    if (move == -1) return -1;
    if (g->board[move] != EMPTY) return -1;
    
    /* Vérifier si c'est un coup interdit */
    if (is_double_three(g, move, player)) {
        #ifdef DEBUG
        printf("⚠️  Coup (%d,%d) interdit (double-three), recherche alternative\n",
               GET_X(move), GET_Y(move));
        #endif
        return -1;  /* Forcer la recherche d'une alternative */
    }
    
    return move;
}
