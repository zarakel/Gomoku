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

    // Évaluation défensive
    g->board[idx] = opponent;
    int defense_score = get_point_score(g, x, y, opponent);
    int opp_captures = count_potential_captures(g, x, y, opponent);
    g->board[idx] = EMPTY;

    // Évaluation offensive
    g->board[idx] = player; 
    int attack_score = get_point_score(g, x, y, player);
    int my_captures = count_potential_captures(g, x, y, player);
    g->board[idx] = EMPTY;

    // --- CAPTURES : PRIORITÉ DYNAMIQUE BASÉE SUR L'ÉTAT ---
    
    // Victoire par capture (5 paires)
    if (g->captures[player] + my_captures / 2 >= 5) {
        return 2000000000; // Victoire !
    }
    
    // Bloquer victoire par capture adverse
    if (g->captures[opponent] + opp_captures / 2 >= 5) {
        return 1950000000; // Blocage critique
    }
    
    // (NOUVEAU) L'adversaire atteint 4 paires après ce coup = DANGER EXTRÊME
    if (g->captures[opponent] + opp_captures / 2 >= 4) {
        return 1940000000 + opp_captures * 10000;
    }
    
    // Opportunité de capture (4 paires après ce coup)
    if (g->captures[player] + my_captures / 2 >= 4) {
        return 1930000000 + my_captures * 10000;
    }
    
    // (NOUVEAU) L'adversaire a 2+ paires et peut en capturer = SÉRIEUX
    if (g->captures[opponent] >= 2 && opp_captures >= 2) {
        return 1910000000 + opp_captures * 10000;
    }
    
    // Opportunité de capture (3 paires après ce coup)
    if (g->captures[player] >= 2 && my_captures >= 2) {
        return 1905000000 + my_captures * 10000;
    }

    // --- HIÉRARCHIE ÉQUILIBRÉE (Attaque = Défense à niveau égal) ---
    
    // Niveau WIN (Victoire immédiate) - ATTAQUE PRIORITAIRE
    if (attack_score >= WIN_SCORE) return 2000000000;
    if (defense_score >= WIN_SCORE) return 1950000000;

    // Niveau OPEN_FOUR - ATTAQUE PRIORITAIRE (victoire garantie)
    if (attack_score >= OPEN_FOUR) return 1900000000;
    if (defense_score >= OPEN_FOUR) return 1850000000;

    // Niveau CLOSED_FOUR - ATTAQUE LÉGÈREMENT PRIORITAIRE (force la réponse)
    if (attack_score >= CLOSED_FOUR) return 1800000000;
    if (defense_score >= CLOSED_FOUR) return 1780000000;

    // Niveau OPEN_THREE - ÉQUILIBRÉ (mais bonus si on fait les deux)
    if (attack_score >= OPEN_THREE && defense_score >= OPEN_THREE) {
        return 1750000000;
    }
    if (attack_score >= OPEN_THREE) return 1700000000;
    if (defense_score >= OPEN_THREE) return 1680000000;

    // Niveau CLOSED_THREE
    if (attack_score >= CLOSED_THREE) return 1600000000;
    if (defense_score >= CLOSED_THREE) return 1580000000;

    // --- BONUS CAPTURES (AUGMENTÉS) ---
    int capture_bonus = 0;
    
    // Bonus offensif pour captures
    if (my_captures >= 2) {
        capture_bonus += 600000 * (my_captures / 2);
        // Bonus supplémentaire si on a déjà des captures
        if (g->captures[player] >= 1) capture_bonus += 200000;
        if (g->captures[player] >= 2) capture_bonus += 300000;
    }
    
    // Bonus défensif pour bloquer captures
    if (opp_captures >= 2) {
        capture_bonus += 500000 * (opp_captures / 2);
        // Bonus supplémentaire si l'adversaire a déjà des captures
        if (g->captures[opponent] >= 1) capture_bonus += 150000;
        if (g->captures[opponent] >= 2) capture_bonus += 250000;
    }

    // Coups standards avec bonus offensif léger
    int combined = attack_score + defense_score + capture_bonus;
    
    // Bonus pour les coups polyvalents
    if (defense_score >= OPEN_TWO && attack_score >= OPEN_TWO) {
        combined += 50000;
    }
    
    // Bonus offensif léger pour encourager le développement
    combined += (attack_score / 10);
    
    return combined;
}

int generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move) {
    int count = 0;
    int effective_depth = (depth < 0) ? 0 : depth; 
    bool disable_pruning = (depth < 0);

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

    // --- PREMIÈRE PASSE : Trouver le meilleur coup DÉFENSIF ---
    int best_defensive_idx = -1;
    int best_defensive_score = 0;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int i = GET_INDEX(x, y);
            if (g->board[i] != EMPTY) continue;
            if (!has_neighbors(g, i)) continue;

            int tactical = quick_evaluate_move(g, i, player);
            
            // Coup défensif critique ? (>= 1800000000 = bloque CLOSED_FOUR ou mieux)
            if (tactical >= 1800000000 && tactical > best_defensive_score) {
                best_defensive_score = tactical;
                best_defensive_idx = i;
            }
        }
    }

    // --- DEUXIÈME PASSE : Générer tous les coups ---
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int i = GET_INDEX(x, y);
            if (g->board[i] != EMPTY) continue;
            if (!has_neighbors(g, i)) continue;

            moves[count].index = i;
            int score = 0;

            if (i == tt_best_move) score = 2100000000; 
            else if (i == killer_moves[effective_depth][0]) score = 100000000;
            else if (i == killer_moves[effective_depth][1]) score = 90000000;
            else {
                score = quick_evaluate_move(g, i, player);
                score += history_heuristic[i];
            }
            moves[count].score_estim = score;
            count++;
        }
    }

    // --- CUTOFF INTELLIGENT ---
    // Ne couper que si on a une victoire immédiate (WIN) ou si on doit bloquer une victoire adverse
    if (best_defensive_score >= 1950000000) { // Bloque un WIN adverse
        moves[0].index = best_defensive_idx;
        moves[0].score_estim = best_defensive_score;
        return 1;
    }

    qsort(moves, count, sizeof(MoveCandidate), compare_moves);
    
    // Si le meilleur coup est une victoire (>= 2000000000), on ne garde que lui
    if (count > 0 && moves[0].score_estim >= 2000000000) {
        return 1;
    }
    
    if (disable_pruning) return count;

    // Beam Search Adaptatif - PLUS LARGE
    int beam_width;
    bool defensive_situation = (count > 0 && moves[0].score_estim >= 1700000000);
    
    if (defensive_situation) {
        beam_width = 25;  // Était 16, augmenté pour ne pas rater de blocages
    } else if (depth >= 8) {
        beam_width = 10;
    } else if (depth >= 6) {
        beam_width = 15;
    } else {
        beam_width = 20;
    }

    int final_count = 0;
    for (int i = 0; i < count; i++) {
        // Garder tous les coups tactiques (>= CLOSED_THREE défense/attaque)
        if (i < beam_width || moves[i].score_estim >= 1550000000) {
            final_count++;
        } else {
            break;
        }
    }
    
    // Garantir un minimum de coups
    if (final_count < 5 && count >= 5) final_count = 5;
    
    return final_count;
}