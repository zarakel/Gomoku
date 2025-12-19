#include "../include/gomoku.h"
#include <limits.h>

int vcf_search(game *g, int depth, int player, int ia_player, clock_t start_time) {
    if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) return 0;
    if (abs(evaluate_board(g, ia_player)) > WIN_SCORE / 2) return 1; 
    if (depth == 0) return 0; 

    int opponent = (player == P1) ? P2 : P1;
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, player, depth, -1);
    
    MoveCandidate vcf_moves[MAX_BOARD];
    int vcf_count = 0;

    for (int i = 0; i < count; i++) {
        int idx = moves[i].index;
        if (player == ia_player) {
            g->board[idx] = player;
            if (get_point_score(g, GET_X(idx), GET_Y(idx), player) >= CLOSED_FOUR) 
                vcf_moves[vcf_count++] = moves[i];
            g->board[idx] = EMPTY;
        } else {
            g->board[idx] = ia_player;
            if (get_point_score(g, GET_X(idx), GET_Y(idx), ia_player) >= CLOSED_FOUR) 
                vcf_moves[vcf_count++] = moves[i];
            g->board[idx] = EMPTY;
        }
    }

    if (player == ia_player && vcf_count == 0) return 0;
    if (player != ia_player && vcf_count == 0) return 1;

    for (int i = 0; i < vcf_count; i++) {
        MoveUndo undo;
        apply_move(g, vcf_moves[i].index, player, &undo);
        int result = vcf_search(g, depth - 1, opponent, ia_player, start_time);
        undo_move(g, player, &undo);
        if (player == ia_player && result == 1) return 1; 
        if (player != ia_player && result == 0) return 0;
    }
    return (player != ia_player); 
}

/*
 * Évalue le score d'un coup AVEC simulation complète des captures
 */
static int evaluate_move_with_captures_full(game *g, int idx, int player) {
    MoveUndo undo;
    apply_move(g, idx, player, &undo);
    
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
    
    // Scanner aussi les lignes ouvertes par les captures
    if (undo.captured_count > 0) {
        for (int i = 0; i < undo.captured_count; i++) {
            int cap_idx = undo.captured_indices[i];
            int cap_x = GET_X(cap_idx);
            int cap_y = GET_Y(cap_idx);
            
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = cap_x + dx;
                    int ny = cap_y + dy;
                    if (!IS_VALID(nx, ny)) continue;
                    int nidx = GET_INDEX(nx, ny);
                    if (g->board[nidx] == player) {
                        int new_score = get_point_score(g, nx, ny, player);
                        if (new_score > score) {
                            score = new_score;
                        }
                    }
                }
            }
        }
    }
    
    undo_move(g, player, &undo);
    return score;
}

/*
 * CORRIGÉ : Trouve la case où le joueur GAGNE s'il y joue
 * Simule les captures pour détecter les menaces "cachées"
 */
static int find_winning_move(game *g, int player) {
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        // CORRECTION : Utiliser evaluate_move_with_captures_full au lieu de get_point_score
        int score = evaluate_move_with_captures_full(g, i, player);
        
        if (score >= WIN_SCORE) {
            return i;
        }
    }
    return -1;
}

/*
 * CORRIGÉ : Trouve le meilleur blocage en cherchant TOUTES les cases WIN de l'adversaire
 */
