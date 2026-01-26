#include "../include/gomoku.h"
#include <stdlib.h>

static int compare_moves(const void *a, const void *b) {
    // Tri décroissant
    if (((MoveCandidate *)b)->score_estim > ((MoveCandidate *)a)->score_estim) return 1;
    if (((MoveCandidate *)b)->score_estim < ((MoveCandidate *)a)->score_estim) return -1;
    return 0;
}

// Vérifie si le coup est suicidaire (inchangé)
static bool is_move_suicidal(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};

    g->board[idx] = player;
    bool suicidal = false;

    for (int i = 0; i < 4; i++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            int dir_x = dx[i] * sign;
            int dir_y = dy[i] * sign;
            int n1_x = x + dir_x;
            int n1_y = y + dir_y;
            
            if (IS_VALID(n1_x, n1_y) && g->board[GET_INDEX(n1_x, n1_y)] == player) {
                int opp_x = x - dir_x;
                int opp_y = y - dir_y;
                int far_x = n1_x + dir_x;
                int far_y = n1_y + dir_y;

                bool side1_blocked = IS_VALID(opp_x, opp_y) && g->board[GET_INDEX(opp_x, opp_y)] == opponent;
                bool side2_blocked = IS_VALID(far_x, far_y) && g->board[GET_INDEX(far_x, far_y)] == opponent;
                
                if (side1_blocked && side2_blocked) suicidal = true;
                if ((side1_blocked && IS_VALID(far_x, far_y) && g->board[GET_INDEX(far_x, far_y)] == EMPTY) ||
                    (side2_blocked && IS_VALID(opp_x, opp_y) && g->board[GET_INDEX(opp_x, opp_y)] == EMPTY)) {
                    suicidal = true;
                }
            }
            if (suicidal) break;
        }
        if (suicidal) break;
    }
    g->board[idx] = EMPTY;
    return suicidal;
}

int generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move) {
    int count = 0;
    int opponent = (player == P1) ? P2 : P1;
    
    // 1. Bounding Box
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
    if (empty) {
        moves[0].index = GET_INDEX(BOARD_SIZE/2, BOARD_SIZE/2);
        moves[0].score_estim = 2000000000;
        return 1;
    }
    min_x = (min_x - 2 < 0) ? 0 : min_x - 2;
    max_x = (max_x + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_x + 2;
    min_y = (min_y - 2 < 0) ? 0 : min_y - 2;
    max_y = (max_y + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_y + 2;

    // 2. Scan et Scoring
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int idx = GET_INDEX(x, y);
            if (g->board[idx] != EMPTY) continue;
            
            // Filtre Voisinage
            bool has_neighbor = false;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (IS_VALID(nx, ny) && g->board[ny * BOARD_SIZE + nx] != EMPTY) {
                        has_neighbor = true; break;
                    }
                }
                if (has_neighbor) break;
            }
            if (!has_neighbor) continue;

            // --- SCORING ---
            int score = 0;

            // A. ALIGNEMENTS
            g->board[idx] = player;
            int my_attack = get_point_score(g, x, y, player);
            g->board[idx] = EMPTY;

            g->board[idx] = opponent;
            int opp_threat = get_point_score(g, x, y, opponent);
            g->board[idx] = EMPTY;

            // B. CAPTURES (C'était la partie manquante !)
            // Combien de pierres JE capture ?
            // count_potential_captures regarde si poser une pierre en (x,y) capture quelque chose.
            // On doit simuler la pierre posée pour que la fonction puisse vérifier les patterns.
            
            // 1. Mes captures (Attaque)
            g->board[idx] = player; 
            int my_captured_stones = count_potential_captures(g, x, y, player);
            g->board[idx] = EMPTY;
            int my_captured_pairs = my_captured_stones / 2;
            
            // 2. Ses captures (Défense / Blocage)
            // Si l'adversaire jouait là, combien il capturerait ?
            g->board[idx] = opponent;
            int opp_captured_stones = count_potential_captures(g, x, y, opponent);
            g->board[idx] = EMPTY;
            int opp_captured_pairs = opp_captured_stones / 2;

            bool suicidal = false;
            if (opp_threat >= OPEN_THREE) {
                if (is_move_suicidal(g, idx, player)) suicidal = true;
            }

            // --- HIERARCHIE DES DECISIONS ---

            // 1. VICTOIRE IMMEDIATE (Attaque)
            // On met un score MAX pour qu'il soit testé en premier.
            // IMPORTANT : PAS DE RETURN ICI ! On continue pour voir si on doit aussi défendre.
            if (my_attack >= WIN_SCORE) {
                 score = 2147483647; 
            }
            // 1b. VICTOIRE PAR CAPTURE
            else if (my_captured_pairs > 0 && g->captures[player] + my_captured_pairs >= 5) {
                score = 2147483647;
            }

            // 2. SURVIE IMMEDIATE (Défense)
            // Si on n'a pas déjà trouvé une victoire (score < MAX), on regarde l'urgence défensive.
            // Le blocage reçoit un score légèrement inférieur à la victoire (2 Mrd vs 2.14 Mrd)
            if (score < 2000000000) { 
                if (opp_threat >= WIN_SCORE || (opp_captured_pairs > 0 && g->captures[opponent] + opp_captured_pairs >= 5)) {
                        score = 2000000000; 
                        if (suicidal) score -= 1000000; 
                }
            }
            
            // 3. GROSSES MENACES (Open 4)
            // Si ce n'est ni une victoire immédiate ni une défaite immédiate
            if (score < 1900000000) {
                if (opp_threat >= OPEN_FOUR) {
                    score = 1910000000; // Priorité défense sur l'attaque non-létale
                    if (suicidal) score -= 10000000; 
                }
                else if (my_attack >= OPEN_FOUR) {
                    score = 1900000000; 
                }
                // 4. Open 3 et Captures Tactiques
                else {
                    score = my_attack + opp_threat; // Base

                    // Bonus Captures
                    if (my_captured_pairs > 0) score += my_captured_pairs * 700000000; 
                    if (opp_captured_pairs > 0) score += opp_captured_pairs * 800000000;

                    // Open 3
                    if (opp_threat >= OPEN_THREE) { 
                        score += 600000000; 
                        if (suicidal) score = 500; 
                    }
                    else if (my_attack >= OPEN_THREE) {
                        score += 500000000; 
                    }
                    
                    if (idx == tt_best_move) score += 10000000;
                    if (is_move_suicidal(g, idx, player)) score -= 500000;
                }
            }

            moves[count].index = idx;
            moves[count].score_estim = score;
            count++;
        }
    }

    // 3. Tri
    qsort(moves, count, sizeof(MoveCandidate), compare_moves);

    // 4. Validation (Double Three)
    int valid_count = 0;
    int max_check = (count > 50) ? 50 : count;
    MoveCandidate final_moves[MAX_BOARD];

    for (int i = 0; i < max_check; i++) {
        // C'est ICI que le "faux" coup gagnant sera éliminé s'il est interdit
        if (!is_double_three(g, moves[i].index, player)) {
            final_moves[valid_count++] = moves[i];
        }
    }

    // Recopie
    for(int i=0; i<valid_count; i++) moves[i] = final_moves[i];

    // 5. Pruning d'Urgence
    // On garde tous les coups critiques (> 1.4 Milliard)
    if (valid_count > 0 && final_moves[0].score_estim >= 1400000000) {
        int urgent = 0;
        for(int i=0; i<valid_count; i++) {
            if(final_moves[i].score_estim >= 1400000000) urgent++;
            else break;
        }
        if (urgent > 0) return urgent;
    }

    int beam_width = (depth >= 6) ? 12 : 18;
    return (valid_count > beam_width) ? beam_width : valid_count;
}