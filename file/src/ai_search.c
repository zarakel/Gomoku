#include "../include/gomoku.h"
#include <limits.h>

/* ============================================================================
 * HELPER : Vérification du temps (Inline pour la perf)
 * ============================================================================ */
static inline int check_timeout(clock_t start_time) {
    if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC >= TIME_LIMIT_MS) {
        return 1;
    }
    return 0;
}

/* ============================================================================
 * QUIESCENCE SEARCH : Sécurisée et Optimisée
 * Ne regarde que les coups "bruyants" (Menaces et Captures)
 * ============================================================================ */

static int quiescence_search(game *g, int alpha, int beta, int ia_player, int qs_depth, clock_t start_time) {
    // 1. Sécurité Temps : On vérifie souvent (tous les 128 noeuds)
    if ((debug_node_count & 127) == 0) {
        if (check_timeout(start_time)) return -2;
    }

    // 2. Évaluation statique (Stand Pat)
    // L'idée est : "Si je ne fais rien, quel est mon score ?"
    // Si ce score est déjà suffisant pour couper (beta), on s'arrête.
    int stand_pat = evaluate_board(g, ia_player);
    
    // Limite de profondeur atteinte ou victoire/défaite immédiate
    if (qs_depth <= 0) return stand_pat;
    if (stand_pat >= WIN_SCORE - 1000) return stand_pat;
    if (stand_pat <= -WIN_SCORE + 1000) return stand_pat;
    
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;
    
    int opponent = (ia_player == P1) ? P2 : P1;
    
    // 3. Génération optimisée
    MoveCandidate moves[MAX_BOARD];
    // On passe -1 en depth pour dire "génération brute sans heuristique historique"
    // On passe -1 en tt_best_move car on n'a pas de TT pour la QS
    int count = generate_moves(g, moves, ia_player, -1, -1);
    
    for (int i = 0; i < count; i++) {
        // 4. FILTRE CRITIQUE : On ne regarde que les coups "intéressants"
        // - Les captures (marquées par is_capture = true)
        // - Les menaces fortes (Open 3 ou plus)
        if (!moves[i].is_capture && moves[i].score_estim < OPEN_THREE) continue;

        // Check temps fréquent dans la boucle
        if ((debug_node_count & 63) == 0) {
             if (check_timeout(start_time)) return -2;
        }
        
        int idx = moves[i].index;

        if (is_double_three(g, idx, ia_player)) continue;
        
        // Simulation du coup
        MoveUndo undo;
        apply_move(g, idx, ia_player, &undo);
        
        // --- CHECK VICTOIRE APRÈS CAPTURE (NOUVEAU) ---
        // Si c'était une capture, est-ce qu'elle a débloqué une victoire immédiate ?
        if (moves[i].is_capture) {
            int score_after = evaluate_board(g, ia_player);
            if (score_after >= WIN_SCORE) {
                undo_move(g, ia_player, &undo);
                return score_after;
            }
        }
        // ---------------------------------------------
        
        // Appel récursif (Note: on passe start_time et on décrémente qs_depth)
        int score = -quiescence_search(g, -beta, -alpha, opponent, qs_depth - 1, start_time);
        
        undo_move(g, ia_player, &undo);
        
        // Propagation du Timeout
        if (score == -2) return -2;
        
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    
    return alpha;
}

// ============================================================================
// MINIMAX (Iterative Deepening Engine)
// ============================================================================

int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, int ia_player, clock_t start_time) {
    debug_node_count++;

    // 1. Sécurité Temps (Check rapide)
    if ((debug_node_count & 127) == 0) {
        if (check_timeout(start_time)) return -2;
    }

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
    
    int current_player = maximizingPlayer ? ia_player : ((ia_player == P1) ? P2 : P1);

    int current_eval = evaluate_board(g, ia_player);

    // Si on détecte une "défaite certaine", on vérifie qu'elle est réelle
    if (abs(current_eval) >= WIN_SCORE - 100) {
        // Est-ce vraiment une défaite IMMÉDIATE ?
        int opponent = (ia_player == P1) ? P2 : P1;
        
        bool opponent_can_win_now = false;
        for (int i = 0; i < MAX_BOARD && !opponent_can_win_now; i++) {
            if (g->board[i] != EMPTY) continue;
            
            g->board[i] = opponent;
            int test_score = get_point_score(g, GET_X(i), GET_Y(i), opponent);
            g->board[i] = EMPTY;
            
            if (test_score >= WIN_SCORE) {
                opponent_can_win_now = true;
            }
        }
        
        // Si l'adversaire NE PEUT PAS gagner immédiatement, on réduit le score
        if (!opponent_can_win_now && current_eval < 0) {
            current_eval = -OPEN_FOUR; // Danger mais pas mort
        }
    }

    // Si victoire/défaite ABSOLUE (Alignement de 5 ou 5 Captures)
    // Note : On utilise WIN_SCORE - 100 pour laisser une marge aux pénalités
    if (abs(current_eval) >= WIN_SCORE - 100) {
        // On retourne le score ajusté à la profondeur pour privilégier le chemin le plus court/long
        return (current_eval > 0) ? (WIN_SCORE + depth) : (-WIN_SCORE - depth);
    }

    // 2. Feuilles : Lancer la Quiescence Search (avec timeout !)
    if (depth <= 0) {
        // On limite la QS à 4 coups de profondeur max pour éviter l'explosion
        return quiescence_search(g, alpha, beta, ia_player, 4, start_time);
    }

    MoveCandidate moves[MAX_BOARD];
    int tt_move = (entry != NULL) ? entry->best_move : -1; 

    // Génération des coups
    int move_count = generate_moves(g, moves, current_player, depth, tt_move);

    if (move_count == 0) return current_eval;

    int best_val = maximizingPlayer ? (-WIN_SCORE - 10000) : (WIN_SCORE + 10000);
    int best_move_this_node = moves[0].index;

    for (int i = 0; i < move_count; i++) {

        // On vérifie le temps AVANT de lancer une branche coûteuse
        if ((debug_node_count & 63) == 0) { // Check fréquent
             if (check_timeout(start_time)) return -2;
        }

        int idx = moves[i].index;

        if (is_double_three(g, idx, current_player)) continue; 

        MoveUndo undo;
        apply_move(g, idx, current_player, &undo);
        
        int val;
        
        // Principal Variation Search (PVS)
        if (i == 0) {
            val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
        } else {
            int reduction = 0;
            // Late Move Reduction (LMR) - Réduit la profondeur pour les coups moins bons
            if (depth >= 4 && i >= 4) reduction = 1;
            if (depth >= 6 && i >= 8) reduction = 2;
            
            if (maximizingPlayer) {
                val = minimax(g, depth - 1 - reduction, alpha, alpha + 1, !maximizingPlayer, ia_player, start_time);
            } else {
                val = minimax(g, depth - 1 - reduction, beta - 1, beta, !maximizingPlayer, ia_player, start_time);
            }

            // Re-recherche si la réduction était mauvaise
            if (reduction > 0 && ((maximizingPlayer && val > alpha) || (!maximizingPlayer && val < beta))) {
                val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
            }
            // Re-recherche si la fenêtre nulle (alpha+1) a échoué
            if (i > 0 && reduction == 0 && ((maximizingPlayer && val > alpha && val < beta) || (!maximizingPlayer && val < beta && val > alpha))) {
                 val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
            }
            int best_val = maximizingPlayer ? (-WIN_SCORE - 10000) : (WIN_SCORE + 10000);
        }

        undo_move(g, current_player, &undo);
        
        // 3. Propagation IMMEDIATE du Timeout
        if (val == -2) return -2;

        if (maximizingPlayer) {
            if (val > best_val) { best_val = val; best_move_this_node = idx; }
            if (val >= beta) {
                debug_cutoff_count++;
                tt_save(g->current_hash, depth, val, TT_LOWERBOUND, best_move_this_node);
                
                // --- AJOUTER CECI ---
                if (g->board[best_move_this_node] == EMPTY) { // Si c'est un coup calme (pas capture)
                    // 1. Killer Moves
                    if (killer_moves[depth][0] != best_move_this_node) {
                        killer_moves[depth][1] = killer_moves[depth][0];
                        killer_moves[depth][0] = best_move_this_node;
                    }
                    
                    // 2. History Heuristic
                    // On augmente le score de ce coup (max 20000 pour éviter overflow)
                    history_heuristic[best_move_this_node] += (depth * depth);
                    if (history_heuristic[best_move_this_node] > 2000000) {
                        // Downscale si ça devient trop gros (tous les scores / 2)
                        for(int k=0; k<MAX_BOARD; k++) history_heuristic[k] /= 2;
                    }
                }
                // --------------------

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