static int find_blocking_move(game *g, int threat_player) {
    int ia_player = (threat_player == P1) ? P2 : P1;
    
    // ÉTAPE 1 : Trouver TOUTES les cases où l'adversaire gagne
    int win_moves[10];
    int win_count = 0;
    
    for (int i = 0; i < MAX_BOARD && win_count < 10; i++) {
        if (g->board[i] != EMPTY) continue;
        
        int score = evaluate_move_with_captures_full(g, i, threat_player);
        if (score >= WIN_SCORE) {
            win_moves[win_count++] = i;
        }
    }
    
    // Si une seule case WIN, c'est elle qu'on doit bloquer
    if (win_count == 1) {
        return win_moves[0];
    }
    
    // Si plusieurs cases WIN (Open Four adverse), on est probablement mort
    // Mais on bloque quand même la première
    if (win_count > 1) {
        return win_moves[0];
    }
    
    // ÉTAPE 2 : Pas de WIN immédiat, chercher le CLOSED_FOUR
    // On cherche la case qui, si bloquée, réduit le plus la menace max
    int best_block = -1;
    int best_reduction = 0;
    
    // Score max adverse AVANT blocage
    int threat_before = 0;
    for (int j = 0; j < MAX_BOARD; j++) {
        if (g->board[j] != EMPTY) continue;
        int s = evaluate_move_with_captures_full(g, j, threat_player);
        if (s > threat_before) threat_before = s;
    }
    
    // Tester chaque blocage possible
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        // Simuler le blocage (sans capture, juste poser la pierre)
        g->board[i] = ia_player;
        
        // Score max adverse APRÈS blocage
        int threat_after = 0;
        for (int j = 0; j < MAX_BOARD; j++) {
            if (g->board[j] != EMPTY) continue;
            int s = evaluate_move_with_captures_full(g, j, threat_player);
            if (s > threat_after) threat_after = s;
        }
        
        g->board[i] = EMPTY;
        
        int reduction = threat_before - threat_after;
        if (reduction > best_reduction) {
            best_reduction = reduction;
            best_block = i;
        }
    }
    
    return best_block;
}

/*
 * Cherche les menaces d'un joueur (meilleur coup et score)
 */
static void find_all_threats(game *g, int player, int *best_idx, int *best_score) {
    *best_idx = -1;
    *best_score = 0;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        int score = evaluate_move_with_captures_full(g, i, player);
        
        if (score > *best_score) {
            *best_score = score;
            *best_idx = i;
        }
    }
}

int solve_vcf(game *g, int ia_player, clock_t start_time) {
    int opponent = (ia_player == P1) ? P2 : P1;

    // ==========================================================
    // ÉTAPE 0 : VICTOIRE IMMÉDIATE POUR L'IA (WIN_SCORE)
    // ==========================================================
    int my_win = find_winning_move(g, ia_player);
    if (my_win != -1) return my_win;
    
    // Victoire par capture
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        g->board[i] = ia_player;
        int my_caps = count_potential_captures(g, GET_X(i), GET_Y(i), ia_player);
        g->board[i] = EMPTY;
        if (g->captures[ia_player] + my_caps / 2 >= 5) return i;
    }

    // ==========================================================
    // ÉTAPE 1 : L'ADVERSAIRE PEUT-IL GAGNER IMMÉDIATEMENT ?
    // ==========================================================
    int opp_win = find_winning_move(g, opponent);
    if (opp_win != -1) return opp_win;  // BLOCAGE DIRECT
    
    // Victoire adverse par capture
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        g->board[i] = opponent;
        int opp_caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        if (g->captures[opponent] + opp_caps / 2 >= 5) return i;
    }

    // ==========================================================
    // ÉTAPE 2 : SCAN DES MENACES
    // ==========================================================
    int best_opp_idx = -1;
    int best_opp_score = 0;
    int my_best_idx = -1;
    int my_best_score = 0;
    
    find_all_threats(g, opponent, &best_opp_idx, &best_opp_score);
    find_all_threats(g, ia_player, &my_best_idx, &my_best_score);

    // ==========================================================
    // ÉTAPE 3 : CLOSED_FOUR adverse ou mieux → BLOCAGE INTELLIGENT
    // ==========================================================
    if (best_opp_score >= CLOSED_FOUR) {
        int block = find_blocking_move(g, opponent);
        if (block != -1) return block;
        return best_opp_idx;
    }

    // ==========================================================
    // ÉTAPE 4 : Pas de menace >= CLOSED_FOUR adverse → Attaque
    // ==========================================================
    if (my_best_score >= OPEN_FOUR) {
        return my_best_idx;
    }

    if (my_best_score >= CLOSED_FOUR) {
        return my_best_idx;
    }

    // ==========================================================
    // ÉTAPE 5 : OPEN_THREE adverse → Blocage ou contre-attaque
    // ==========================================================
    if (best_opp_score >= OPEN_THREE) {
        if (my_best_score >= OPEN_THREE) {
            g->board[my_best_idx] = ia_player;
            int blocks_opp = (get_point_score(g, GET_X(best_opp_idx), GET_Y(best_opp_idx), opponent) < best_opp_score);
            g->board[my_best_idx] = EMPTY;
            
            if (blocks_opp) {
                return my_best_idx;
            }
        }
        return best_opp_idx;
    }
    
    // ==========================================================
    // ÉTAPE 6 : OPEN_THREE pour moi → Développement
    // ==========================================================
    if (my_best_score >= OPEN_THREE) {
        return my_best_idx;
    }
    
    // ==========================================================
    // ÉTAPE 7 : Pas de menace critique → MINIMAX
    // ==========================================================
    return -1;
}

