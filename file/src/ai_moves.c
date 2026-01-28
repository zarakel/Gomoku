#include "../include/gomoku.h"
#include <stdlib.h>

/*
 * SCORE HEURISTIQUE DES COUPS
 * C'est ici qu'on définit "l'intuition" de l'IA avant de calculer.
 */

static int compare_moves(const void *a, const void *b) {
    MoveCandidate *ma = (MoveCandidate *)a;
    MoveCandidate *mb = (MoveCandidate *)b;
    return (mb->score_estim - ma->score_estim); // Décroissant
}

// Fonction helper pour vérifier l'adjacence directe
bool is_directly_adjacent_to_threat(game *g, int idx, int opponent) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int dx[] = {1, 1, 0, -1, -1, -1, 0, 1};
    int dy[] = {0, 1, 1, 1, 0, -1, -1, -1};
    
    for(int i=0; i<8; i++) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        if(IS_VALID(nx,ny) && g->board[GET_INDEX(nx,ny)] == opponent) {
            return true;
        }
    }
    return false;
}

/* ai_moves.c */

static int score_move_ordering(game *g, int idx, int player, int tt_move, int depth) {
    // 1. HASH MOVE (Priorité Absolue)
    if (idx == tt_move) return SORT_HASH;

    int opponent = (player == P1) ? P2 : P1;
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    // 2. ANALYSE CAPTURE (inchangé)
    int captures = check_capture_count(g, idx, player);
    if (captures > 0) {
        if (g->captures[player] + captures >= 5) return SORT_WIN_IMMEDIATE;
        return SORT_CAPTURE + (captures * 10000); 
    }

    int score = 0;

    // 3. MENACES & BLOCAGES (Modifié)
    
    // A. Simuler Attaque (Mon coup)
    g->board[idx] = player;
    int atk_score = get_point_score(g, x, y, player);
    g->board[idx] = EMPTY;

    if (atk_score >= WIN_SCORE) return SORT_WIN_IMMEDIATE;

    // B. Simuler Défense (Bloquer l'adversaire)
    g->board[idx] = opponent; 
    int def_score = get_point_score(g, x, y, opponent);
    g->board[idx] = EMPTY;
    
    if (def_score >= WIN_SCORE) return SORT_BLOCK_WIN;

    // --- NOUVEAU : DÉTECTION DES FOURCHETTES (FORKS) ---
    
    // CAS 1 : C'est une Fourchette pour MOI ? (Je gagne)
    if (atk_score >= OPEN_THREE) {
        // Si je crée déjà une menace, est-ce que j'en crée une deuxième ?
        int fork_bonus = compute_fork_value(g, idx, player);
        if (fork_bonus > 0) {
            // C'est une fourchette ! Priorité quasi-absolue.
            return fork_bonus; 
        }
    }
    
    // CAS 2 : C'est une Fourchette pour LUI ? (Il va gagner, je dois bloquer)
    // On ne vérifie que si c'est un point "chaud" pour l'adversaire (défense utile)
    if (def_score >= OPEN_THREE) {
        // Est-ce que l'adversaire allait faire une fourchette ici ?
        int opp_fork_bonus = compute_fork_value(g, idx, opponent);
        if (opp_fork_bonus > 0) {
            // IL FAUT BLOQUER !
            // On donne un score supérieur à une simple défense, mais inférieur à notre victoire.
            // SORT_BLOCK_WIN (1.8Mrd) > Block Fork > Normal Moves
            return 1400000000; 
        }
    }
    // ---------------------------------------------------

    // Scores standards (inchangés ou légèrement ajustés)
    if (atk_score >= OPEN_FOUR) score += 50000000;
    else if (atk_score >= CLOSED_FOUR) score += 20000000;
    else if (atk_score >= OPEN_THREE) score += 10000000;
    
    if (def_score >= OPEN_FOUR) score += 40000000;
    else if (def_score >= CLOSED_FOUR) score += 20000000;
    else if (def_score >= OPEN_THREE) score += 5000000;

    // 4. KILLER MOVES (inchangé)
    if (depth < MAX_DEPTH) {
        if (idx == killer_moves[depth][0]) score += SORT_KILLER_1;
        else if (idx == killer_moves[depth][1]) score += SORT_KILLER_2;
    }

    // 5. HISTORY HEURISTIC (inchangé)
    score += history_heuristic[idx];

    return score;
}

// Fonction légère pour détecter si un coup capture quelque chose
// Retourne le nombre de paires capturées
int check_capture_count(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;
    int captures = 0;
    
    int dx[] = {1, 1, 0, -1, -1, -1, 0, 1};
    int dy[] = {0, 1, 1, 1, 0, -1, -1, -1};

    for (int i = 0; i < 8; i++) {
        int x1 = x + dx[i];
        int y1 = y + dy[i];
        int x2 = x + dx[i] * 2;
        int y2 = y + dy[i] * 2;
        int x3 = x + dx[i] * 3;
        int y3 = y + dy[i] * 3;

        if (IS_VALID(x3, y3)) {
            // Pattern : [Moi] [Adv] [Adv] [Moi]
            if (g->board[GET_INDEX(x1, y1)] == opponent &&
                g->board[GET_INDEX(x2, y2)] == opponent &&
                g->board[GET_INDEX(x3, y3)] == player) {
                captures++;
            }
        }
    }
    return captures;
}

int generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move) {
    int count = 0;
    
    // --- 1. OPTIMISATION ZONE DE RECHERCHE ---
    // On réduit le plateau aux zones intéressantes pour éviter de scanner du vide
    int min_x = BOARD_SIZE, max_x = 0, min_y = BOARD_SIZE, max_y = 0;
    bool empty = true;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) {
            empty = false;
            int cx = GET_X(i); int cy = GET_Y(i);
            if (cx < min_x) min_x = cx; if (cx > max_x) max_x = cx;
            if (cy < min_y) min_y = cy; if (cy > max_y) max_y = cy;
        }
    }
    
    // Premier coup au centre si plateau vide
    if (empty) {
        moves[0].index = GET_INDEX(BOARD_SIZE/2, BOARD_SIZE/2);
        moves[0].score_estim = WEIGHT_WIN;
        return 1;
    }

    // Marge de 2 cases autour des pierres existantes
    min_x = (min_x - 2 < 0) ? 0 : min_x - 2;
    max_x = (max_x + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_x + 2;
    min_y = (min_y - 2 < 0) ? 0 : min_y - 2;
    max_y = (max_y + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_y + 2;

    // --- 2. GÉNÉRATION & HEURISTIQUE LÉGÈRE ---
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int idx = GET_INDEX(x, y);
            if (g->board[idx] != EMPTY) continue;

            // Optimisation : On ne joue que s'il y a une pierre à max 2 cases (Voisinage)
            bool has_neighbor = false;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    if (dx==0 && dy==0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] != EMPTY) {
                        has_neighbor = true; break;
                    }
                }
                if (has_neighbor) break;
            }
            if (!has_neighbor) continue;

            moves[count].index = idx;
            // Appel à l'heuristique rapide (score_move_ordering doit être efficace)
            moves[count].score_estim = score_move_ordering(g, idx, player, tt_best_move, depth);
            
            // Pré-calcul capture pour le tri
            int caps = check_capture_count(g, idx, player);
            moves[count].is_capture = (caps > 0);

            count++;
        }
    }

    // Tri des coups du meilleur au pire
    qsort(moves, count, sizeof(MoveCandidate), compare_moves);

    // --- 3. SELECTION (BEAM SEARCH + EXTENSIONS) ---
    int final_count = 0;
    int max_beam;
    
    // A. Calcul de la largeur du faisceau (Beam Width)
    // Modification pour utiliser plus de temps :
    if (g->in_crisis) {
        max_beam = 100; // Augmenté de 80 à 100 pour tout voir en crise
    } else {
        // Jeu normal : On ouvre BEAUCOUP plus
        if (depth <= 4) max_beam = 120;      // Au début (racine), on regarde 120 coups !
        else if (depth <= 8) max_beam = 80;  // Milieu : 80 coups
        else max_beam = 40;                  // Fin : 40 coups
    }
    
    // Quiescence Search (Profondeur négative ou nulle) : Très sélectif pour la vitesse
    if (depth <= 0) max_beam = 12;

    for (int i = 0; i < count; i++) {
        bool keep = false;
        int idx = moves[i].index;

        // --- Règle 1 : Victoire Immédiate ---
        if (moves[i].score_estim >= SORT_WIN_IMMEDIATE) {
            // Check légalité IMMÉDIATEMENT
            if (!is_double_three(g, idx, player)) {
                moves[0] = moves[i];
                return 1; // On ne retourne que celui-là, c'est gagné.
            }
            continue; // Si illégal, on l'ignore totalement
        }

        // --- Règle 2 : Le Quota (Beam) ---
        if (i < max_beam) {
            keep = true;
        }
        // --- Règle 3 : Les Extensions (Hors Quota) ---
        else {
            // A. Captures : Toujours garder (change le matériel)
            if (moves[i].is_capture) keep = true;
            
            // B. Menaces d'alignement (C'EST ICI QUE TU GAGNES L'ATTAQUE)
            // On garde tout ce qui crée un Open 3 ou mieux, même si c'est mal noté
            // CLOSED_THREE est généralement autour de 5000-10000 points.
            else if (moves[i].score_estim >= CLOSED_THREE) keep = true;
            
            // C. Coups Défensifs Critiques (identifiés par ai_crisis)
            else if (g->in_crisis) {
                for (int j = 0; j < g->crisis_move_count; j++) {
                    if (idx == g->crisis_moves[j]) {
                        keep = true; break;
                    }
                }
            }
        }

        if (keep) {
            // --- Règle 4 : Filtrage Légal (CRITIQUE) ---
            // On ne garde JAMAIS un coup Double-Three dans l'arbre Minimax.
            // Cela évite que l'IA "pense" gagner avec un coup interdit.
            if (is_double_three(g, idx, player)) {
                continue; 
            }

            moves[final_count++] = moves[i];
        }
    }

    // Filet de sécurité : Si on a tout filtré (ex: que des coups illégaux), on cherche n'importe quoi de légal
    if (final_count == 0 && count > 0) {
        for (int i = 0; i < count; i++) {
            if (!is_double_three(g, moves[i].index, player)) {
                moves[0] = moves[i];
                return 1;
            }
        }
    }

    return final_count;
}