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
    
    // 2. ANALYSE CAPTURE (Critique pour la variante Pente)
    // On utilise ta fonction légère existante
    int captures = check_capture_count(g, idx, player); // Retourne nombre de PAIRES ou PIERRES? (Ton code dit paires capturées * 2 ou nb captures ?)
    // check_capture_count dans ton code retourne le nombre de paires (vu la boucle 0..8) ? 
    // Vérification : Ton check_capture_count semble retourner le nombre de pattern trouvés (donc paires).
    
    if (captures > 0) {
        // Est-ce une capture gagnante ? (J'ai déjà 4 paires + celle-ci = 5)
        if (g->captures[player] + captures >= 5) return SORT_WIN_IMMEDIATE;
        
        // Est-ce qu'on capture pour casser un 4 adverse ? (Très fort)
        // (Simplification : toute capture est très bonne)
        return SORT_CAPTURE + (captures * 10000); 
    }

    int score = 0;

    // 3. MENACES & BLOCAGES (Heuristique rapide)
    
    // Simuler Défense
    g->board[idx] = opponent; 
    int def_score = get_point_score(g, x, y, opponent);
    g->board[idx] = EMPTY;
    
    if (def_score >= WIN_SCORE) return SORT_BLOCK_WIN; // Bloquer victoire immédiate

    // Simuler Attaque
    g->board[idx] = player;
    int atk_score = get_point_score(g, x, y, player);
    g->board[idx] = EMPTY;

    if (atk_score >= WIN_SCORE) return SORT_WIN_IMMEDIATE; // Victoire immédiate par alignement

    // Pondération Attaque / Défense pour les coups normaux
    if (atk_score >= OPEN_FOUR) score += 5000000;
    else if (atk_score >= CLOSED_FOUR) score += 2000000;
    else if (atk_score >= OPEN_THREE) score += 1000000;
    
    if (def_score >= OPEN_FOUR) score += 4000000; // Bloquer un 4 est prioritaire sur créer un 4 fermé
    else if (def_score >= CLOSED_FOUR) score += 2000000;
    else if (def_score >= OPEN_THREE) score += 500000;

    // 4. KILLER MOVES
    if (depth < MAX_DEPTH) { // Sécurité tableau
        if (idx == killer_moves[depth][0]) score += SORT_KILLER_1;
        else if (idx == killer_moves[depth][1]) score += SORT_KILLER_2;
    }

    // 5. HISTORY HEURISTIC (Tie-breaker)
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
    
    // 1. Définition de la zone de recherche (Bounding Box + 2 cases)
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
    
    // Si plateau vide, jouer au centre
    if (empty) {
        moves[0].index = GET_INDEX(BOARD_SIZE/2, BOARD_SIZE/2);
        moves[0].score_estim = WEIGHT_WIN;
        return 1;
    }

    // Élargir la zone de recherche de 2 cases autour des pierres existantes
    min_x = (min_x - 2 < 0) ? 0 : min_x - 2;
    max_x = (max_x + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_x + 2;
    min_y = (min_y - 2 < 0) ? 0 : min_y - 2;
    max_y = (max_y + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_y + 2;

    // 2. Génération et Notation
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int idx = GET_INDEX(x, y);
            if (g->board[idx] != EMPTY) continue;

            // Filtre voisin (Garde-le, c'est bien)
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

            // --- NOTATION OPTIMISÉE ---
            moves[count].index = idx;
            moves[count].score_estim = score_move_ordering(g, idx, player, tt_best_move, depth);
            
            // On marque si c'est une capture pour la Quiescence Search
            int caps = check_capture_count(g, idx, player);
            moves[count].is_capture = (caps > 0);

            count++;
        }
    }

    // Tri
    qsort(moves, count, sizeof(MoveCandidate), compare_moves);

    // --- BEAM SEARCH INTELLIGENT ---
    // Au lieu de couper brutalement à 20, on coupe seulement si le score chute trop
    // ou si on a dépassé un quota ET qu'on n'est pas sur un coup forcé.
    
    int final_count = 0;
    int max_beam = (depth <= 4) ? 30 : 20;
    
    // Si on est en Quiescence (depth <= 0), on est très strict
    if (depth <= 0) max_beam = 12;

    for (int i = 0; i < count; i++) {
        // 1. Si on a trouvé une victoire immédiate, on ne retourne QUE celle-là (Cutoff parfait)
        if (moves[i].score_estim >= SORT_WIN_IMMEDIATE) {
            // Petite vérif Double-Three pour être sûr
            if (!is_double_three(g, moves[i].index, player)) {
                moves[0] = moves[i];
                return 1; 
            }
            continue; 
        }

        // 2. Si on a dépassé le quota, on vérifie si le coup vaut le coup d'être gardé
        if (i >= max_beam) {
            // On garde quand même si c'est une capture ou un blocage critique
            if (moves[i].score_estim < SORT_CAPTURE && moves[i].score_estim < SORT_BLOCK_WIN) {
                continue; // On jette
            }
        }

        // 3. Validation Double-Three (Uniquement pour les coups gardés)
        if (moves[i].score_estim > 1000) { // On ne check que les coups un minimum pertinents
            if (is_double_three(g, moves[i].index, player)) continue;
        }

        moves[final_count++] = moves[i];
    }

    return final_count;
}