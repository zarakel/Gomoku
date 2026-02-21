#include "../include/gomoku.h"

/**
 * Module de detection des menaces multiples et fourchettes.
 * 
 * Fonctions principales :
 * - Comptage des menaces creees par un coup
 * - Detection des fourchettes (double/triple menaces simultanees)
 * - Calcul du potentiel de jonction d'une case
 */

/**
 * Compte le nombre de menaces (OPEN_THREE ou mieux) creees par un coup.
 * 
 * Simule le coup temporairement et evalue les 4 directions.
 * Verifie la legalite (regle du double-three).
 * 
 * Retourne le nombre de menaces creees (0 si coup illegal).
 */
int count_created_threats(game *g, int idx, int player) {
    if (g->board[idx] != EMPTY) return 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    int open_threats = 0;
    int mixed_threats = 0;
    
    g->board[idx] = player;
    
    for (int d = 0; d < 4; d++) {
        int score = evaluate_line(g, x, y, dx[d], dy[d], player);
        if (score >= OPEN_THREE) open_threats++;
        else if (score >= CLOSED_THREE) mixed_threats++;
    }
    
    g->board[idx] = EMPTY;

    // Fourchette OUVERTE : 2+ open_threes  (imparable sauf blocage immédiat)
    // Fourchette MIXTE  : 1 open_three + 1 closed_three  (dangereux ca 2 coups)
    // Pur closed×2 ignoré ici : défendable en 1 coup (bloquer l'open_end).
    int total_threats = open_threats + mixed_threats;
    if (total_threats < 2 || open_threats < 1) return 0;

    if (total_threats >= 2) {
        if (is_double_three(g, idx, player)) return 0;
    }
    return total_threats;
}

/**
 * Calcule la valeur strategique d'une fourchette.
 * 
 * Une fourchette est un coup qui cree simultanement plusieurs menaces.
 * L'adversaire ne peut bloquer qu'une menace a la fois, donc :
 * - Double fourchette (2 menaces) : Tres dangereux
 * - Triple fourchette (3+ menaces) : Quasiment imparable
 * 
 * Retourne un score tres eleve si fourchette detectee, 0 sinon.
 */
int compute_fork_value(game *g, int idx, int player) {
    // Vérification rapide avant calcul coûteux
    if (g->board[idx] != EMPTY) return 0;

    int threats = count_created_threats(g, idx, player);
    
    if (threats >= 3) {
        // TRIPLE FOURCHETTE ! Presque imparable
        // Score légèrement inférieur à la victoire immédiate
        return SORT_WIN_IMMEDIATE - 500000;
    } else if (threats == 2) {
        // DOUBLE FOURCHETTE CLASSIQUE
        // L'adversaire ne peut bloquer qu'une seule menace
        // Score : Priorité offensive maximale
        return SORT_FORK;
    }
    
    return 0;
}

/**
 * Calcule le potentiel de jonction d'une case.
 * 
 * Une jonction est une case ou plusieurs lignes de pierres se croisent.
 * Plus une case a de jonctions, plus elle est strategiquement importante.
 * 
 * Compte le nombre de directions (sur 4) contenant des pierres alliees proches.
 * Retourne le nombre de croisements detectes (0-4).
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