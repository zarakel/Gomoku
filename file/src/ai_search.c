#include "../include/gomoku.h"
#include <limits.h>

/* ============================================================================
 * HELPER : Vérification du temps
 * ============================================================================ */
static inline bool is_timeout(clock_t start_time) {
    return ((double)(clock() - start_time) / CLOCKS_PER_SEC >= (double)TIME_LIMIT_MS / 1000.0);
}

/* ============================================================================
 * QUIESCENCE SEARCH (Style Negamax)
 * ============================================================================ */
static int quiescence_search(game *g, int alpha, int beta, int player, int qs_depth, clock_t start_time) {
    if ((debug_node_count & 127) == 0 && is_timeout(start_time)) return TIMEOUT_CODE;

    // Stand Pat
    int stand_pat = evaluate_board(g, player);

    if (qs_depth <= 0) return stand_pat;
    
    if (stand_pat >= WIN_SCORE - 1000) return stand_pat;

    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    // Génération des coups (Captures et Menaces)
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, player, -1, -1); 

    for (int i = 0; i < count; i++) {
        if (!moves[i].is_capture && moves[i].score_estim < OPEN_THREE) continue;

        int idx = moves[i].index;
        if (is_double_three(g, idx, player)) continue;

        MoveUndo undo;
        apply_move(g, idx, player, &undo);

        if (moves[i].is_capture && evaluate_board(g, player) >= WIN_SCORE) {
            undo_move(g, player, &undo);
            return WIN_SCORE;
        }

        int opponent = (player == P1) ? P2 : P1;
        int score = -quiescence_search(g, -beta, -alpha, opponent, qs_depth - 1, start_time);

        undo_move(g, player, &undo);

        if (score == TIMEOUT_CODE || score == -TIMEOUT_CODE) return TIMEOUT_CODE;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

/* ============================================================================
 * NEGAMAX (Avec PVS et Iterative Deepening)
 * ============================================================================ */
int negamax(game *g, int depth, int alpha, int beta, int player, clock_t start_time) {
    debug_node_count++;

    // 1. Timeout Check
    if ((debug_node_count & 255) == 0 && is_timeout(start_time)) return TIMEOUT_CODE;

    // 2. Transposition Table Probe
    int original_alpha = alpha;
    TTEntry *entry = tt_probe(g->current_hash);
    if (entry != NULL && entry->depth >= depth) {
        if (entry->flag == TT_EXACT) return entry->value;
        else if (entry->flag == TT_LOWERBOUND) { 
            if (entry->value > alpha) alpha = entry->value;
        }
        else if (entry->flag == TT_UPPERBOUND) { 
            if (entry->value < beta) beta = entry->value;
        }
        if (alpha >= beta) {
            debug_cutoff_count++;
            return entry->value;
        }
    }

    // 3. Évaluation Terminale
    int current_eval = evaluate_board(g, player);
    if (abs(current_eval) >= WIN_SCORE - 1000) {
        return (current_eval > 0) ? (WIN_SCORE + depth) : (-WIN_SCORE - depth);
    }

    if (depth <= 0) {
        return quiescence_search(g, alpha, beta, player, 2, start_time);
    }

    // 4. Génération
    MoveCandidate moves[MAX_BOARD];
    int tt_move = (entry != NULL) ? entry->best_move : -1;
    int count = generate_moves(g, moves, player, depth, tt_move);

    if (count == 0) return current_eval;

    int best_val = -WIN_SCORE - 20000;
    int best_move = moves[0].index;
    int opponent = (player == P1) ? P2 : P1;

    for (int i = 0; i < count; i++) {
        int idx = moves[i].index;

        MoveUndo undo;
        apply_move(g, idx, player, &undo);

        int val;
        
        // PVS Logic
        if (i == 0) {
            val = -negamax(g, depth - 1, -beta, -alpha, opponent, start_time);
        } else {
            // LMR
            int R = 0;
            if (depth >= 4 && i >= 6 && moves[i].score_estim < OPEN_THREE) R = 1;
            
            val = -negamax(g, depth - 1 - R, -alpha - 1, -alpha, opponent, start_time);
            
            if (val == TIMEOUT_CODE) {
                 undo_move(g, player, &undo);
                 return TIMEOUT_CODE;
            }

            if (R > 0 && val > alpha) {
                 val = -negamax(g, depth - 1, -alpha - 1, -alpha, opponent, start_time);
                 if (val == TIMEOUT_CODE) { 
                     undo_move(g, player, &undo); 
                     return TIMEOUT_CODE; 
                 }
            }
            if (val > alpha && val < beta) {
                val = -negamax(g, depth - 1, -beta, -alpha, opponent, start_time);
            }
        }
        
        undo_move(g, player, &undo);

        // Propagation Timeout
        if (val == TIMEOUT_CODE) return TIMEOUT_CODE;

        if (val > best_val) {
            best_val = val;
            best_move = idx;
        }

        if (val >= beta) {
            debug_cutoff_count++;
            tt_save(g->current_hash, depth, val, TT_LOWERBOUND, best_move);
            return val;
        }

        if (val > alpha) {
            alpha = val;
        }
    }

    // Sauvegarde TT
    if (best_val != TIMEOUT_CODE) {
        int flag = TT_UPPERBOUND;
        if (best_val > original_alpha) flag = TT_EXACT;
        tt_save(g->current_hash, depth, best_val, flag, best_move);
    }
    
    return best_val;
}

int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, int ia_player, clock_t start_time) {
    return negamax(g, depth, alpha, beta, ia_player, start_time);
}