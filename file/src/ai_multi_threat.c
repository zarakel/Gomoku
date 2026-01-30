#include "../include/gomoku.h"

/*
 * Compte les menaces créées (Open 3 ou mieux).
 * Optimisé pour ne pas surévaluer les coups illégaux.
 */
int count_created_threats(game *g, int idx, int player) {
    if (g->board[idx] != EMPTY) return 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    int threats_count = 0;
    
    g->board[idx] = player;
    
    for (int d = 0; d < 4; d++) {
        int score = evaluate_line(g, x, y, dx[d], dy[d], player);
        if (score >= OPEN_THREE) {
            threats_count++;
        }
    }
    
    g->board[idx] = EMPTY;

    // Si on crée plusieurs menaces, on vérifie si c'est un Double Three légal ou non
    if (threats_count >= 2) {
        if (is_double_three(g, idx, player)) return 0;
    }
    
    return threats_count;
}

/*
 * Détecte les fourchettes (Double attaque simultanée)
 */
int compute_fork_value(game *g, int idx, int player) {
    // Vérification rapide avant calcul coûteux
    if (g->board[idx] != EMPTY) return 0;

    int threats = count_created_threats(g, idx, player);
    
    if (threats >= 2) {
        // FOURCHETTE DÉTECTÉE !
        // Score : Juste sous la victoire immédiate, mais au-dessus de tout blocage
        // WIN_SCORE (20M) > SORT_WIN_IMMEDIATE (20M) > FORK (18M) > BLOCK (15M)
        return SORT_WIN_IMMEDIATE - 2000000; 
    }
    
    return 0;
}

/*
 * Potentiel de jonction (Pré-filtre heuristique)
 */
int compute_junction_potential(game *g, int idx, int player) {
    if (g->board[idx] != EMPTY) return 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int crossings = 0;
    
    for (int d = 0; d < 4; d++) {
        // Regarde à 4 cases de distance max
        for (int k = -4; k <= 4; k++) {
            if (k == 0) continue;
            int nx = x + dx[d] * k;
            int ny = y + dy[d] * k;
            if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] == player) {
                crossings++;
                break; 
            }
        }
    }
    return crossings;
}