#include "../include/gomoku.h"
#include <limits.h>

// Déclarations externes
extern int find_gapped_four_hole(game *g, int player);
extern int find_gapped_three_hole(game *g, int player);

// ============================================================================
// VCF SEARCH
// ============================================================================

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

// ============================================================================
// SOLVE_VCF - DÉCISION TACTIQUE
// ============================================================================

int solve_vcf(game *g, int ia_player, clock_t start_time) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    int my_capture_threat = compute_unified_threat_level(g, ia_player);
    int opp_capture_threat = compute_unified_threat_level(g, opponent);

    // ==========================================================
    // ÉTAPE 0 : VICTOIRE IMMÉDIATE
    // ==========================================================
    int my_win = find_winning_move(g, ia_player);
    if (my_win != -1) return my_win;
    
    if (my_capture_threat >= WIN_SCORE) {
        int capture_win = find_best_capture_move(g, ia_player);
        if (capture_win != -1) return capture_win;
    }

    // ==========================================================
    // ÉTAPE 0.5 : GAPPED FOUR IA
    // ==========================================================
    int my_gapped_four = find_gapped_four_hole(g, ia_player);
    if (my_gapped_four != -1) {
        int score = evaluate_move_with_captures_full(g, my_gapped_four, ia_player);
        if (score >= WIN_SCORE) {
            return my_gapped_four;
        }
    }

    // ==========================================================
    // ÉTAPE 1 : BLOCAGE VICTOIRE ADVERSE
    // ==========================================================
    int opp_win = find_winning_move(g, opponent);
    if (opp_win != -1) return opp_win;
    
    if (opp_capture_threat >= WIN_SCORE) {
        int capture_block = find_critical_capture_block(g, ia_player);
        if (capture_block != -1) return capture_block;
    }
    
    int opp_gapped_four = find_gapped_four_hole(g, opponent);
    if (opp_gapped_four != -1) {
        int opp_score = evaluate_move_with_captures_full(g, opp_gapped_four, opponent);
        if (opp_score >= WIN_SCORE) {
            return opp_gapped_four;
        }
    }

    // ==========================================================
    // ÉTAPE 1.5 : MENACES EXISTANTES
    // ==========================================================
    int existing_opp_block = -1;
    int existing_opp_threat = scan_existing_threats(g, opponent, &existing_opp_block);
    
    if (existing_opp_threat >= OPEN_THREE && existing_opp_block != -1) {
        int my_existing_block = -1;
        int my_existing_threat = scan_existing_threats(g, ia_player, &my_existing_block);
        
        if (my_existing_threat < OPEN_FOUR && my_capture_threat < OPEN_FOUR) {
            return existing_opp_block;
        }
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
    
    int opp_gapped_three = find_gapped_three_hole(g, opponent);
    int opp_gapped_count = count_gapped_threes(g, opponent);

    // ==========================================================
    // ÉTAPE 3 : BLOCAGE MENACES CRITIQUES
    // ==========================================================
    int opp_max_threat = (best_opp_score > opp_capture_threat) ? best_opp_score : opp_capture_threat;
    int my_max_threat = (my_best_score > my_capture_threat) ? my_best_score : my_capture_threat;
    
    // CAS 3.0 : Capture adverse critique
    if (opp_capture_threat >= OPEN_FOUR) {
        if (my_max_threat >= OPEN_FOUR) {
            if (my_best_score >= OPEN_FOUR) return my_best_idx;
            return find_best_capture_move(g, ia_player);
        }
        int capture_block = find_critical_capture_block(g, ia_player);
        if (capture_block != -1) return capture_block;
    }
    
    // CAS 3.1 : CLOSED_FOUR adverse
    if (best_opp_score >= CLOSED_FOUR) {
        if (my_max_threat >= OPEN_FOUR) {
            if (my_best_score >= OPEN_FOUR) return my_best_idx;
            return find_best_capture_move(g, ia_player);
        }
        int block = find_blocking_move(g, opponent);
        if (block != -1) return block;
        return best_opp_idx;
    }

    // CAS 3.2 : OPEN_THREE adverse
    if (best_opp_score >= OPEN_THREE) {
        if (my_max_threat >= OPEN_FOUR) {
            if (my_best_score >= OPEN_FOUR) return my_best_idx;
            return find_best_capture_move(g, ia_player);
        }
        
        if (my_best_score >= OPEN_THREE) {
            int dual = find_best_dual_purpose_move(g, ia_player, opponent);
            if (dual != -1) {
                g->board[dual] = ia_player;
                int my_score_after = get_point_score(g, GET_X(dual), GET_Y(dual), ia_player);
                g->board[dual] = EMPTY;
                
                g->board[dual] = opponent;
                int opp_score_after = get_point_score(g, GET_X(dual), GET_Y(dual), opponent);
                g->board[dual] = EMPTY;
                
                if (my_score_after >= CLOSED_THREE && opp_score_after < best_opp_score) {
                    return dual;
                }
            }
        }
        
        int block = find_blocking_move(g, opponent);
        if (block != -1) return block;
        return best_opp_idx;
    }

    // CAS 3.3 : Gapped Three adverse
    if (opp_gapped_three != -1 && opp_gapped_count > 0) {
        if (my_max_threat >= OPEN_FOUR) {
            if (my_best_score >= OPEN_FOUR) return my_best_idx;
            return find_best_capture_move(g, ia_player);
        }
        return opp_gapped_three;
    }
    
    // CAS 3.4 : Capture adverse sérieuse
    if (opp_capture_threat >= CLOSED_FOUR) {
        if (my_max_threat >= OPEN_FOUR) {
            if (my_best_score >= OPEN_FOUR) return my_best_idx;
            return find_best_capture_move(g, ia_player);
        }
        int capture_block = find_critical_capture_block(g, ia_player);
        if (capture_block != -1) return capture_block;
    }

    // ==========================================================
    // ÉTAPE 4 : ATTAQUES DE L'IA
    // ==========================================================
    if (my_best_score >= OPEN_FOUR) return my_best_idx;
    if (my_capture_threat >= OPEN_FOUR) return find_best_capture_move(g, ia_player);
    
    int my_gapped_three = find_gapped_three_hole(g, ia_player);
    if (my_gapped_three != -1) {
        int score = evaluate_move_with_captures_full(g, my_gapped_three, ia_player);
        if (score >= OPEN_FOUR) return my_gapped_three;
    }

    if (my_best_score >= CLOSED_FOUR) return my_best_idx;
    if (my_best_score >= OPEN_THREE) return my_best_idx;
    
    // ==========================================================
    // ÉTAPE 5 : CAPTURES OFFENSIVES
    // ==========================================================
    if (g->captures[ia_player] >= 1 && best_opp_score < OPEN_THREE) {
        int capture_move = find_best_capture_move(g, ia_player);
        if (capture_move != -1) {
            g->board[capture_move] = ia_player;
            int caps = count_potential_captures(g, GET_X(capture_move), GET_Y(capture_move), ia_player) / 2;
            int alignment = get_point_score(g, GET_X(capture_move), GET_Y(capture_move), ia_player);
            g->board[capture_move] = EMPTY;
            
            if (alignment >= CLOSED_THREE || g->captures[ia_player] + caps >= 3) {
                return capture_move;
            }
        }
    }
    
    // ==========================================================
    // ÉTAPE 6 : CLOSED_THREE adverse
    // ==========================================================
    if (best_opp_score >= CLOSED_THREE) {
        int dual = find_best_dual_purpose_move(g, ia_player, opponent);
        if (dual != -1) {
            g->board[dual] = ia_player;
            int my_score_after = get_point_score(g, GET_X(dual), GET_Y(dual), ia_player);
            g->board[dual] = EMPTY;
            
            if (my_score_after >= CLOSED_THREE) return dual;
        }
        
        int block = find_blocking_move(g, opponent);
        if (block != -1) return block;
    }
    
    // ==========================================================
    // ÉTAPE 7 : CAPTURES DÉFENSIVES
    // ==========================================================
    if (g->captures[opponent] >= 2) {
        int capture_block = find_critical_capture_block(g, ia_player);
        if (capture_block != -1) {
            g->board[capture_block] = opponent;
            int caps = count_potential_captures(g, GET_X(capture_block), GET_Y(capture_block), opponent) / 2;
            g->board[capture_block] = EMPTY;
            
            if (g->captures[opponent] + caps >= 4) return capture_block;
            if (my_best_score < OPEN_THREE) return capture_block;
        }
    }
    
    // ==========================================================
    // ÉTAPE 8 : DÉVELOPPEMENT
    // ==========================================================
    if (best_opp_score < CLOSED_THREE && opp_capture_threat < CLOSED_THREE) {
        int capture_move = find_best_capture_move(g, ia_player);
        if (capture_move != -1 && g->captures[ia_player] >= 1) {
            return capture_move;
        }
    }
    
    // ==========================================================
    // ÉTAPE 9 : MINIMAX
    // ==========================================================
    return -1;
}

// ============================================================================
// MINIMAX
// ============================================================================

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

    if (depth <= 0) return current_eval;

    MoveCandidate moves[MAX_BOARD];
    int tt_move = (entry != NULL) ? entry->best_move : -1; 

    int move_count = generate_moves(g, moves, current_player, depth, tt_move);

    if (move_count == 0) return current_eval;

    int best_val = maximizingPlayer ? (-WIN_SCORE - 1000) : (WIN_SCORE + 1000);
    int best_move_this_node = moves[0].index;

    for (int i = 0; i < move_count; i++) {
        int idx = moves[i].index;

        if (is_double_three(g, idx, current_player)) continue; 

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