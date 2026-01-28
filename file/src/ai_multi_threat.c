#include "../include/gomoku.h"

/*
 * Compte combien de menaces sérieuses (OPEN_THREE ou mieux) 
 * seraient créées si 'player' jouait en 'idx'.
 */
int count_created_threats(game *g, int idx, int player) {
    if (g->board[idx] != EMPTY) return 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    int threats_count = 0;
    
    // On simule la pose de la pierre
    g->board[idx] = player;
    
    for (int d = 0; d < 4; d++) {
        // On utilise la fonction existante evaluate_line
        int score = evaluate_line(g, x, y, dx[d], dy[d], player);
        
        // Si ce coup crée une menace sérieuse sur cet axe
        if (score >= OPEN_THREE) {
            threats_count++;
        }
    }
    
    g->board[idx] = EMPTY;

    // Si on crée un Double Three interdit, ce n'est pas une menace valide (pour P1)
    // Note : On suppose que is_double_three gère la règle Renju si active
    if (threats_count >= 2) {
        if (is_double_three(g, idx, player)) return 0;
    }
    
    return threats_count;
}

/*
 * Calcule un score BONUS pour une fourchette.
 * Retourne un score énorme si c'est une fourchette, 0 sinon.
 */
int compute_fork_value(game *g, int idx, int player) {
    // Optimisation : On ne lance l'analyse lourde que si le coup a déjà un potentiel
    // (Cette vérification est faite dans ai_moves.c pour gagner du temps)
    
    int threats = count_created_threats(g, idx, player);
    
    if (threats >= 2) {
        // C'EST UNE FOURCHETTE !
        // C'est quasiment une victoire assurée.
        // On retourne un score juste en dessous de la victoire immédiate
        // pour que ce coup soit trié en premier.
        return 1500000000; // SORT_WIN_IMMEDIATE - epsilon
    }
    
    return 0;
}

/*
 * Pré-filtre géométrique rapide (inchangé ou optimisé)
 */
int compute_junction_potential(game *g, int idx, int player) {
    if (g->board[idx] != EMPTY) return 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int crossings = 0;
    
    for (int d = 0; d < 4; d++) {
        // On regarde juste s'il y a des pierres amies proches dans cette direction
        for (int k = -4; k <= 4; k++) {
            if (k == 0) continue;
            int nx = x + dx[d] * k;
            int ny = y + dy[d] * k;
            if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] == player) {
                crossings++;
                break; // Une pierre suffit pour dire "cette ligne est active"
            }
        }
    }
    return crossings;
}