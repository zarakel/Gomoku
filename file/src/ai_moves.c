#include "../include/gomoku.h"

static bool has_neighbors(game *g, int idx) {
    int cx = GET_X(idx);
    int cy = GET_Y(idx);
    int radius = 2;
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (!IS_VALID(x, y)) continue;
            if (g->board[GET_INDEX(x, y)] != EMPTY) return true;
        }
    }
    return false;
}

static int compare_moves(const void *a, const void *b) {
    return ((MoveCandidate *)b)->score_estim - ((MoveCandidate *)a)->score_estim;
}

int quick_evaluate_move(game *g, int idx, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int x = GET_X(idx);
    int y = GET_Y(idx);

    // 1. Défense (Ce que l'adversaire ferait ici)
    g->board[idx] = opponent;
    int defense_score = get_point_score(g, x, y, opponent);
    g->board[idx] = EMPTY;

    // 2. Attaque (Ce que je fais ici)
    g->board[idx] = player; 
    int attack_score = get_point_score(g, x, y, player);
    g->board[idx] = EMPTY;

    // --- PRIORITÉS ABSOLUES ---
    if (attack_score >= WIN_SCORE) return 2000000000; // Je gagne
    if (defense_score >= WIN_SCORE) return 1900000000; // Je bloque une victoire

    if (attack_score >= OPEN_FOUR) return 1800000000; // Je crée un Open 4 (Win imparable)
    if (defense_score >= OPEN_FOUR) return 1700000000; // Je bloque un Open 4

    // --- COUPS DOUBLES (Attaque + Défense) ---
    // C'est ici que se joue la stratégie. Un coup qui bloque un Open 3 ET crée un Open 3 est meilleur qu'un simple blocage.
    
    int score = attack_score + defense_score;

    // Bonus pour les coups polyvalents
    if (attack_score >= OPEN_THREE && defense_score >= OPEN_THREE) score += 500000;
    if (attack_score >= CLOSED_FOUR && defense_score >= OPEN_THREE) score += 600000;

    return score;
}

int generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move) {
    int count = 0;
    int min_x = BOARD_SIZE, max_x = 0, min_y = BOARD_SIZE, max_y = 0;
    bool empty_board = true;

    // Bounding Box
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) {
            empty_board = false;
            int cx = GET_X(i); int cy = GET_Y(i);
            if (cx < min_x) min_x = cx; if (cx > max_x) max_x = cx;
            if (cy < min_y) min_y = cy; if (cy > max_y) max_y = cy;
        }
    }

    if (empty_board) {
        moves[0].index = GET_INDEX(BOARD_SIZE/2, BOARD_SIZE/2);
        moves[0].score_estim = 20000000;
        return 1;
    }

    min_x = (min_x - 2 < 0) ? 0 : min_x - 2;
    max_x = (max_x + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_x + 2;
    min_y = (min_y - 2 < 0) ? 0 : min_y - 2;
    max_y = (max_y + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_y + 2;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int i = GET_INDEX(x, y);
            if (g->board[i] != EMPTY) continue;
            if (!has_neighbors(g, i)) continue;

            moves[count].index = i;
            int score = 0;

            if (i == tt_best_move) score = 2000000000; 
            else if (i == killer_moves[depth][0]) score = 100000000;
            else if (i == killer_moves[depth][1]) score = 90000000;
            else {
                int tactical = quick_evaluate_move(g, i, player);
                if (tactical >= 1500000000) { // Must-Play Cutoff
                    moves[0].index = i;
                    moves[0].score_estim = tactical;
                    return 1;
                }
                score = tactical + history_heuristic[i];
            }
            moves[count].score_estim = score;
            count++;
        }
    }

    qsort(moves, count, sizeof(MoveCandidate), compare_moves);
    
    // Beam Search
    int final_count = 0;
    int beam_width = (depth >= 6) ? 8 : 12;

    for (int i = 0; i < count; i++) {
        if (i < beam_width || moves[i].score_estim > 4000) final_count++;
        else break;
    }
    return final_count;
}