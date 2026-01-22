#include "../include/gomoku.h"
#include <limits.h>

// ============================================================================
// MINIMAX (SEULE FONCTION RESTANTE)
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