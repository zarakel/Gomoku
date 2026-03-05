#include "../include/gomoku.h"
#include <limits.h>

/**
 * Verifie si le temps imparti est ecoule.
 * Permet d'interrompre la recherche avant le timeout du systeme.
 */
static inline bool is_timeout(clock_t start_time) {
    return ((double)(clock() - start_time) / CLOCKS_PER_SEC >= (double)TIME_LIMIT_MS / 1000.0);
}

/**
 * Recherche de quiescence : prolonge la recherche dans les positions tactiquement instables.
 * 
 * Evite l'effet d'horizon en evaluant les captures et menaces immediates au-dela de la profondeur limite.
 * N'explore que les coups "bruyants" (captures, menaces fortes) jusqu'a une position calme.
 * 
 * Retourne le score de la position, ou TIMEOUT_CODE si le temps est ecoule.
 */
static int quiescence_search(game *g, int alpha, int beta, int player, int qs_depth, clock_t start_time) {
    // Timeout check tous les 64 noeuds
    if ((debug_node_count & 63) == 0 && is_timeout(start_time)) return TIMEOUT_CODE;

    // Stand Pat : evaluation statique, cutoff si >= beta
    int stand_pat = evaluate_board(g, player);

    if (qs_depth <= 0) return stand_pat;
    
    if (stand_pat >= WIN_SCORE - 1000) return stand_pat;

    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    // N'explorer que les captures et menaces serieuses
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, player, -1, -1);
    int opponent = (player == P1) ? P2 : P1;

    for (int i = 0; i < count; i++) {
        // Explorer captures + menaces >= CLOSED_THREE
        if (!moves[i].is_capture && moves[i].score_estim < CLOSED_THREE) continue;
        
        // Proche de mort par capture : focus captures uniquement
        if (g->captures[opponent] >= 4 && !moves[i].is_capture) continue;

        int idx = moves[i].index;
        if (is_double_three(g, idx, player)) continue;

        MoveUndo undo;
        apply_move(g, idx, player, &undo);

        if (moves[i].is_capture && evaluate_board(g, player) >= WIN_SCORE) {
            undo_move(g, player, &undo);
            return WIN_SCORE;
        }

        int score = -quiescence_search(g, -beta, -alpha, opponent, qs_depth - 1, start_time);

        undo_move(g, player, &undo);

        if (score == TIMEOUT_CODE || score == -TIMEOUT_CODE) return TIMEOUT_CODE;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

/**
 * Algorithme Negamax avec elagage alpha-beta et Principal Variation Search.
 * 
 * Variante simplifiee de Minimax exploitant la symetrie : max(a,b) = -min(-a,-b).
 * Optimisations :
 * - Table de transposition (evite de recalculer les positions deja vues)
 * - Elagage alpha-beta (coupe les branches inutiles)
 * - PVS (recherche optimiste avec fenetre nulle apres le premier coup)
 * - Quiescence search (prolonge la recherche dans les positions tactiques)
 * 
 * Retourne le score de la position du point de vue du joueur courant.
 */
int negamax(game *g, int depth, int alpha, int beta, int player, clock_t start_time, bool null_allowed) {
    debug_node_count++;

    // Timeout check tous les 64 noeuds
    if ((debug_node_count & 63) == 0 && is_timeout(start_time)) return TIMEOUT_CODE;

    // TT probe : reutiliser si deja evalue a profondeur suffisante
    int original_alpha = alpha;
    TTEntry *entry = tt_probe(g->current_hash);
    if (entry != NULL && entry->depth >= depth) {
        // Exclure les scores quasi-terminaux (specifiques a leur position)
        if (abs(entry->value) < WIN_SCORE - 50000) {
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
    }

    // Terminal : victoire/defaite ou profondeur limite
    int current_eval = evaluate_board(g, player);
    if (abs(current_eval) >= WIN_SCORE - 1000) {
        return (current_eval > 0) ? (WIN_SCORE + depth) : (-WIN_SCORE - depth);
    }

    int opponent = (player == P1) ? P2 : P1;

    if (depth <= 0) {
        // QS depth fixe a 2 : bon compromis rapidite/precision tactique
        int qs_depth = 2;
        return quiescence_search(g, alpha, beta, player, qs_depth, start_time);
    }

    // NULL MOVE PRUNING : passer son tour, si score reste >= beta -> cutoff
    // Desactive en crise locale ou sequence de mat
    bool local_crisis = (g->max_threat_level[opponent] >= IDX_OPEN_FOUR
                         || g->captures[opponent] >= 4);
    if (null_allowed
        && depth >= 3
        && !local_crisis
        && g->captures[opponent] < 4
        && current_eval >= beta
        && abs(current_eval) < WIN_SCORE - 10000)
    {
        int R = (depth >= 6) ? 3 : 2;
        int null_score = -negamax(g, depth - 1 - R, -beta, -beta + 1, opponent, start_time, false);
        if (null_score == TIMEOUT_CODE || null_score == -TIMEOUT_CODE) return TIMEOUT_CODE;
        if (null_score >= beta) return beta;
    }

    // 4. Generation des coups
    MoveCandidate moves[MAX_BOARD];
    int tt_move = (entry != NULL) ? entry->best_move : -1;
    int count = generate_moves(g, moves, player, depth, tt_move);

    if (count == 0) return current_eval;

    int best_val = -WIN_SCORE - 20000;
    int best_move = moves[0].index;

    for (int i = 0; i < count; i++) {
        int idx = moves[i].index;

        // FUTILITY PRUNING : coups tardifs sans valeur tactique
        // eval + score_estim + marge <= alpha -> break (coups tries par score decroissant)
        if (!local_crisis && abs(current_eval) < WIN_SCORE - 50000) {
            if (depth == 1 && i >= 4
                && moves[i].score_estim < CLOSED_THREE
                && current_eval + moves[i].score_estim + (CLOSED_THREE / 2) <= alpha) {
                break;
            }
            if (depth == 2 && i >= 6
                && moves[i].score_estim < OPEN_TWO
                && current_eval + moves[i].score_estim + CLOSED_THREE <= alpha) {
                break;
            }
            // depth=3 : marge = CLOSED_THREE*2, seuil i>=5
            if (depth == 3 && i >= 5
                && moves[i].score_estim < OPEN_TWO
                && current_eval + moves[i].score_estim + CLOSED_THREE * 2 <= alpha) {
                break;
            }
        }
        MoveUndo undo;
        apply_move(g, idx, player, &undo);

        int val;
        
        // PVS : premier coup en fenetre pleine, suivants en null-window + re-search
        if (i == 0) {
            val = -negamax(g, depth - 1, -beta, -alpha, opponent, start_time, true);
            if (val == TIMEOUT_CODE || val == -TIMEOUT_CODE) {
                undo_move(g, player, &undo);
                return TIMEOUT_CODE;
            }
        } else {
            // LMR : reduction progressive des coups tardifs calmes
            int R = 0;
            if (depth >= 2 && i >= 2 && moves[i].score_estim < CLOSED_THREE) R = 1;
            if (depth >= 3 && i >= 5 && moves[i].score_estim < OPEN_TWO)     R = 2;
            if (depth >= 4 && i >= 7 && moves[i].score_estim < CLOSED_TWO)   R = 3;
            
            val = -negamax(g, depth - 1 - R, -alpha - 1, -alpha, opponent, start_time, true);
            
            if (val == TIMEOUT_CODE) {
                 undo_move(g, player, &undo);
                 return TIMEOUT_CODE;
            }

            // LMR re-search si score depasse alpha
            if (val > alpha && val < beta) {
                val = -negamax(g, depth - 1, -beta, -alpha, opponent, start_time, true);
                if (val == TIMEOUT_CODE || val == -TIMEOUT_CODE) {
                    undo_move(g, player, &undo);
                    return TIMEOUT_CODE;
                }
            }
        }
        
        undo_move(g, player, &undo);

        // Propagation Timeout (inclut -TIMEOUT_CODE issu de la négation)
        if (val == TIMEOUT_CODE || val == -TIMEOUT_CODE) return TIMEOUT_CODE;

        if (val > best_val) {
            best_val = val;
            best_move = idx;
        }

        if (val >= beta) {
            debug_cutoff_count++;
            // Killer moves : memoriser les coups silencieux causant un cutoff
            if (depth >= 0 && depth < MAX_DEPTH) {
                if (killer_moves[depth][0] != idx) {
                    killer_moves[depth][1] = killer_moves[depth][0];
                    killer_moves[depth][0] = idx;
                }
            }
            // History heuristic : bonus proportionnel a depth^2
            if (idx >= 0 && idx < MAX_BOARD) {
                history_heuristic[idx] += depth * depth;
                if (history_heuristic[idx] > 200000) history_heuristic[idx] = 200000;
            }
            tt_save(g->current_hash, depth, val, TT_LOWERBOUND, best_move);
            return val;
        }

        if (val > alpha) {
            alpha = val;
        }
    }

    // Sauvegarde TT (exclure quasi-terminaux)
    if (best_val != TIMEOUT_CODE && abs(best_val) < WIN_SCORE - 50000) {
        int flag = TT_UPPERBOUND;
        if (best_val > original_alpha) flag = TT_EXACT;
        tt_save(g->current_hash, depth, best_val, flag, best_move);
    }
    
    return best_val;
}