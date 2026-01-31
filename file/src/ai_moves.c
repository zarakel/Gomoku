#include "../include/gomoku.h"
#include <stdlib.h>

#include "../include/gomoku.h"
#include <stdlib.h>

static int compare_moves(const void *a, const void *b) {
    MoveCandidate *ma = (MoveCandidate *)a;
    MoveCandidate *mb = (MoveCandidate *)b;
    return (mb->score_estim - ma->score_estim);
}

// Helper pour détecter si un coup adverse capturerait nos pièces
static bool would_capture_us(game *g, int idx, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int x = GET_X(idx), y = GET_Y(idx);
    
    // On simule le coup de l'adversaire
    g->board[idx] = opponent;
    int caps = count_potential_captures(g, x, y, opponent);
    g->board[idx] = EMPTY;
    
    return (caps > 0);
}

static int score_move_ordering(game *g, int idx, int player, int tt_move, int depth) {
    if (idx == tt_move) return SORT_HASH;

    int opponent = (player == P1) ? P2 : P1;
    int x = GET_X(idx), y = GET_Y(idx);
    
    // 1. ANALYSE DES CAPTURES (OFFENSIF ET DÉFENSIF)
    int my_caps = count_potential_captures(g, x, y, player);
    bool enemy_can_cap = would_capture_us(g, idx, player);
    
    // 2. ÉVALUATION DES MENACES DE LIGNES
    int atk_score = get_point_score(g, x, y, player);
    int def_score = get_point_score(g, x, y, opponent);

    // --- HIÉRARCHIE DE TRI IMPITOYABLE ---
    
    // Niveau 0 : Victoire immédiate
    if (atk_score >= WIN_SCORE) return SORT_WIN_IMMEDIATE;
    if (g->captures[player] + (my_caps/2) >= 5) return SORT_WIN_IMMEDIATE;

    // Niveau 1 : Survie absolue (Bloquer un 4 ou une capture fatale)
    if (def_score >= WIN_SCORE) return SORT_BLOCK_WIN + 10000000;
    if (g->captures[opponent] >= 4 && enemy_can_cap) return SORT_BLOCK_WIN + 5000000;
    if (def_score >= OPEN_FOUR) return SORT_BLOCK_WIN;

    // Niveau 2 : Attaque de Fourchette (La "Fork" stratégique)
    // Un coup qui crée un Four ou plusieurs Threes
    if (atk_score >= OPEN_FOUR) return SORT_THREAT_MAX;
    
    // Niveau 3 : Capture tactique
    int capture_priority = 0;
    if (my_caps > 0) {
        capture_priority = SORT_CAPTURE + (my_caps * 100000);
        // Bonus si la capture casse un alignement adverse
        if (def_score > 0) capture_priority += 500000; 
    }

    // Niveau 4 : Défense préventive (Empêcher l'adversaire de nous capturer)
    int defense_priority = 0;
    if (enemy_can_cap) {
        defense_priority = SORT_BLOCK_WIN / 2; // Très haute priorité
    }

    // Calcul du score final pour le tri
    int final_score = atk_score + (int)(def_score * 1.3) + capture_priority + defense_priority;

    // Bonus de centralité et heuristiques de recherche
    final_score += (20 - (abs(x - 9) + abs(y - 9))) * 100;
    if (depth >= 0 && depth < MAX_DEPTH) {
        if (idx == killer_moves[depth][0]) final_score += SORT_KILLER_1;
    }
    final_score += history_heuristic[idx];

    return final_score;
}

int generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move) {
    int count = 0;
    int min_x = BOARD_SIZE, max_x = 0, min_y = BOARD_SIZE, max_y = 0;
    bool empty = true;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) {
            empty = false;
            int cx = GET_X(i), cy = GET_Y(i);
            if (cx < min_x) min_x = cx; if (cx > max_x) max_x = cx;
            if (cy < min_y) min_y = cy; if (cy > max_y) max_y = cy;
        }
    }
    
    if (empty) {
        moves[0].index = GET_INDEX(9, 9);
        moves[0].score_estim = 1000;
        return 1;
    }

    // Zone étendue
    min_x = (min_x - 2 < 0) ? 0 : min_x - 2;
    max_x = (max_x + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_x + 2;
    min_y = (min_y - 2 < 0) ? 0 : min_y - 2;
    max_y = (max_y + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_y + 2;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int idx = GET_INDEX(x, y);
            if (g->board[idx] != EMPTY) continue;

            // On garde les cases proches des pierres existantes (distance 2)
            bool interesting = false;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] != EMPTY) {
                        interesting = true; break;
                    }
                }
                if (interesting) break;
            }
            if (!interesting) continue;

            moves[count].index = idx;
            moves[count].score_estim = score_move_ordering(g, idx, player, tt_best_move, depth);
            count++;
        }
    }

    // Tri par score décroissant
    qsort(moves, count, sizeof(MoveCandidate), compare_moves);

    // Élagage (Beam Search) : on garde plus de coups si on est en danger
    int max_to_keep = (g->in_crisis) ? 60 : 30;
    return (count > max_to_keep) ? max_to_keep : count;
}