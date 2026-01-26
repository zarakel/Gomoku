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
    int stand_pat = evaluate_board(g, ia_player);
    
    // Limite de profondeur atteinte
    if (qs_depth <= 0) return stand_pat;
    
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;
    
    int opponent = (ia_player == P1) ? P2 : P1;
    
    // 3. Génération optimisée (utilise ton nouveau generate_moves rapide)
    MoveCandidate moves[MAX_BOARD];
    // On passe -1 en depth pour dire "génération brute sans heuristique historique"
    int count = generate_moves(g, moves, ia_player, -1, -1);
    
    for (int i = 0; i < count; i++) {
        // 4. FILTRE CRITIQUE : On ne regarde que les menaces réelles
        // Si le coup ne crée pas au moins un OPEN_THREE (ou ne bloque pas), on l'ignore.
        // Cela réduit drastiquement l'arbre de recherche.
        if (moves[i].score_estim < OPEN_THREE) continue;

        if ((debug_node_count & 63) == 0) {
             if (check_timeout(start_time)) return -2;
        }
        
        int idx = moves[i].index;
        
        MoveUndo undo;
        apply_move(g, idx, ia_player, &undo);
        
        // Appel récursif (Note: on passe start_time pour le timeout)
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
    // Coupure si victoire déjà acquise (optimisation)
    if (abs(current_eval) >= WIN_SCORE - 1000) {
        // Si on gagne, on préfère gagner TÔT (current_eval est positif) -> on enlève depth
        // Si on perd, on préfère perdre TARD (current_eval est négatif) -> on ajoute depth
        return (current_eval > 0) ? (current_eval - depth) : (current_eval + depth);
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