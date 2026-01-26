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
        // On utilise votre fonction evaluate_line désormais publique
        int score = evaluate_line(g, x, y, dx[d], dy[d], player);
        
        // Si ce coup crée une menace sérieuse sur cet axe
        if (score >= OPEN_THREE) {
            threats_count++;
        }
    }
    
    g->board[idx] = EMPTY;
    return threats_count;
}

/*
 * Détecte si une case est un "Point Focal" (Fork Spot) pour un joueur.
 * Retourne un score énorme si c'est une fourchette.
 * Utilisé par heuristics.c pour pénaliser ces cases.
 */
int compute_fork_value(game *g, int idx, int player) {
    // 1. Pré-filtre rapide : Est-ce une jonction géométrique ?
    // Inutile de lancer l'analyse lourde si la case est isolée
    int potential = compute_junction_potential(g, idx, player);
    if (potential < 2) return 0; 
    
    // 2. Analyse réelle : Est-ce que ça crée 2 vraies menaces ?
    int threats = count_created_threats(g, idx, player);
    
    if (threats >= 2) {
        // C'EST UNE FOURCHETTE ! (Double Three ou mieux)
        // C'est quasiment une victoire assurée pour celui qui joue là.
        return 1800000000; // Score "Urgence Absolue"
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