int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, int ia_player, clock_t start_time) {
    debug_node_count++;

    TTEntry *entry = tt_probe(g->current_hash);
    if (entry != NULL && entry->depth >= depth) {
        if (entry->flag == TT_EXACT) return entry->value;
        else if (entry->flag == TT_LOWERBOUND) { if (entry->value > alpha) alpha = entry->value; }
        else if (entry->flag == TT_UPPERBOUND) { if (entry->value < beta) beta = entry->value; }
        if (alpha >= beta) {
            debug_cutoff_count++;
            return entry->value;
        }
    }
    
    if ((debug_node_count & 2047) == 0) {
        if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) return -2;
    }

    int current_player = maximizingPlayer ? ia_player : ((ia_player == P1) ? P2 : P1);

    int current_eval = evaluate_board(g, ia_player);
    if (abs(current_eval) >= WIN_SCORE / 2) return current_eval;

    if (depth <= 0) {
        return current_eval;
    }

    MoveCandidate moves[MAX_BOARD];
    int tt_move = (entry != NULL) ? entry->best_move : -1; 

    int move_count = generate_moves(g, moves, current_player, depth, tt_move);

    if (move_count == 0) return current_eval;

    int best_val = maximizingPlayer ? (-WIN_SCORE - 1000) : (WIN_SCORE + 1000);
    int best_move_this_node = moves[0].index;

    for (int i = 0; i < move_count; i++) {
        int idx = moves[i].index;

        if (is_double_three(g, idx, current_player)) {
            continue; 
        }

        MoveUndo undo;
        apply_move(g, idx, current_player, &undo);
        
        int val;
        
        if (i == 0) {
            val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
        } else {
            int reduction = 0;
            if (depth >= 4 && i >= 4) reduction = 1;
            if (depth >= 6 && i >= 8) reduction = 2;
            
            if (maximizingPlayer) {
                val = minimax(g, depth - 1 - reduction, alpha, alpha + 1, !maximizingPlayer, ia_player, start_time);
            } else {
                val = minimax(g, depth - 1 - reduction, beta - 1, beta, !maximizingPlayer, ia_player, start_time);
            }

            if (reduction > 0 && ((maximizingPlayer && val > alpha) || (!maximizingPlayer && val < beta))) {
                val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
            }
        }

        undo_move(g, current_player, &undo);
        if (val == -2) return -2;

        if (maximizingPlayer) {
            if (val > best_val) { best_val = val; best_move_this_node = idx; }
            if (val >= beta) {
                debug_cutoff_count++;
                tt_save(g->current_hash, depth, val, TT_LOWERBOUND, best_move_this_node);
                return best_val;
            }
            if (val > alpha) alpha = val;
        } else {
            if (val < best_val) { best_val = val; best_move_this_node = idx; }
            if (val <= alpha) {
                debug_cutoff_count++;
                tt_save(g->current_hash, depth, val, TT_UPPERBOUND, best_move_this_node);
                return best_val;
            }
            if (val < beta) beta = val;
        }
    }

    tt_save(g->current_hash, depth, best_val, TT_EXACT, best_move_this_node);
    return best_val;
}